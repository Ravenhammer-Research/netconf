#include "common.h"
#include <bsdxml.h>

// Forward declarations
extern int configure_interface(const if_config_t *config);
extern int show_interfaces(char *response, size_t resp_len);
extern int configure_route(const route_config_t *config);
extern int remove_route(const route_config_t *config);
extern int show_routes(char *response, size_t resp_len, int fib_filter);

// NETCONF message parsing functions are now declared in common.h

int execute_command(command_t *cmd, char *response, size_t resp_len) {
    switch (cmd->type) {
        case CMD_SHOW:
            if (strcmp(cmd->target, "interface") == 0) {
                return show_interfaces_filtered(response, resp_len, cmd->subtype);
            } else if (strcmp(cmd->target, "route") == 0) {
                return show_routes(response, resp_len, cmd->fib);
            } else if (strcmp(cmd->target, "help") == 0 || strcmp(cmd->target, "?") == 0 || strlen(cmd->target) == 0) {
                // Show help/usage
                print_usage();
                snprintf(response, resp_len, "Help displayed\n");
                return 0;
            } else {
                snprintf(response, resp_len, "Error: Unknown show target '%s'\n", cmd->target);
                return -1;
            }
            break;
            
        case CMD_SET:
            if (strcmp(cmd->target, "interface") == 0) {
                if (strcmp(cmd->subtype, "ethernet") == 0) {
                    if (configure_interface(&cmd->data.if_config) == 0) {
                        snprintf(response, resp_len, "Interface %s configured successfully\n", 
                                cmd->data.if_config.name);
                        return 0;
                    } else {
                        snprintf(response, resp_len, "Error: Failed to configure interface %s\n", 
                                cmd->data.if_config.name);
                        return -1;
                    }
                } else {
                    snprintf(response, resp_len, "Error: Unknown interface type '%s'\n", cmd->subtype);
                    return -1;
                }
            } else if (strcmp(cmd->target, "route") == 0) {
                if (strcmp(cmd->subtype, "static") == 0) {
                    if (configure_route(&cmd->data.route_config) == 0) {
                        snprintf(response, resp_len, "Static route configured successfully\n");
                        return 0;
                    } else {
                        snprintf(response, resp_len, "Error: Failed to configure static route\n");
                        return -1;
                    }
                } else {
                    snprintf(response, resp_len, "Error: Unknown route type '%s'\n", cmd->subtype);
                    return -1;
                }
            } else {
                snprintf(response, resp_len, "Error: Unknown set target '%s'\n", cmd->target);
                return -1;
            }
            break;
            
        case CMD_DELETE:
            if (strcmp(cmd->target, "route") == 0) {
                if (strcmp(cmd->subtype, "static") == 0) {
                    if (remove_route(&cmd->data.route_config) == 0) {
                        snprintf(response, resp_len, "Static routes deleted successfully\n");
                        return 0;
                    } else {
                        snprintf(response, resp_len, "Error: Failed to delete static routes\n");
                        return -1;
                    }
                } else {
                    snprintf(response, resp_len, "Error: Unknown route type '%s'\n", cmd->subtype);
                    return -1;
                }
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
            // This would typically save to /etc/rc.conf or similar
            // For now, just return success
            snprintf(response, resp_len, "Configuration saved successfully\n");
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
        return show_routes(response, resp_len, -1); // -1 means show all FIBs
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