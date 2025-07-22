#include "common.h"

static cmd_type_t parse_cmd_type(const char *token) {
    if (strcmp(token, "show") == 0) return CMD_SHOW;
    if (strcmp(token, "set") == 0) return CMD_SET;
    if (strcmp(token, "delete") == 0) return CMD_DELETE;
    if (strcmp(token, "commit") == 0) return CMD_COMMIT;
    if (strcmp(token, "save") == 0) return CMD_SAVE;
    if (strcmp(token, "help") == 0 || strcmp(token, "?") == 0) return CMD_SHOW; // Treat help as show
    return CMD_UNKNOWN;
}

static addr_family_t parse_family(const char *token) {
    if (strcmp(token, "inet") == 0) return ADDR_FAMILY_INET4;
    if (strcmp(token, "inet6") == 0) return ADDR_FAMILY_INET6;
    return ADDR_FAMILY_INET4; // Default
}

static int parse_address(const char *addr_str, addr_family_t family, 
                        struct in_addr *addr, struct in6_addr *addr6) {
    if (family == ADDR_FAMILY_INET4) {
        return inet_pton(AF_INET, addr_str, addr);
    } else {
        return inet_pton(AF_INET6, addr_str, addr6);
    }
}

static int parse_cidr(const char *cidr_str, int *prefix_len) {
    char *slash = strchr(cidr_str, '/');
    if (!slash) {
        *prefix_len = (slash - cidr_str == 4) ? 32 : 128; // Default to /32 for IPv4, /128 for IPv6
        return 0;
    }
    
    *slash = '\0';
    *prefix_len = atoi(slash + 1);
    return 0;
}

int parse_command(const char *cmd_line, command_t *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    
    char *line_copy = strdup(cmd_line);
    if (!line_copy) return -1;
    
    char *token = strtok(line_copy, " \t");
    if (!token) {
        free(line_copy);
        return -1;
    }
    
    cmd->type = parse_cmd_type(token);
    if (cmd->type == CMD_UNKNOWN) {
        free(line_copy);
        return -1;
    }
    
    // Handle simple commands
    if (cmd->type == CMD_COMMIT || cmd->type == CMD_SAVE) {
        free(line_copy);
        return 0;
    }
    
    // Parse target (interface, route, etc.)
    token = strtok(NULL, " \t");
    if (!token) {
        // For show commands, this is OK (e.g., "show" without target)
        if (cmd->type == CMD_SHOW) {
            free(line_copy);
            return 0;
        }
        free(line_copy);
        return -1;
    }
    strncpy(cmd->target, token, sizeof(cmd->target) - 1);
    
    // Handle show commands - they can be simple like "show interface"
    if (cmd->type == CMD_SHOW) {
        // Check if there's a subtype (ethernet, bridge, gif, etc.)
        token = strtok(NULL, " \t");
        if (token) {
            strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
        }
        free(line_copy);
        return 0;
    }
    
    if (strcmp(cmd->target, "interface") == 0) {
        // Parse interface command
        token = strtok(NULL, " \t");
        if (!token) {
            free(line_copy);
            return -1;
        }
        strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
        
        token = strtok(NULL, " \t");
        if (!token) {
            free(line_copy);
            return -1;
        }
        strncpy(cmd->name, token, sizeof(cmd->name) - 1);
        
        if (cmd->type == CMD_SET) {
            token = strtok(NULL, " \t");
            if (!token) {
                free(line_copy);
                return -1;
            }
            
            if (strcmp(token, "inet") == 0 || strcmp(token, "inet6") == 0) {
                cmd->family = parse_family(token);
                
                token = strtok(NULL, " \t");
                if (!token) {
                    free(line_copy);
                    return -1;
                }
                
                if (strcmp(token, "addr") == 0) {
                    token = strtok(NULL, " \t");
                    if (!token) {
                        free(line_copy);
                        return -1;
                    }
                    
                    char addr_str[INET6_ADDRSTRLEN];
                    strncpy(addr_str, token, sizeof(addr_str) - 1);
                    
                    if (parse_cidr(addr_str, &cmd->data.if_config.prefix_len) < 0) {
                        free(line_copy);
                        return -1;
                    }
                    
                    if (parse_address(addr_str, cmd->family, 
                                    &cmd->data.if_config.addr, 
                                    &cmd->data.if_config.addr6) <= 0) {
                        free(line_copy);
                        return -1;
                    }
                    
                    // Check for optional FIB
                    token = strtok(NULL, " \t");
                    if (token && strcmp(token, "fib") == 0) {
                        token = strtok(NULL, " \t");
                        if (token) {
                            cmd->fib = atoi(token);
                        }
                    }
                }
            }
        }
    } else if (strcmp(cmd->target, "route") == 0) {
        // Parse route command
        token = strtok(NULL, " \t");
        if (!token) {
            free(line_copy);
            return -1;
        }
        
        // Handle "protocol static" combination
        if (strcmp(token, "protocol") == 0 || strcmp(token, "protocols") == 0) {
            token = strtok(NULL, " \t");
            if (!token) {
                free(line_copy);
                return -1;
            }
            strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
            // Continue parsing for additional parameters
            token = strtok(NULL, " \t");
        } else {
            strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
            // Continue parsing for additional parameters
            token = strtok(NULL, " \t");
        }
        
        // Handle show commands for routes
        if (cmd->type == CMD_SHOW) {
            // Parse: show route [protocol static] [fib N]
            // The token is already parsed from above, so check for FIB
            printf("DEBUG: After protocol static, token = '%s'\n", token ? token : "NULL");
            
            // Check for FIB parameter
            if (token && strcmp(token, "fib") == 0) {
                token = strtok(NULL, " \t");
                printf("DEBUG: Found 'fib', next token = '%s'\n", token ? token : "NULL");
                if (token) {
                    cmd->fib = atoi(token);
                    printf("DEBUG: Parsed FIB number: %d\n", cmd->fib);
                }
            }
            free(line_copy);
            return 0;
        }
        
        if (cmd->type == CMD_SET) {
            // Check for optional FIB
            token = strtok(NULL, " \t");
            if (token && strcmp(token, "fib") == 0) {
                token = strtok(NULL, " \t");
                if (token) {
                    cmd->fib = atoi(token);
                }
                token = strtok(NULL, " \t");
            }
            
            if (!token) {
                free(line_copy);
                return -1;
            }
            
            cmd->family = parse_family(token);
            
            // Parse destination
            token = strtok(NULL, " \t");
            if (!token) {
                free(line_copy);
                return -1;
            }
            
            char dest_str[INET6_ADDRSTRLEN];
            strncpy(dest_str, token, sizeof(dest_str) - 1);
            
            if (parse_cidr(dest_str, &cmd->data.route_config.prefix_len) < 0) {
                free(line_copy);
                return -1;
            }
            
            if (parse_address(dest_str, cmd->family,
                            &cmd->data.route_config.dest,
                            &cmd->data.route_config.dest6) <= 0) {
                free(line_copy);
                return -1;
            }
            
            // Parse gateway
            token = strtok(NULL, " \t");
            if (!token) {
                free(line_copy);
                return -1;
            }
            
            if (parse_address(token, cmd->family,
                            &cmd->data.route_config.gw,
                            &cmd->data.route_config.gw6) <= 0) {
                free(line_copy);
                return -1;
            }
        }
    }
    
    free(line_copy);
    return 0;
}

void print_usage(void) {
    printf("Usage:\n");
    printf("  net [command]                    - One-shot mode\n");
    printf("  net                              - Interactive mode\n");
    printf("\nCommands:\n");
    printf("  show interface                   - Show all interfaces (detailed)\n");
    printf("  show interface <type>            - Show interfaces by type (ethernet, bridge, gif, etc.)\n");
    printf("  show route [fib N]               - Show routing table (optionally filter by FIB)\n");
    printf("  set interface ethernet <if> inet addr <addr>/<prefix> [fib N]\n");
    printf("  set interface ethernet <if> inet6 addr <addr>/<prefix> [fib N]\n");
    printf("  set route protocol static [fib N] inet dest gw\n");
    printf("  set route protocol static [fib N] inet6 dest gw\n");
    printf("  delete route protocol static [fib N]\n");
    printf("  commit                           - Apply queued changes\n");
    printf("  save                             - Persist configuration\n");
    printf("  help, ?                          - Show this help\n");
    printf("  quit, exit                       - Exit interactive mode\n");
    printf("\nInterface Types:\n");
    printf("  ethernet, bridge, gif, tun, tap, vlan, lo\n");
    printf("\nTab completion is available for all commands.\n");
} 