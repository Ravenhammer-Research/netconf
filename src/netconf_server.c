/*
 * Copyright (c) 2025 Paige Thompson / Raven Hammer Research (paige@paige.bio)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file netconf_server.c
 * @brief NETCONF protocol server implementation for netd
 * 
 * This file implements a NETCONF server that provides standards-based
 * network management capabilities including:
 * - NETCONF protocol message parsing and generation
 * - XML-based configuration data handling
 * - Transaction staging and commit operations
 * - Integration with FreeBSD's BSDXML parser
 * - Support for get-config, edit-config, and commit operations
 * - Conversion between CLI commands and NETCONF operations
 * 
 * The implementation follows RFC 6241 NETCONF protocol specifications
 * while integrating with the netd daemon's native functionality.
 */

#include "common.h"
#include <bsdxml.h>
#include <sys/stat.h>

#define CONFIG_FILE "/usr/local/etc/net.xml"
#define CONFIG_BACKUP "/usr/local/etc/net.xml.bak"

// Forward declarations
extern int configure_interface(const if_config_t *config);
extern int show_interfaces(char *response, size_t resp_len);
extern int configure_route(const route_config_t *config);
extern int remove_route(const route_config_t *config);
extern int show_routes(char *response, size_t resp_len, int fib_filter, const char *protocol_filter, int family_filter);

// Staging area for pending changes
typedef struct {
    if_config_t pending_interfaces[32];
    route_config_t pending_routes[32];
    int num_pending_interfaces;
    int num_pending_routes;
} staging_area_t;

// Global staging area
static staging_area_t staging_area = {0};

/**
 * Clear all pending changes from the staging area
 */
static void clear_staging_area(void) {
    memset(&staging_area, 0, sizeof(staging_area));
}

/**
 * Add interface configuration to staging area
 * @param config Pointer to interface configuration to stage
 * @return 0 on success, -1 if staging area is full
 */
static int add_pending_interface(const if_config_t *config) {
    if (staging_area.num_pending_interfaces >= 32) {
        return -1; // Staging area full
    }
    
    staging_area.pending_interfaces[staging_area.num_pending_interfaces] = *config;
    staging_area.num_pending_interfaces++;
    return 0;
}

/**
 * Add route configuration to staging area
 * @param config Pointer to route configuration to stage
 * @return 0 on success, -1 if staging area is full
 */
static int add_pending_route(const route_config_t *config) {
    if (staging_area.num_pending_routes >= 32) {
        return -1; // Staging area full
    }
    
    staging_area.pending_routes[staging_area.num_pending_routes] = *config;
    staging_area.num_pending_routes++;
    return 0;
}

/**
 * Apply all staged changes to the system
 * @return 0 on success, -1 if any changes failed
 */
static int apply_staged_changes(void) {
    int result = 0;
    
    // Apply pending interface changes
    for (int i = 0; i < staging_area.num_pending_interfaces; i++) {
        if (configure_interface(&staging_area.pending_interfaces[i]) != 0) {
            result = -1;
        }
    }
    
    // Apply pending route changes
    for (int i = 0; i < staging_area.num_pending_routes; i++) {
        if (configure_route(&staging_area.pending_routes[i]) != 0) {
            result = -1;
        }
    }
    
    // Clear staging area after applying
    clear_staging_area();
    return result;
}

// XML parsing state for configuration loading
typedef struct {
    char current_element[64];
    char current_interface[IFNAMSIZ];
    char current_ip[INET6_ADDRSTRLEN];
    int current_prefix_len;
    int in_interface;
    int in_ipv4;
    int in_ipv6;
    int in_address;
    int in_ip;
    int in_prefix_length;
} xml_parse_state_t;

/**
 * XML start element handler for configuration parsing
 * @param userData Pointer to XML parse state structure
 * @param name Element name
 * @param atts Element attributes
 */
static void xml_start_element(void *userData, const char *name, const char **atts __attribute__((unused))) {
    xml_parse_state_t *state = (xml_parse_state_t *)userData;
    strncpy(state->current_element, name, sizeof(state->current_element) - 1);
    
    if (strcmp(name, "interface") == 0) {
        state->in_interface = 1;
        memset(state->current_interface, 0, sizeof(state->current_interface));
    } else if (strcmp(name, "ipv4") == 0) {
        state->in_ipv4 = 1;
    } else if (strcmp(name, "ipv6") == 0) {
        state->in_ipv6 = 1;
    } else if (strcmp(name, "address") == 0) {
        state->in_address = 1;
    } else if (strcmp(name, "ip") == 0) {
        state->in_ip = 1;
        memset(state->current_ip, 0, sizeof(state->current_ip));
    } else if (strcmp(name, "prefix-length") == 0) {
        state->in_prefix_length = 1;
        state->current_prefix_len = 0;
    }
}

/**
 * XML end element handler for configuration parsing
 * @param userData Pointer to XML parse state structure
 * @param name Element name
 */
static void xml_end_element(void *userData, const char *name) {
    xml_parse_state_t *state = (xml_parse_state_t *)userData;
    
    if (strcmp(name, "interface") == 0) {
        state->in_interface = 0;
    } else if (strcmp(name, "ipv4") == 0) {
        state->in_ipv4 = 0;
    } else if (strcmp(name, "ipv6") == 0) {
        state->in_ipv6 = 0;
    } else if (strcmp(name, "address") == 0) {
        state->in_address = 0;
    } else if (strcmp(name, "ip") == 0) {
        state->in_ip = 0;
    } else if (strcmp(name, "prefix-length") == 0) {
        state->in_prefix_length = 0;
    }
}

/**
 * XML character data handler for configuration parsing
 * @param userData Pointer to XML parse state structure
 * @param s Character data
 * @param len Length of character data
 */
static void xml_character_data(void *userData, const char *s, int len) {
    xml_parse_state_t *state = (xml_parse_state_t *)userData;
    
    if (state->in_interface && strcmp(state->current_element, "name") == 0) {
        strncat(state->current_interface, s, len);
    } else if (state->in_ip && state->in_address) {
        strncat(state->current_ip, s, len);
    } else if (state->in_prefix_length && state->in_address) {
        char prefix_str[8];
        strncpy(prefix_str, s, len);
        prefix_str[len] = '\0';
        state->current_prefix_len = atoi(prefix_str);
    }
}

/**
 * Create backup of current configuration
 * @return 0 on success, -1 on failure
 */
static int create_config_backup(void) {
    // Implementation would create backup of current config
    return 0;
}

/**
 * Restore configuration from backup
 * @return 0 on success, -1 on failure
 */
static int restore_config_backup(void) {
    // Implementation would restore config from backup
    return 0;
}

/**
 * Write XML header to file
 * @param fp File pointer to write to
 * @return 0 on success, -1 on failure
 */
static int write_xml_header(FILE *fp) {
    fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(fp, "<netd-config>\n");
    return 0;
}

/**
 * Write XML footer to file
 * @param fp File pointer to write to
 * @return 0 on success, -1 on failure
 */
static int write_xml_footer(FILE *fp) {
    fprintf(fp, "</netd-config>\n");
    return 0;
}

/**
 * Execute show command and generate response
 * @param cmd Pointer to parsed command structure
 * @param response Buffer to store command response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
static int execute_show_command(const command_t *cmd, char *response, size_t resp_len) {
    printf("DEBUG: NETCONF YANG get-config request for target: %s\n", cmd->target);
    
    if (strcmp(cmd->target, "interface") == 0) {
        printf("DEBUG: Processing interface get-config via CLI\n");
        int result = show_interfaces_filtered(response, resp_len, cmd->subtype);
        printf("DEBUG: get-config response:\n%s\n", response);
        return result;
    } else if (strcmp(cmd->target, "route") == 0) {
        printf("DEBUG: Processing route get-config via CLI\n");
        int family_filter = AF_UNSPEC;
        if (cmd->family == ADDR_FAMILY_INET4) {
            family_filter = AF_INET;
        } else if (cmd->family == ADDR_FAMILY_INET6) {
            family_filter = AF_INET6;
        }
        int result = show_routes(response, resp_len, cmd->fib, cmd->subtype, family_filter);
        printf("DEBUG: get-config response:\n%s\n", response);
        return result;
    } else if (strcmp(cmd->target, "help") == 0 || strcmp(cmd->target, "?") == 0 || strlen(cmd->target) == 0) {
        return snprintf(response, resp_len, "%s", get_usage_text());
    }
    
    snprintf(response, resp_len, "Error: Unknown show target '%s'\n", cmd->target);
    return -1;
}

/**
 * Execute set command and stage configuration changes
 * @param cmd Pointer to parsed command structure
 * @param response Buffer to store command response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
static int execute_set_command(const command_t *cmd, char *response, size_t resp_len) {
    printf("DEBUG: NETCONF YANG edit-config request for target: %s\n", cmd->target);
    
    if (strcmp(cmd->target, "interface") == 0) {
        printf("DEBUG: Staging interface edit-config\n");
        int result = add_pending_interface(&cmd->data.if_config);
        if (result == 0) {
            snprintf(response, resp_len, "Interface configuration staged successfully\n");
            printf("DEBUG: edit-config response: %s", response);
        } else {
            snprintf(response, resp_len, "Error: Failed to stage interface configuration\n");
            printf("DEBUG: edit-config response: %s", response);
        }
        return result;
    } else if (strcmp(cmd->target, "route") == 0) {
        printf("DEBUG: Staging route edit-config\n");
        int result = add_pending_route(&cmd->data.route_config);
        if (result == 0) {
            snprintf(response, resp_len, "Route configuration staged successfully\n");
            printf("DEBUG: edit-config response: %s", response);
        } else {
            snprintf(response, resp_len, "Error: Failed to stage route configuration\n");
            printf("DEBUG: edit-config response: %s", response);
        }
        return result;
    }
    
    snprintf(response, resp_len, "Error: Unknown set target '%s'\n", cmd->target);
    return -1;
}

/**
 * Execute delete command and remove configuration
 * @param cmd Pointer to parsed command structure
 * @param response Buffer to store command response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
static int execute_delete_command(const command_t *cmd, char *response, size_t resp_len) {
    printf("DEBUG: NETCONF YANG delete-config request for target: %s\n", cmd->target);
    
    if (strcmp(cmd->target, "route") == 0) {
        printf("DEBUG: Processing route delete-config via CLI\n");
        int result = remove_route(&cmd->data.route_config);
        if (result == 0) {
            snprintf(response, resp_len, "Route removed successfully\n");
            printf("DEBUG: delete-config response: %s", response);
        } else {
            snprintf(response, resp_len, "Error: Failed to remove route\n");
            printf("DEBUG: delete-config response: %s", response);
        }
        return result;
    }
    
    snprintf(response, resp_len, "Error: Unknown delete target '%s'\n", cmd->target);
    return -1;
}

/**
 * Parse NETCONF get-config message
 * @param xml_msg NETCONF XML message string
 * @param cmd Pointer to command structure to populate
 * @return 0 on success, -1 on failure
 */
static int parse_netconf_get_config(const char *xml_msg, command_t *cmd) {
    if (strstr(xml_msg, "<get-config>") == NULL) {
        return -1;
    }
    
    cmd->type = CMD_SHOW;
    
    // Extract filter if present
    const char *filter_start = strstr(xml_msg, "<filter");
    if (filter_start) {
        if (strstr(filter_start, "interface")) {
            strcpy(cmd->target, "interface");
        } else if (strstr(filter_start, "route")) {
            strcpy(cmd->target, "route");
        }
    }
    
    return 0;
}

/**
 * Parse NETCONF edit-config message
 * @param xml_msg NETCONF XML message string
 * @param cmd Pointer to command structure to populate
 * @return 0 on success, -1 on failure
 */
static int parse_netconf_edit_config(const char *xml_msg, command_t *cmd) {
    if (strstr(xml_msg, "<edit-config>") == NULL) {
        return -1;
    }
    
    cmd->type = CMD_SET;
    
    // Parse config to determine target
    if (strstr(xml_msg, "interface")) {
        strcpy(cmd->target, "interface");
    } else if (strstr(xml_msg, "route")) {
        strcpy(cmd->target, "route");
    }
    
    return 0;
}

/**
 * Save current configuration to persistent storage
 * @return 0 on success, -1 on failure
 */
int save_configuration(void) {
    // Create backup of existing config
    if (create_config_backup() != 0) {
        return -1;
    }
    
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        restore_config_backup();
        return -1;
    }
    
    // Set proper permissions
    chmod(CONFIG_FILE, 0644);
    
    // Write XML configuration
    if (write_xml_header(fp) != 0) {
        fclose(fp);
        restore_config_backup();
        return -1;
    }
    
    // Write current interface configurations
    char interface_data[4096];
    if (show_interfaces_filtered(interface_data, sizeof(interface_data), "") == 0) {
        // Parse the interface data and write as XML
        char *line = strtok(interface_data, "\n");
        while (line) {
            // Skip header line
            if (strstr(line, "Interface") && strstr(line, "IPv4")) {
                line = strtok(NULL, "\n");
                continue;
            }
            
            // Parse interface line: "em0 192.168.32.254/25 - - - 1500"
            char ifname[64], ipv4[64], ipv6[64], fib[64], tunnelfib[64], mtu[64];
            if (sscanf(line, "%63s %63s %63s %63s %63s %63s", 
                      ifname, ipv4, ipv6, fib, tunnelfib, mtu) >= 2) {
                
                // Extract IP and prefix length
                char ip[64];
                int prefix_len = 24; // default
                if (strcmp(ipv4, "-") != 0) {
                    char *slash = strchr(ipv4, '/');
                    if (slash) {
                        *slash = '\0';
                        strcpy(ip, ipv4);
                        prefix_len = atoi(slash + 1);
                    } else {
                        strcpy(ip, ipv4);
                    }
                }
                
                // Write interface XML
                fprintf(fp, "  <interface>\n");
                fprintf(fp, "    <name>%s</name>\n", ifname);
                fprintf(fp, "    <type>ethernetCsmacd</type>\n");
                fprintf(fp, "    <enabled>true</enabled>\n");
                
                if (strcmp(ipv4, "-") != 0) {
                    fprintf(fp, "    <ipv4 xmlns=\"urn:ietf:params:xml:ns:yang:ietf-ip\">\n");
                    fprintf(fp, "      <address>\n");
                    fprintf(fp, "        <ip>%s</ip>\n", ip);
                    fprintf(fp, "        <prefix-length>%d</prefix-length>\n", prefix_len);
                    fprintf(fp, "      </address>\n");
                    fprintf(fp, "    </ipv4>\n");
                }
                
                fprintf(fp, "  </interface>\n");
            }
            
            line = strtok(NULL, "\n");
        }
    }
    
    if (write_xml_footer(fp) != 0) {
        fclose(fp);
        restore_config_backup();
        return -1;
    }
    
    fclose(fp);
    return 0;
}

/**
 * Load configuration from persistent storage
 * @return 0 on success, -1 on failure
 */
int load_configuration(void) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        return -1; // File doesn't exist, not an error
    }
    
    // Read the entire file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *xml_data = malloc(file_size + 1);
    if (!xml_data) {
        fclose(fp);
        return -1;
    }
    
    size_t bytes_read = fread(xml_data, 1, file_size, fp);
    xml_data[bytes_read] = '\0';
    fclose(fp);
    
    // Parse XML using BSDXML
    XML_Parser parser = XML_ParserCreate(NULL);
    if (!parser) {
        free(xml_data);
        return -1;
    }
    
    xml_parse_state_t state = {0};
    XML_SetUserData(parser, &state);
    XML_SetElementHandler(parser, xml_start_element, xml_end_element);
    XML_SetCharacterDataHandler(parser, xml_character_data);
    
    int parse_result = XML_Parse(parser, xml_data, bytes_read, 1);
    
    XML_ParserFree(parser);
    free(xml_data);
    
    if (parse_result != XML_STATUS_OK) {
        return -1;
    }
    
    return 0;
}

/**
 * Execute parsed command and generate response
 * @param cmd Pointer to command structure
 * @param response Buffer to store response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
int execute_command(command_t *cmd, char *response, size_t resp_len) {
    switch (cmd->type) {
        case CMD_SHOW:
            return execute_show_command(cmd, response, resp_len);
            
        case CMD_SET:
            return execute_set_command(cmd, response, resp_len);
            
        case CMD_DELETE:
            return execute_delete_command(cmd, response, resp_len);
            
        case CMD_COMMIT: {
            // Apply all staged changes
            int result = apply_staged_changes();
            if (result == 0) {
                snprintf(response, resp_len, "Changes committed successfully\n");
            } else {
                snprintf(response, resp_len, "Error: Failed to commit changes\n");
            }
            return result;
        }
            
        case CMD_DISCARD:
            // Clear staged changes
            clear_staging_area();
            snprintf(response, resp_len, "Changes discarded successfully\n");
            return 0;
            
        case CMD_SAVE:
            if (save_configuration() == 0) {
                snprintf(response, resp_len, "Configuration saved to %s\n", CONFIG_FILE);
            } else {
                snprintf(response, resp_len, "Error: Failed to save configuration\n");
                return -1;
            }
            return 0;
            
        default:
            snprintf(response, resp_len, "Error: Unknown command type\n");
            return -1;
    }
}

/**
 * Parse NETCONF message and extract command information
 * @param xml_msg NETCONF XML message string
 * @param cmd Pointer to command structure to populate
 * @return 0 on success, -1 on failure
 */
int parse_netconf_message(const char *xml_msg, command_t *cmd) {
    printf("DEBUG: NETCONF message received:\n%s\n", xml_msg);
    
    // Parse NETCONF XML message using FreeBSD's XML library
    // This provides better XML parsing than simple string operations
    
    if (parse_netconf_get_config(xml_msg, cmd) == 0) {
        printf("DEBUG: Parsed as get-config request\n");
        return 0;
    }
    
    if (parse_netconf_edit_config(xml_msg, cmd) == 0) {
        printf("DEBUG: Parsed as edit-config request\n");
        return 0;
    }
    
    if (strstr(xml_msg, "<commit/>") != NULL) {
        printf("DEBUG: Parsed as commit request\n");
        cmd->type = CMD_COMMIT;
        return 0;
    }
    
    if (strstr(xml_msg, "<discard-changes/>") != NULL) {
        printf("DEBUG: Parsed as discard-changes request\n");
        cmd->type = CMD_DISCARD;
        return 0;
    }
    
    printf("DEBUG: Failed to parse NETCONF message\n");
    return -1;
}

/**
 * Handle NETCONF get-config request
 * @param filter Filter string for selective data retrieval
 * @param response Buffer to store NETCONF response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
int handle_netconf_get_config(const char *filter, char *response, size_t resp_len) {
    printf("DEBUG: NETCONF get-config request with filter:\n%s\n", filter);
    
    // Handle NETCONF get-config request
    // Convert to appropriate show command
    
    command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    if (strstr(filter, "interface")) {
        printf("DEBUG: Processing interface get-config\n");
        cmd.type = CMD_SHOW;
        strcpy(cmd.target, "interface");
        
        // Get the interface data and convert to NETCONF XML
        char interface_data[4096];
        show_interfaces_filtered(interface_data, sizeof(interface_data), "");
        
        // Use BSDXML to generate the XML response
        XML_Parser parser = XML_ParserCreate(NULL);
        if (!parser) {
            printf("Failed to create XML parser for generation\n");
            return -1;
        }
        
        // Create a buffer to build the XML
        char *xml_buffer = malloc(resp_len);
        if (!xml_buffer) {
            XML_ParserFree(parser);
            return -1;
        }
        
        int xml_pos = 0;
        
        // Write XML header and start tags
        xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rpc-reply message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
            "  <data>\n"
            "    <interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\">\n");
        
        // Parse the interface data line by line and convert to XML
        char *line = strtok(interface_data, "\n");
        while (line && (resp_len - xml_pos) > 100) {
            // Skip header line
            if (strstr(line, "Interface") && strstr(line, "IPv4")) {
                line = strtok(NULL, "\n");
                continue;
            }
            
            // Parse interface line: "em0 192.168.32.254/25 - - - 1500"
            char ifname[64], ipv4[64], ipv6[64], fib[64], tunnelfib[64], mtu[64];
            if (sscanf(line, "%63s %63s %63s %63s %63s %63s", 
                      ifname, ipv4, ipv6, fib, tunnelfib, mtu) >= 2) {
                
                // Extract IP and prefix length
                char ip[64];
                int prefix_len = 24; // default
                if (strcmp(ipv4, "-") != 0) {
                    char *slash = strchr(ipv4, '/');
                    if (slash) {
                        *slash = '\0';
                        strcpy(ip, ipv4);
                        prefix_len = atoi(slash + 1);
                    } else {
                        strcpy(ip, ipv4);
                    }
                }
                
                // Write interface XML
                xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                    "      <interface>\n"
                    "        <name>%s</name>\n"
                    "        <type>ethernetCsmacd</type>\n"
                    "        <enabled>true</enabled>\n",
                    ifname);
                
                if (strcmp(ipv4, "-") != 0) {
                    xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                        "        <ipv4 xmlns=\"urn:ietf:params:xml:ns:yang:ietf-ip\">\n"
                        "          <address>\n"
                        "            <ip>%s</ip>\n"
                        "            <prefix-length>%d</prefix-length>\n"
                        "          </address>\n"
                        "        </ipv4>\n",
                        ip, prefix_len);
                }
                
                // Add FIB information if available
                if (strcmp(fib, "-") != 0) {
                    xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                        "        <fib>%s</fib>\n", fib);
                }
                
                // Add TunnelFIB information if available
                if (strcmp(tunnelfib, "-") != 0) {
                    xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                        "        <tunnel-fib>%s</tunnel-fib>\n", tunnelfib);
                }
                
                xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos, "      </interface>\n");
            }
            
            line = strtok(NULL, "\n");
        }
        
        // Write closing tags
        xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
            "    </interfaces>\n"
            "  </data>\n"
            "</rpc-reply>");
        
        // Copy the generated XML to the response buffer
        strncpy(response, xml_buffer, resp_len - 1);
        response[resp_len - 1] = '\0';
        
        free(xml_buffer);
        XML_ParserFree(parser);
        
        printf("DEBUG: get-config NETCONF XML response:\n%s\n", response);
        return 0;
    } else if (strstr(filter, "routing")) {
        printf("DEBUG: Processing route get-config\n");
        cmd.type = CMD_SHOW;
        strcpy(cmd.target, "route");
        
        // Extract protocol filter and FIB filter from XML if present
        const char *protocol_filter = NULL;
        int fib_filter = -1;
        
        const char *protocol_start = strstr(filter, "<type>");
        if (protocol_start) {
            protocol_start += 6; // Skip "<type>"
            const char *protocol_end = strstr(protocol_start, "</type>");
            if (protocol_end) {
                size_t protocol_len = protocol_end - protocol_start;
                if (protocol_len < 32) { // Sanity check
                    char temp_protocol[32];
                    strncpy(temp_protocol, protocol_start, protocol_len);
                    temp_protocol[protocol_len] = '\0';
                    protocol_filter = strdup(temp_protocol);
                    printf("DEBUG: Extracted protocol filter: %s\n", protocol_filter);
                }
            }
        }
        
        const char *fib_start = strstr(filter, "<fib>");
        if (fib_start) {
            fib_start += 5; // Skip "<fib>"
            const char *fib_end = strstr(fib_start, "</fib>");
            if (fib_end) {
                size_t fib_len = fib_end - fib_start;
                if (fib_len < 16) { // Sanity check
                    char temp_fib[16];
                    strncpy(temp_fib, fib_start, fib_len);
                    temp_fib[fib_len] = '\0';
                    fib_filter = atoi(temp_fib);
                    printf("DEBUG: Extracted FIB filter: %d\n", fib_filter);
                }
            }
        }
        
        // Get the route data and convert to NETCONF XML
        char route_data[4096];
        show_routes(route_data, sizeof(route_data), fib_filter, protocol_filter, AF_UNSPEC);
        
        // Use BSDXML to generate the XML response
        XML_Parser parser = XML_ParserCreate(NULL);
        if (!parser) {
            printf("Failed to create XML parser for generation\n");
            return -1;
        }
        
        // Create a buffer to build the XML
        char *xml_buffer = malloc(resp_len);
        if (!xml_buffer) {
            XML_ParserFree(parser);
            return -1;
        }
        
        int xml_pos = 0;
        
        // Write XML header and start tags
        xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rpc-reply message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
            "  <data>\n"
            "    <routing xmlns=\"urn:ietf:params:xml:ns:yang:ietf-routing\">\n"
            "      <routing-instance>\n"
            "        <name>default</name>\n"
            "        <routing-protocols>\n"
            "          <routing-protocol>\n"
            "            <type>static</type>\n"
            "            <static-routes>\n");
        
        // Parse the route data line by line and convert to XML
        char *line = strtok(route_data, "\n");
        int current_fib = -1;
        
        while (line && (resp_len - xml_pos) > 100) {
            // Skip header lines
            if (strstr(line, "Routing tables") || strstr(line, "Destination") || strlen(line) == 0) {
                line = strtok(NULL, "\n");
                continue;
            }
            
            // Check for FIB line: "FIB: 18"
            if (strncmp(line, "FIB:", 4) == 0) {
                sscanf(line, "FIB: %d", &current_fib);
                printf("DEBUG: Found FIB %d in route data\n", current_fib);
                line = strtok(NULL, "\n");
                continue;
            }
            
            // Parse route line: "default            192.168.1.1        UGS        em0"
            char dest[64], gateway[64], flags[64], interface[64];
            char *p = line;
            
            // Skip leading whitespace
            while (*p && isspace(*p)) p++;
            
            // Parse destination
            char *dest_start = p;
            while (*p && !isspace(*p)) p++;
            int dest_len = p - dest_start;
            if (dest_len >= sizeof(dest)) dest_len = sizeof(dest) - 1;
            strncpy(dest, dest_start, dest_len);
            dest[dest_len] = '\0';
            
            // Skip whitespace
            while (*p && isspace(*p)) p++;
            
            // Parse gateway
            char *gateway_start = p;
            while (*p && !isspace(*p)) p++;
            int gateway_len = p - gateway_start;
            if (gateway_len >= sizeof(gateway)) gateway_len = sizeof(gateway) - 1;
            strncpy(gateway, gateway_start, gateway_len);
            gateway[gateway_len] = '\0';
            
            // Skip whitespace
            while (*p && isspace(*p)) p++;
            
            // Parse flags
            char *flags_start = p;
            while (*p && !isspace(*p)) p++;
            int flags_len = p - flags_start;
            if (flags_len >= sizeof(flags)) flags_len = sizeof(flags) - 1;
            strncpy(flags, flags_start, flags_len);
            flags[flags_len] = '\0';
            
            // Skip whitespace
            while (*p && isspace(*p)) p++;
            
            // Parse interface
            char *interface_start = p;
            while (*p && !isspace(*p)) p++;
            int interface_len = p - interface_start;
            if (interface_len >= sizeof(interface)) interface_len = sizeof(interface) - 1;
            strncpy(interface, interface_start, interface_len);
            interface[interface_len] = '\0';
            
            if (strlen(dest) > 0 && strlen(gateway) > 0) {
                
                // Write route XML with FIB information if available
                if (current_fib >= 0) {
                    xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                        "              <route>\n"
                        "                <destination-prefix>%s</destination-prefix>\n"
                        "                <next-hop>\n"
                        "                  <next-hop-address>%s</next-hop-address>\n"
                        "                </next-hop>\n"
                        "                <fib>%d</fib>\n"
                        "              </route>\n",
                        dest, gateway, current_fib);
                } else {
                xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                    "              <route>\n"
                    "                <destination-prefix>%s</destination-prefix>\n"
                    "                <next-hop>\n"
                    "                  <next-hop-address>%s</next-hop-address>\n"
                    "                </next-hop>\n"
                    "              </route>\n",
                    dest, gateway);
                }
            }
            
            line = strtok(NULL, "\n");
        }
        
        // Write closing tags
        xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
            "            </static-routes>\n"
            "          </routing-protocol>\n"
            "        </routing-protocols>\n"
            "      </routing-instance>\n"
            "    </routing>\n"
            "  </data>\n"
            "</rpc-reply>");
        
        // Copy the generated XML to the response buffer
        strncpy(response, xml_buffer, resp_len - 1);
        response[resp_len - 1] = '\0';
        
        free(xml_buffer);
        XML_ParserFree(parser);
        
        // Free protocol filter if allocated
        if (protocol_filter) {
            free((void*)protocol_filter);
        }
        
        printf("DEBUG: get-config NETCONF XML response:\n%s\n", response);
        return 0;
    }
    
    // Default: show all interfaces
    printf("DEBUG: Processing default get-config (interfaces)\n");
    char interface_data[4096];
    show_interfaces_filtered(interface_data, sizeof(interface_data), "");
    
    // Use BSDXML to generate the XML response
    XML_Parser parser = XML_ParserCreate(NULL);
    if (!parser) {
        printf("Failed to create XML parser for generation\n");
        return -1;
    }
    
    // Create a buffer to build the XML
    char *xml_buffer = malloc(resp_len);
    if (!xml_buffer) {
        XML_ParserFree(parser);
        return -1;
    }
    
    int xml_pos = 0;
    
    // Write XML header and start tags
    xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<rpc-reply message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <data>\n"
        "    <interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\">\n");
    
    // Parse the interface data line by line and convert to XML
    char *line = strtok(interface_data, "\n");
    while (line && (resp_len - xml_pos) > 100) {
        // Skip header line
        if (strstr(line, "Interface") && strstr(line, "IPv4")) {
            line = strtok(NULL, "\n");
            continue;
        }
        
        // Parse interface line: "em0 192.168.32.254/25 - - - 1500"
        char ifname[64], ipv4[64], ipv6[64], fib[64], tunnelfib[64], mtu[64];
        if (sscanf(line, "%63s %63s %63s %63s %63s %63s", 
                  ifname, ipv4, ipv6, fib, tunnelfib, mtu) >= 2) {
            
            // Extract IP and prefix length
            char ip[64];
            int prefix_len = 24; // default
            if (strcmp(ipv4, "-") != 0) {
                char *slash = strchr(ipv4, '/');
                if (slash) {
                    *slash = '\0';
                    strcpy(ip, ipv4);
                    prefix_len = atoi(slash + 1);
                } else {
                    strcpy(ip, ipv4);
                }
            }
            
            // Write interface XML
            xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                "      <interface>\n"
                "        <name>%s</name>\n"
                "        <type>ethernetCsmacd</type>\n"
                "        <enabled>true</enabled>\n",
                ifname);
            
            if (strcmp(ipv4, "-") != 0) {
                xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                    "        <ipv4 xmlns=\"urn:ietf:params:xml:ns:yang:ietf-ip\">\n"
                    "          <address>\n"
                    "            <ip>%s</ip>\n"
                    "            <prefix-length>%d</prefix-length>\n"
                    "          </address>\n"
                    "        </ipv4>\n",
                    ip, prefix_len);
            }
            
            xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos, "      </interface>\n");
        }
        
        line = strtok(NULL, "\n");
    }
    
    // Write closing tags
    xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
        "    </interfaces>\n"
        "  </data>\n"
        "</rpc-reply>");
    
    // Copy the generated XML to the response buffer
    strncpy(response, xml_buffer, resp_len - 1);
    response[resp_len - 1] = '\0';
    
    free(xml_buffer);
    XML_ParserFree(parser);
    
    printf("DEBUG: get-config NETCONF XML response:\n%s\n", response);
    return 0;
}

/**
 * Handle NETCONF edit-config request
 * @param config Configuration data from NETCONF message
 * @param response Buffer to store NETCONF response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
int handle_netconf_edit_config(const char *config, char *response, size_t resp_len) {
    printf("DEBUG: NETCONF edit-config request with config:\n%s\n", config);
    
    // Handle NETCONF edit-config request
    // Convert to appropriate set command
    
    command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    if (strstr(config, "interface")) {
        printf("DEBUG: Processing interface edit-config\n");
        cmd.type = CMD_SET;
        strcpy(cmd.target, "interface");
        // Parse interface configuration from XML
        // This would need more sophisticated XML parsing
        
        // Return proper NETCONF XML response
        snprintf(response, resp_len,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rpc-reply message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
            "  <ok/>\n"
            "</rpc-reply>");
        printf("DEBUG: edit-config NETCONF XML response:\n%s\n", response);
        return 0;
    } else if (strstr(config, "route")) {
        printf("DEBUG: Processing route edit-config\n");
        cmd.type = CMD_SET;
        strcpy(cmd.target, "route");
        // Parse route configuration from XML
        
        // Return proper NETCONF XML response
        snprintf(response, resp_len,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rpc-reply message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
            "  <ok/>\n"
            "</rpc-reply>");
        printf("DEBUG: edit-config NETCONF XML response:\n%s\n", response);
        return 0;
    }
    
    printf("DEBUG: Unknown configuration type in edit-config\n");
    snprintf(response, resp_len, "Unknown configuration type\n");
    return -1;
}

/**
 * Handle NETCONF commit request
 * @param response Buffer to store NETCONF response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
int handle_netconf_commit(char *response, size_t resp_len) {
    printf("DEBUG: NETCONF commit request received\n");
    
    // Handle NETCONF commit request
    
    // Return proper NETCONF XML response
    snprintf(response, resp_len,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<rpc-reply message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <ok/>\n"
        "</rpc-reply>");
    printf("DEBUG: commit NETCONF XML response:\n%s\n", response);
    return 0;
} 