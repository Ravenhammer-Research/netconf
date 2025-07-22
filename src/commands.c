#include "common.h"

// Simple lexer for command parsing
typedef struct {
    char *tokens[32];  // Max 32 tokens
    int count;
    int pos;
} lexer_t;

static void lexer_init(lexer_t *lexer, const char *input) {
    lexer->count = 0;
    lexer->pos = 0;
    
    char *input_copy = strdup(input);
    char *token = strtok(input_copy, " \t");
    
    while (token && lexer->count < 32) {
        lexer->tokens[lexer->count++] = strdup(token);
        token = strtok(NULL, " \t");
    }
    
    free(input_copy);
}

static void lexer_cleanup(lexer_t *lexer) {
    for (int i = 0; i < lexer->count; i++) {
        free(lexer->tokens[i]);
    }
}

static const char* lexer_peek(lexer_t *lexer) {
    return (lexer->pos < lexer->count) ? lexer->tokens[lexer->pos] : NULL;
}

static const char* lexer_next(lexer_t *lexer) {
    return (lexer->pos < lexer->count) ? lexer->tokens[lexer->pos++] : NULL;
}

static int lexer_has_more(lexer_t *lexer) {
    return lexer->pos < lexer->count;
}

static cmd_type_t parse_cmd_type(const char *token) {
    if (strcmp(token, "show") == 0) return CMD_SHOW;
    if (strcmp(token, "set") == 0) return CMD_SET;
    if (strcmp(token, "delete") == 0) return CMD_DELETE;
    if (strcmp(token, "commit") == 0) return CMD_COMMIT;
    if (strcmp(token, "save") == 0) return CMD_SAVE;
    if (strcmp(token, "help") == 0 || strcmp(token, "?") == 0) return CMD_SHOW;
    return CMD_UNKNOWN;
}

static addr_family_t parse_family(const char *token) {
    if (strcmp(token, "inet") == 0) return ADDR_FAMILY_INET4;
    if (strcmp(token, "inet6") == 0) return ADDR_FAMILY_INET6;
    return ADDR_FAMILY_INET4;
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
        *prefix_len = (strlen(cidr_str) == 4) ? 32 : 128;
        return 0;
    }
    
    *slash = '\0';
    *prefix_len = atoi(slash + 1);
    return 0;
}

int parse_command(const char *cmd_line, command_t *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    
    lexer_t lexer;
    lexer_init(&lexer, cmd_line);
    
    if (!lexer_has_more(&lexer)) {
        lexer_cleanup(&lexer);
        return -1;
    }
    
    // Parse command type
    const char *token = lexer_next(&lexer);
    cmd->type = parse_cmd_type(token);
    if (cmd->type == CMD_UNKNOWN) {
        lexer_cleanup(&lexer);
        return -1;
    }
    
    // Handle simple commands
    if (cmd->type == CMD_COMMIT || cmd->type == CMD_SAVE) {
        lexer_cleanup(&lexer);
        return 0;
    }
    
    // Parse target
    if (!lexer_has_more(&lexer)) {
        if (cmd->type == CMD_SHOW) {
            lexer_cleanup(&lexer);
            return 0;
        }
        lexer_cleanup(&lexer);
        return -1;
    }
    
    token = lexer_next(&lexer);
    strncpy(cmd->target, token, sizeof(cmd->target) - 1);
    
        // Handle show commands
    if (cmd->type == CMD_SHOW) {
        if (strcmp(cmd->target, "interface") == 0 && lexer_has_more(&lexer)) {
            token = lexer_next(&lexer);
            strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
        }
        lexer_cleanup(&lexer);
        return 0;
    }
    
    // Handle route commands
    if (strcmp(cmd->target, "route") == 0) {
        if (!lexer_has_more(&lexer)) {
            lexer_cleanup(&lexer);
            return -1;
        }
        
        // Handle show commands for routes
        if (cmd->type == CMD_SHOW) {
            // Parse: show route [fib N] [protocol static|dynamic] [inet|inet6]
            while (lexer_has_more(&lexer)) {
                token = lexer_peek(&lexer);
                
                if (strcmp(token, "fib") == 0) {
                    lexer_next(&lexer);
                    if (lexer_has_more(&lexer)) {
                        token = lexer_next(&lexer);
                    cmd->fib = atoi(token);
                    printf("DEBUG: Parsed FIB number: %d\n", cmd->fib);
                }
                } else if (strcmp(token, "protocol") == 0) {
                    lexer_next(&lexer);
                    if (lexer_has_more(&lexer)) {
                        token = lexer_next(&lexer);
                        strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
                        printf("DEBUG: Parsed protocol: %s\n", cmd->subtype);
                    }
                } else if (strcmp(token, "inet") == 0) {
                    lexer_next(&lexer);
                    cmd->family = ADDR_FAMILY_INET4;
                    printf("DEBUG: Parsed family: inet\n");
                } else if (strcmp(token, "inet6") == 0) {
                    lexer_next(&lexer);
                    cmd->family = ADDR_FAMILY_INET6;
                    printf("DEBUG: Parsed family: inet6\n");
                } else {
                    lexer_next(&lexer);
                }
            }
            lexer_cleanup(&lexer);
            return 0;
        }
        
        // Handle set commands for routes
        if (cmd->type == CMD_SET) {
            // Parse optional FIB
            while (lexer_has_more(&lexer)) {
                token = lexer_peek(&lexer);
                if (strcmp(token, "fib") == 0) {
                    lexer_next(&lexer);
                    if (lexer_has_more(&lexer)) {
                        token = lexer_next(&lexer);
                    cmd->fib = atoi(token);
                    }
                } else {
                    break;
                }
            }
            
            if (!lexer_has_more(&lexer)) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            token = lexer_next(&lexer);
            cmd->family = parse_family(token);
            
            // Parse destination
            if (!lexer_has_more(&lexer)) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            token = lexer_next(&lexer);
            char dest_str[INET6_ADDRSTRLEN];
            strncpy(dest_str, token, sizeof(dest_str) - 1);
            
            if (parse_cidr(dest_str, &cmd->data.route_config.prefix_len) < 0) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            if (parse_address(dest_str, cmd->family,
                            &cmd->data.route_config.dest,
                            &cmd->data.route_config.dest6) <= 0) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            // Parse gateway
            if (!lexer_has_more(&lexer)) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            token = lexer_next(&lexer);
            if (parse_address(token, cmd->family,
                            &cmd->data.route_config.gw,
                            &cmd->data.route_config.gw6) <= 0) {
                lexer_cleanup(&lexer);
                return -1;
            }
        }
    }
    
    // Handle interface commands
    if (strcmp(cmd->target, "interface") == 0) {
        if (!lexer_has_more(&lexer)) {
            lexer_cleanup(&lexer);
            return -1;
        }
        
        token = lexer_next(&lexer);
        strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
        
        if (cmd->type == CMD_SET) {
            // Parse interface name
            if (!lexer_has_more(&lexer)) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            token = lexer_next(&lexer);
            strncpy(cmd->name, token, sizeof(cmd->name) - 1);
            
            // Parse family
            if (!lexer_has_more(&lexer)) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            token = lexer_next(&lexer);
            cmd->family = parse_family(token);
            
            // Parse optional FIB
            while (lexer_has_more(&lexer)) {
                token = lexer_peek(&lexer);
                if (strcmp(token, "fib") == 0) {
                    lexer_next(&lexer);
                    if (lexer_has_more(&lexer)) {
                        token = lexer_next(&lexer);
                        cmd->fib = atoi(token);
                    }
                } else {
                    break;
                }
            }
            
            // Parse "addr"
            if (!lexer_has_more(&lexer) || strcmp(lexer_peek(&lexer), "addr") != 0) {
                lexer_cleanup(&lexer);
                return -1;
            }
            lexer_next(&lexer);
            
            // Parse address
            if (!lexer_has_more(&lexer)) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            token = lexer_next(&lexer);
            char addr_str[INET6_ADDRSTRLEN];
            strncpy(addr_str, token, sizeof(addr_str) - 1);
            
            if (parse_cidr(addr_str, &cmd->data.if_config.prefix_len) < 0) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            if (parse_address(addr_str, cmd->family,
                            &cmd->data.if_config.addr,
                            &cmd->data.if_config.addr6) <= 0) {
                lexer_cleanup(&lexer);
                return -1;
            }
            
            strncpy(cmd->data.if_config.name, cmd->name, sizeof(cmd->data.if_config.name) - 1);
            cmd->data.if_config.family = cmd->family;
            cmd->data.if_config.fib = cmd->fib;
        }
    }
    
    lexer_cleanup(&lexer);
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