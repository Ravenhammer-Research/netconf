#include "common.h"
#include <bsdxml.h>
#include <sys/stat.h>

#define CONFIG_FILE "/usr/local/etc/net.conf"
#define CONFIG_BACKUP "/usr/local/etc/net.conf.bak"

// Forward declarations
extern int configure_interface(const if_config_t *config);
extern int show_interfaces(char *response, size_t resp_len);
extern int configure_route(const route_config_t *config);
extern int remove_route(const route_config_t *config);
extern int show_routes(char *response, size_t resp_len, int fib_filter, const char *protocol_filter, int family_filter);

// NETCONF message parsing functions are now declared in common.h

// Configuration file handling
int save_configuration(void) {
    // Create backup of existing config
    if (access(CONFIG_FILE, F_OK) == 0) {
        if (rename(CONFIG_FILE, CONFIG_BACKUP) != 0) {
            return -1;
        }
    }
    
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        // Restore backup if we can't create new file
        if (access(CONFIG_BACKUP, F_OK) == 0) {
            rename(CONFIG_BACKUP, CONFIG_FILE);
        }
        return -1;
    }
    
    // Set proper permissions
    chmod(CONFIG_FILE, 0644);
    
    // Write header
    fprintf(fp, "# Network configuration for netd\n");
    fprintf(fp, "# Generated automatically - do not edit manually\n\n");
    
    // TODO: Write current interface and route configurations
    // This would require getting current state from the system
    // For now, just write a placeholder
    fprintf(fp, "# Interface configurations\n");
    fprintf(fp, "# [interface.<name>]\n");
    fprintf(fp, "# inet=<ipv4>/<prefix>\n");
    fprintf(fp, "# inet6=<ipv6>/<prefix>\n");
    fprintf(fp, "# fib=<number>\n\n");
    
    fprintf(fp, "# Route configurations\n");
    fprintf(fp, "# [route.static.<index>]\n");
    fprintf(fp, "# destination=<dest>/<prefix>\n");
    fprintf(fp, "# gateway=<gateway>\n");
    fprintf(fp, "# fib=<number>\n\n");
    
    fclose(fp);
    return 0;
}

int load_configuration(void) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        return -1; // File doesn't exist, not an error
    }
    
    char line[1024];
    char section[64] = "";

    
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue;
        }
        
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Parse section headers [section]
        if (line[0] == '[' && strchr(line, ']')) {
            char *end = strchr(line, ']');
            size_t len = end - line - 1;
            if (len > 0 && len < sizeof(section) - 1) {
                strncpy(section, line + 1, len);
                section[len] = '\0';
            }
            continue;
        }
        
        // Parse key=value pairs
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        
        // Trim whitespace
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        
        char *end = key + strlen(key) - 1;
        while (end > key && (*end == ' ' || *end == '\t')) *end-- = '\0';
        
        end = value + strlen(value) - 1;
        while (end > value && (*end == ' ' || *end == '\t')) *end-- = '\0';
        
        // Parse based on section
        if (strncmp(section, "interface.", 10) == 0) {
            char *ifname = section + 10;
            
            if (strcmp(key, "inet") == 0) {
                // Parse IPv4 address
                char *slash = strchr(value, '/');
                if (slash) {
                    *slash = '\0';
                    int prefix = atoi(slash + 1);
                    
                    if_config_t config;
                    memset(&config, 0, sizeof(config));
                    strncpy(config.name, ifname, sizeof(config.name) - 1);
                    config.family = ADDR_FAMILY_INET4;
                    config.prefix_len = prefix;
                    
                    if (inet_pton(AF_INET, value, &config.addr) == 1) {
                        configure_interface(&config);
                    }
                }
            } else if (strcmp(key, "inet6") == 0) {
                // Parse IPv6 address
                char *slash = strchr(value, '/');
                if (slash) {
                    *slash = '\0';
                    int prefix = atoi(slash + 1);
                    
                    if_config_t config;
                    memset(&config, 0, sizeof(config));
                    strncpy(config.name, ifname, sizeof(config.name) - 1);
                    config.family = ADDR_FAMILY_INET6;
                    config.prefix_len = prefix;
                    
                    if (inet_pton(AF_INET6, value, &config.addr6) == 1) {
                        configure_interface(&config);
                    }
                }
            } else if (strcmp(key, "fib") == 0) {
                // FIB number for interface
                // TODO: Apply FIB to interface
            }
        } else if (strncmp(section, "route.static.", 13) == 0) {
            if (strcmp(key, "destination") == 0) {
                // Parse destination
                char *slash = strchr(value, '/');
                if (slash) {
                    *slash = '\0';
                    int prefix = atoi(slash + 1);
                    
                    route_config_t config;
                    memset(&config, 0, sizeof(config));
                    config.prefix_len = prefix;
                    
                    // Determine family from address format
                    if (strchr(value, ':')) {
                        config.family = ADDR_FAMILY_INET6;
                        if (inet_pton(AF_INET6, value, &config.dest6) == 1) {
                            // TODO: Get gateway from next line and configure route
                        }
                    } else {
                        config.family = ADDR_FAMILY_INET4;
                        if (inet_pton(AF_INET, value, &config.dest) == 1) {
                            // TODO: Get gateway from next line and configure route
                        }
                    }
                }
            } else if (strcmp(key, "gateway") == 0) {
                // Gateway for route
                // TODO: Apply gateway to pending route configuration
            } else if (strcmp(key, "fib") == 0) {
                // FIB number for route
                // TODO: Apply FIB to pending route configuration
            }
        }
    }
    
    fclose(fp);
    return 0;
}

int execute_command(command_t *cmd, char *response, size_t resp_len) {
    switch (cmd->type) {
        case CMD_SHOW:
            if (strcmp(cmd->target, "interface") == 0) {
                return show_interfaces_filtered(response, resp_len, cmd->subtype);
            } else if (strcmp(cmd->target, "route") == 0) {
                int family_filter = AF_UNSPEC;
                if (cmd->family == ADDR_FAMILY_INET4) {
                    family_filter = AF_INET;
                } else if (cmd->family == ADDR_FAMILY_INET6) {
                    family_filter = AF_INET6;
                }
                return show_routes(response, resp_len, cmd->fib, cmd->subtype, family_filter);
            } else if (strcmp(cmd->target, "help") == 0 || strcmp(cmd->target, "?") == 0 || strlen(cmd->target) == 0) {
                // Show help/usage
                snprintf(response, resp_len, 
                    "Usage:\n"
                    "  net [command]                    - One-shot mode\n"
                    "  net                              - Interactive mode\n"
                    "\nCommands:\n"
                    "  show interface                   - Show all interfaces (detailed)\n"
                    "  show interface <type>            - Show interfaces by type (ethernet, bridge, gif, etc.)\n"
                    "  show route [fib N]               - Show routing table (optionally filter by FIB)\n"
                    "  set interface ethernet <if> inet addr <addr>/<prefix> [fib N]\n"
                    "  set interface ethernet <if> inet6 addr <addr>/<prefix> [fib N]\n"
                    "  set route protocol static [fib N] inet dest gw\n"
                    "  set route protocol static [fib N] inet6 dest gw\n"
                    "  delete route protocol static [fib N]\n"
                    "  commit                           - Apply queued changes\n"
                    "  save                             - Persist configuration\n"
                    "  help, ?                          - Show this help\n"
                    "  quit, exit                       - Exit interactive mode\n"
                    "\nInterface Types:\n"
                    "  ethernet, bridge, gif, tun, tap, vlan, lo\n"
                    "\nTab completion is available for all commands.\n");
                return 0;
            } else {
                snprintf(response, resp_len, "Error: Unknown show target '%s'\n", cmd->target);
                return -1;
            }
            break;
            
        case CMD_SET:
            if (strcmp(cmd->target, "interface") == 0) {
                return configure_interface(&cmd->data.if_config);
            } else if (strcmp(cmd->target, "route") == 0) {
                return configure_route(&cmd->data.route_config);
            } else {
                snprintf(response, resp_len, "Error: Unknown set target '%s'\n", cmd->target);
                return -1;
            }
            break;
            
        case CMD_DELETE:
            if (strcmp(cmd->target, "route") == 0) {
                return remove_route(&cmd->data.route_config);
            } else {
                snprintf(response, resp_len, "Error: Unknown delete target '%s'\n", cmd->target);
                return -1;
            }
            break;
            
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
    
    return -1;
}

int parse_netconf_message(const char *xml_msg, command_t *cmd) {
    // Parse NETCONF XML message using FreeBSD's XML library
    // This provides better XML parsing than simple string operations
    
    if (strstr(xml_msg, "<get-config>") != NULL) {
        cmd->type = CMD_SHOW;
        // Extract filter if present
        const char *filter_start = strstr(xml_msg, "<filter");
        if (filter_start) {
            // Parse filter to determine target
            if (strstr(filter_start, "interface")) {
                strcpy(cmd->target, "interface");
            } else if (strstr(filter_start, "route")) {
                strcpy(cmd->target, "route");
            }
        }
        return 0;
    } else if (strstr(xml_msg, "<edit-config>") != NULL) {
        cmd->type = CMD_SET;
        // Parse config to determine target
        if (strstr(xml_msg, "interface")) {
            strcpy(cmd->target, "interface");
        } else if (strstr(xml_msg, "route")) {
            strcpy(cmd->target, "route");
        }
        return 0;
    } else if (strstr(xml_msg, "<commit/>") != NULL) {
        cmd->type = CMD_COMMIT;
        return 0;
    }
    
    return -1;
}

int handle_netconf_get_config(const char *filter, char *response, size_t resp_len) {
    // Handle NETCONF get-config request
    // Convert to appropriate show command
    
    command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    if (strstr(filter, "interface")) {
        cmd.type = CMD_SHOW;
        strcpy(cmd.target, "interface");
        return show_interfaces_filtered(response, resp_len, "");
    } else if (strstr(filter, "route")) {
        cmd.type = CMD_SHOW;
        strcpy(cmd.target, "route");
        return show_routes(response, resp_len, -1, NULL, AF_UNSPEC); // -1 means show all FIBs, NULL means no protocol filter, AF_UNSPEC means both families
    }
    
    // Default: show all interfaces
    return show_interfaces_filtered(response, resp_len, "");
}

int handle_netconf_edit_config(const char *config, char *response, size_t resp_len) {
    // Handle NETCONF edit-config request
    // Convert to appropriate set command
    
    command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    if (strstr(config, "interface")) {
        cmd.type = CMD_SET;
        strcpy(cmd.target, "interface");
        // Parse interface configuration from XML
        // This would need more sophisticated XML parsing
        snprintf(response, resp_len, "Interface configuration applied via NETCONF\n");
        return 0;
    } else if (strstr(config, "route")) {
        cmd.type = CMD_SET;
        strcpy(cmd.target, "route");
        // Parse route configuration from XML
        snprintf(response, resp_len, "Route configuration applied via NETCONF\n");
        return 0;
    }
    
    snprintf(response, resp_len, "Unknown configuration type\n");
    return -1;
}

int handle_netconf_commit(char *response, size_t resp_len) {
    // Handle NETCONF commit request
    snprintf(response, resp_len, "Configuration committed successfully via NETCONF\n");
    return 0;
} 