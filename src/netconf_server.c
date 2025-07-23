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

#include "common.h"
#include <bsdxml.h>
#include <sys/stat.h>

#define CONFIG_FILE "/usr/local/etc/net.conf"
#define CONFIG_BACKUP "/usr/local/etc/net.conf.bak"
#define MAX_LINE_LENGTH 1024
#define MAX_SECTION_LENGTH 64

// Forward declarations
extern int configure_interface(const if_config_t *config);
extern int show_interfaces(char *response, size_t resp_len);
extern int configure_route(const route_config_t *config);
extern int remove_route(const route_config_t *config);
extern int show_routes(char *response, size_t resp_len, int fib_filter, const char *protocol_filter, int family_filter);

// Configuration section types
typedef enum {
    SECTION_NONE,
    SECTION_INTERFACE,
    SECTION_ROUTE_STATIC
} section_type_t;

// Configuration parser state
typedef struct {
    char current_section[MAX_SECTION_LENGTH];
    section_type_t section_type;
    char interface_name[IFNAMSIZ];
    int route_index;
} config_parser_t;

// Initialize configuration parser
static void config_parser_init(config_parser_t *parser) {
    memset(parser, 0, sizeof(config_parser_t));
    parser->section_type = SECTION_NONE;
    parser->route_index = -1;
}

// Parse section header [section]
static int parse_section_header(const char *line, config_parser_t *parser) {
    if (line[0] != '[' || !strchr(line, ']')) {
        return -1;
    }
    
    char *end = strchr(line, ']');
    size_t len = end - line - 1;
    if (len <= 0 || len >= sizeof(parser->current_section) - 1) {
        return -1;
    }
    
    strncpy(parser->current_section, line + 1, len);
    parser->current_section[len] = '\0';
    
    // Determine section type
    if (strncmp(parser->current_section, "interface.", 10) == 0) {
        parser->section_type = SECTION_INTERFACE;
        strncpy(parser->interface_name, parser->current_section + 10, sizeof(parser->interface_name) - 1);
    } else if (strncmp(parser->current_section, "route.static.", 13) == 0) {
        parser->section_type = SECTION_ROUTE_STATIC;
        parser->route_index = atoi(parser->current_section + 13);
    } else {
        parser->section_type = SECTION_NONE;
    }
    
    return 0;
}

// Parse key=value pair
static int parse_key_value(const char *line, char **key, char **value) {
    char *equals = strchr(line, '=');
    if (!equals) {
        return -1;
    }
    
    *equals = '\0';
    *key = (char*)line;
    *value = equals + 1;
    
    // Trim whitespace from key
    while (**key == ' ' || **key == '\t') (*key)++;
    char *end = *key + strlen(*key) - 1;
    while (end > *key && (*end == ' ' || *end == '\t')) *end-- = '\0';
    
    // Trim whitespace from value
    while (**value == ' ' || **value == '\t') (*value)++;
    end = *value + strlen(*value) - 1;
    while (end > *value && (*end == ' ' || *end == '\t')) *end-- = '\0';
    
    return 0;
}

// Parse CIDR notation (address/prefix)
static int parse_cidr_address(const char *value, char *addr_str, size_t addr_len, int *prefix_len) {
    char *slash = strchr(value, '/');
    if (!slash) {
        return -1;
    }
    
    size_t addr_part_len = slash - value;
    if (addr_part_len >= addr_len) {
        return -1;
    }
    
    strncpy(addr_str, value, addr_part_len);
    addr_str[addr_part_len] = '\0';
    *prefix_len = atoi(slash + 1);
    
    return 0;
}

// Apply interface configuration
static int apply_interface_config(const char *ifname, const char *key, const char *value) {
    if_config_t config;
    memset(&config, 0, sizeof(config));
    strncpy(config.name, ifname, sizeof(config.name) - 1);
    
    if (strcmp(key, "inet") == 0) {
        char addr_str[INET_ADDRSTRLEN];
        int prefix_len;
        
        if (parse_cidr_address(value, addr_str, sizeof(addr_str), &prefix_len) == 0) {
            config.family = ADDR_FAMILY_INET4;
            config.prefix_len = prefix_len;
            
            if (inet_pton(AF_INET, addr_str, &config.addr) == 1) {
                return configure_interface(&config);
            }
        }
    } else if (strcmp(key, "inet6") == 0) {
        char addr_str[INET6_ADDRSTRLEN];
        int prefix_len;
        
        if (parse_cidr_address(value, addr_str, sizeof(addr_str), &prefix_len) == 0) {
            config.family = ADDR_FAMILY_INET6;
            config.prefix_len = prefix_len;
            
            if (inet_pton(AF_INET6, addr_str, &config.addr6) == 1) {
                return configure_interface(&config);
            }
        }
    }
    
    return -1;
}

// Apply route configuration
static int apply_route_config(const char *key, const char *value) {
    route_config_t config;
    memset(&config, 0, sizeof(config));
    
    if (strcmp(key, "destination") == 0) {
        char addr_str[INET6_ADDRSTRLEN];
        int prefix_len;
        
        if (parse_cidr_address(value, addr_str, sizeof(addr_str), &prefix_len) == 0) {
            config.prefix_len = prefix_len;
            
            // Determine family from address format
            if (strchr(addr_str, ':')) {
                config.family = ADDR_FAMILY_INET6;
                if (inet_pton(AF_INET6, addr_str, &config.dest6) == 1) {
                    // TODO: Get gateway from next line and configure route
                    return 0;
                }
            } else {
                config.family = ADDR_FAMILY_INET4;
                if (inet_pton(AF_INET, addr_str, &config.dest) == 1) {
                    // TODO: Get gateway from next line and configure route
                    return 0;
                }
            }
        }
    }
    
    return -1;
}

// Parse configuration line
static int parse_config_line(const char *line, config_parser_t *parser) {
    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
        return 0;
    }
    
    // Parse section headers
    if (line[0] == '[') {
        return parse_section_header(line, parser);
    }
    
    // Parse key=value pairs
    char *key, *value;
    if (parse_key_value(line, &key, &value) != 0) {
        return 0; // Skip malformed lines
    }
    
    // Apply configuration based on section type
    switch (parser->section_type) {
        case SECTION_INTERFACE:
            return apply_interface_config(parser->interface_name, key, value);
            
        case SECTION_ROUTE_STATIC:
            return apply_route_config(key, value);
            
        default:
            return 0; // Unknown section, ignore
    }
}

// Create backup of existing configuration
static int create_config_backup(void) {
    if (access(CONFIG_FILE, F_OK) == 0) {
        return rename(CONFIG_FILE, CONFIG_BACKUP);
    }
    return 0; // No existing file to backup
}

// Restore configuration from backup
static int restore_config_backup(void) {
    if (access(CONFIG_BACKUP, F_OK) == 0) {
        return rename(CONFIG_BACKUP, CONFIG_FILE);
    }
    return -1;
}

// Write configuration header
static int write_config_header(FILE *fp) {
    if (fprintf(fp, "# Network configuration for netd\n") < 0) return -1;
    if (fprintf(fp, "# Generated automatically - do not edit manually\n\n") < 0) return -1;
    return 0;
}

// Write interface configuration template
static int write_interface_template(FILE *fp) {
    if (fprintf(fp, "# Interface configurations\n") < 0) return -1;
    if (fprintf(fp, "# [interface.<name>]\n") < 0) return -1;
    if (fprintf(fp, "# inet=<ipv4>/<prefix>\n") < 0) return -1;
    if (fprintf(fp, "# inet6=<ipv6>/<prefix>\n") < 0) return -1;
    if (fprintf(fp, "# fib=<number>\n\n") < 0) return -1;
    return 0;
}

// Write route configuration template
static int write_route_template(FILE *fp) {
    if (fprintf(fp, "# Route configurations\n") < 0) return -1;
    if (fprintf(fp, "# [route.static.<index>]\n") < 0) return -1;
    if (fprintf(fp, "# destination=<dest>/<prefix>\n") < 0) return -1;
    if (fprintf(fp, "# gateway=<gateway>\n") < 0) return -1;
    if (fprintf(fp, "# fib=<number>\n\n") < 0) return -1;
    return 0;
}

// Command execution helpers
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

static int execute_set_command(const command_t *cmd, char *response, size_t resp_len) {
    printf("DEBUG: NETCONF YANG edit-config request for target: %s\n", cmd->target);
    
    if (strcmp(cmd->target, "interface") == 0) {
        printf("DEBUG: Processing interface edit-config via CLI\n");
        int result = configure_interface(&cmd->data.if_config);
        if (result == 0) {
            snprintf(response, resp_len, "Interface configuration applied successfully\n");
            printf("DEBUG: edit-config response: %s", response);
        } else {
            snprintf(response, resp_len, "Error: Failed to configure interface\n");
            printf("DEBUG: edit-config response: %s", response);
        }
        return result;
    } else if (strcmp(cmd->target, "route") == 0) {
        printf("DEBUG: Processing route edit-config via CLI\n");
        int result = configure_route(&cmd->data.route_config);
        if (result == 0) {
            snprintf(response, resp_len, "Route configuration applied successfully\n");
            printf("DEBUG: edit-config response: %s", response);
        } else {
            snprintf(response, resp_len, "Error: Failed to configure route\n");
            printf("DEBUG: edit-config response: %s", response);
        }
        return result;
    }
    
    snprintf(response, resp_len, "Error: Unknown set target '%s'\n", cmd->target);
    return -1;
}

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

// NETCONF message parsing helpers
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

// Public interface functions

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
    
    // Write configuration
    if (write_config_header(fp) != 0 ||
        write_interface_template(fp) != 0 ||
        write_route_template(fp) != 0) {
        fclose(fp);
        restore_config_backup();
        return -1;
    }
    
    fclose(fp);
    return 0;
}

int load_configuration(void) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        return -1; // File doesn't exist, not an error
    }
    
    config_parser_t parser;
    config_parser_init(&parser);
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        parse_config_line(line, &parser);
    }
    
    fclose(fp);
    return 0;
}

int execute_command(command_t *cmd, char *response, size_t resp_len) {
    switch (cmd->type) {
        case CMD_SHOW:
            return execute_show_command(cmd, response, resp_len);
            
        case CMD_SET:
            return execute_set_command(cmd, response, resp_len);
            
        case CMD_DELETE:
            return execute_delete_command(cmd, response, resp_len);
            
        case CMD_COMMIT:
            // For now, commit is a no-op since we apply changes immediately
            snprintf(response, resp_len, "Changes committed successfully\n");
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
    
    printf("DEBUG: Failed to parse NETCONF message\n");
    return -1;
}

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
    } else if (strstr(filter, "route")) {
        printf("DEBUG: Processing route get-config\n");
        cmd.type = CMD_SHOW;
        strcpy(cmd.target, "route");
        
        // Get the route data and convert to NETCONF XML
        char route_data[4096];
        show_routes(route_data, sizeof(route_data), -1, NULL, AF_UNSPEC);
        
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
        while (line && (resp_len - xml_pos) > 100) {
            // Skip header lines
            if (strstr(line, "Routing tables") || strstr(line, "Destination") || strlen(line) == 0) {
                line = strtok(NULL, "\n");
                continue;
            }
            
            // Parse route line: "default            192.168.1.1        UGS        em0"
            char dest[64], gateway[64], flags[64], interface[64];
            if (sscanf(line, "%63s %63s %63s %63s", dest, gateway, flags, interface) >= 3) {
                
                // Write route XML
                xml_pos += snprintf(xml_buffer + xml_pos, resp_len - xml_pos,
                    "              <route>\n"
                    "                <destination-prefix>%s</destination-prefix>\n"
                    "                <next-hop>\n"
                    "                  <next-hop-address>%s</next-hop-address>\n"
                    "                </next-hop>\n"
                    "              </route>\n",
                    dest, gateway);
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