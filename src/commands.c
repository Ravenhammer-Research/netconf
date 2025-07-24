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
 * @file commands.c
 * @brief Command line parsing and validation for netd
 * 
 * This file implements comprehensive command line parsing functionality
 * for the netd CLI interface, including:
 * - Lexical analysis and tokenization of command strings
 * - Command syntax validation and argument parsing
 * - Support for show, set, delete, commit, and save operations
 * - Interface and route command parameter handling
 * - Address parsing for IPv4 and IPv6 configurations
 * - Usage text generation and help functionality
 * 
 * The parser supports a structured command syntax similar to Cisco IOS
 * and other network management CLIs, with proper validation and error
 * reporting for invalid commands or parameters.
 */

#include "common.h"

// Command definition structure
typedef struct {
    const char *name;
    const char *syntax;
    int min_args;
    int max_args;
    cmd_type_t type;
} cmd_def_t;

// Command definitions
static const cmd_def_t cmd_definitions[] = {
    {"show", "show <target> [args...]", 1, 10, CMD_SHOW},
    {"set", "set <target> <args...>", 2, 20, CMD_SET},
    {"delete", "delete <target> [args...]", 1, 10, CMD_DELETE},
    {"commit", "commit", 0, 0, CMD_COMMIT},
    {"save", "save", 0, 0, CMD_SAVE},
    {"help", "help", 0, 0, CMD_SHOW},
    {"?", "?", 0, 0, CMD_SHOW},
    {NULL, NULL, 0, 0, CMD_UNKNOWN}
};

// Target-specific command definitions
typedef struct {
    const char *target;
    const char *syntax;
    int min_args;
    int max_args;
    const char *valid_args[10];
} target_def_t;

static const target_def_t target_definitions[] = {
    {
        "interface", 
        "show interface [type]",
        0, 1,
        {"ethernet", "bridge", "gif", "tun", "tap", "vlan", "lo"}
    },
    {
        "route",
        "show route [fib <n>] [protocol <type>] [inet|inet6]",
        1, 6,
        {"fib", "protocol", "inet", "inet6", "static", "dynamic"}
    },
    {NULL, NULL, 0, 0, {NULL}}
};

// Lexer structure
typedef struct {
    char *tokens[32];
    int count;
    int pos;
} lexer_t;

/**
 * Initialize lexer with input string, tokenizing on whitespace
 * @param lexer Pointer to lexer structure to initialize
 * @param input Input string to tokenize
 */
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

/**
 * Clean up lexer by freeing allocated token strings
 * @param lexer Pointer to lexer structure to clean up
 */
static void lexer_cleanup(lexer_t *lexer) {
    for (int i = 0; i < lexer->count; i++) {
        free(lexer->tokens[i]);
    }
}

/**
 * Peek at current token without advancing position
 * @param lexer Pointer to lexer structure
 * @return Current token string, or NULL if at end
 */
static const char* lexer_peek(lexer_t *lexer) {
    return (lexer->pos < lexer->count) ? lexer->tokens[lexer->pos] : NULL;
}

/**
 * Get current token and advance position
 * @param lexer Pointer to lexer structure
 * @return Current token string, or NULL if at end
 */
static const char* lexer_next(lexer_t *lexer) {
    return (lexer->pos < lexer->count) ? lexer->tokens[lexer->pos++] : NULL;
}

/**
 * Check if more tokens are available
 * @param lexer Pointer to lexer structure
 * @return 1 if more tokens available, 0 otherwise
 */
static int lexer_has_more(lexer_t *lexer) {
    return lexer->pos < lexer->count;
}

/**
 * Get number of remaining tokens
 * @param lexer Pointer to lexer structure
 * @return Number of tokens remaining
 */
static int lexer_remaining(lexer_t *lexer) {
    return lexer->count - lexer->pos;
}

/**
 * Find command definition by name
 * @param name Command name to search for
 * @return Pointer to command definition, or NULL if not found
 */
static const cmd_def_t* find_cmd_def(const char *name) {
    for (int i = 0; cmd_definitions[i].name != NULL; i++) {
        if (strcmp(cmd_definitions[i].name, name) == 0) {
            return &cmd_definitions[i];
        }
    }
    return NULL;
}

/**
 * Find target definition by target name
 * @param target Target name to search for
 * @return Pointer to target definition, or NULL if not found
 */
static const target_def_t* find_target_def(const char *target) {
    for (int i = 0; target_definitions[i].target != NULL; i++) {
        if (strcmp(target_definitions[i].target, target) == 0) {
            return &target_definitions[i];
        }
    }
    return NULL;
}

/**
 * Validate if argument is in the list of valid arguments
 * @param arg Argument to validate
 * @param valid_args Array of valid argument strings (NULL-terminated)
 * @return 1 if argument is valid, 0 otherwise
 */
static int is_valid_arg(const char *arg, const char *const valid_args[]) {
    for (int i = 0; valid_args[i] != NULL; i++) {
        if (strcmp(valid_args[i], arg) == 0) {
            return 1;
        }
    }
    return 0;
}

// Forward declarations
static int parse_address(const char *addr_str, addr_family_t family, 
                        struct in_addr *addr, struct in6_addr *addr6);
static int parse_cidr(const char *cidr_str, int *prefix_len);

/**
 * Parse route command arguments (fib, protocol, inet/inet6)
 * @param lexer Pointer to lexer structure with tokens
 * @param cmd Pointer to command structure to populate
 * @return 0 on success, -1 on error
 */
static int parse_route_args(lexer_t *lexer, command_t *cmd) {
    const char *token;
    int has_args = 0;
    
    while (lexer_has_more(lexer)) {
        token = lexer_peek(lexer);
        has_args = 1;
        
        if (strcmp(token, "fib") == 0) {
            lexer_next(lexer);
            if (!lexer_has_more(lexer)) {
                return -1; // Error: fib requires a number
            }
            token = lexer_next(lexer);
            cmd->fib = atoi(token);
            if (cmd->fib < 0) {
                return -1; // Error: invalid FIB number
            }
        } else if (strcmp(token, "protocol") == 0) {
            lexer_next(lexer);
            if (!lexer_has_more(lexer)) {
                return -1; // Error: protocol requires a type
            }
            token = lexer_next(lexer);
            if (strcmp(token, "static") != 0 && strcmp(token, "dynamic") != 0) {
                return -1; // Error: invalid protocol type
            }
            strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
        } else if (strcmp(token, "inet") == 0) {
            lexer_next(lexer);
            cmd->family = ADDR_FAMILY_INET4;
        } else if (strcmp(token, "inet6") == 0) {
            lexer_next(lexer);
            cmd->family = ADDR_FAMILY_INET6;
        } else {
            return -1; // Error: unknown argument
        }
    }
    
    // Require at least one argument
    if (!has_args) {
        return -1; // Error: show route requires at least one argument
    }
    
    return 0;
}

/**
 * Parse interface command arguments (interface type)
 * @param lexer Pointer to lexer structure with tokens
 * @param cmd Pointer to command structure to populate
 * @return 0 on success, -1 on error
 */
static int parse_interface_args(lexer_t *lexer, command_t *cmd) {
    if (lexer_has_more(lexer)) {
        const char *token = lexer_next(lexer);
        const target_def_t *target_def = find_target_def("interface");
        if (target_def && !is_valid_arg(token, target_def->valid_args)) {
            return -1; // Error: invalid interface type
        }
        strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
    }
    return 0;
}

/**
 * Parse set command arguments for interface and route configuration
 * @param lexer Pointer to lexer structure with tokens
 * @param cmd Pointer to command structure to populate
 * @return 0 on success, -1 on error
 */
static int parse_set_args(lexer_t *lexer, command_t *cmd) {
    const char *token;
    
    // Parse target-specific arguments
    if (strcmp(cmd->target, "interface") == 0) {
        // set interface <type> <name> <family> addr <addr>/<prefix> [fib <n>]
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer); // interface type
        strncpy(cmd->subtype, token, sizeof(cmd->subtype) - 1);
        
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer); // interface name
        strncpy(cmd->name, token, sizeof(cmd->name) - 1);
        
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer); // family
        cmd->family = (strcmp(token, "inet6") == 0) ? ADDR_FAMILY_INET6 : ADDR_FAMILY_INET4;
        
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer); // addr or address
        if (strcmp(token, "addr") != 0 && strcmp(token, "address") != 0) return -1;
        
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer); // address
        
        // Parse address and prefix
        char addr_str[INET6_ADDRSTRLEN];
        strncpy(addr_str, token, sizeof(addr_str) - 1);
        
        if (parse_cidr(addr_str, &cmd->data.if_config.prefix_len) < 0) return -1;
        
        // Extract just the IP address part (before the /)
        char *slash = strchr(addr_str, '/');
        if (slash) {
            *slash = '\0';
        }
        
        if (parse_address(addr_str, cmd->family, &cmd->data.if_config.addr, &cmd->data.if_config.addr6) <= 0) {
            return -1;
        }
        
        strncpy(cmd->data.if_config.name, cmd->name, sizeof(cmd->data.if_config.name) - 1);
        cmd->data.if_config.family = cmd->family;
        
        // Parse optional FIB
        while (lexer_has_more(lexer)) {
            token = lexer_peek(lexer);
            if (strcmp(token, "fib") == 0) {
                lexer_next(lexer);
                if (!lexer_has_more(lexer)) return -1;
                token = lexer_next(lexer);
                cmd->fib = atoi(token);
                cmd->data.if_config.fib = cmd->fib;
            } else {
                break;
            }
        }
        
    } else if (strcmp(cmd->target, "route") == 0) {
        // set route protocol static [fib <n>] <family> <dest> <gw>
        if (!lexer_has_more(lexer) || strcmp(lexer_next(lexer), "protocol") != 0) return -1;
        if (!lexer_has_more(lexer) || strcmp(lexer_next(lexer), "static") != 0) return -1;
        
        // Parse optional FIB
        while (lexer_has_more(lexer)) {
            token = lexer_peek(lexer);
            if (strcmp(token, "fib") == 0) {
                lexer_next(lexer);
                if (!lexer_has_more(lexer)) return -1;
                token = lexer_next(lexer);
                cmd->fib = atoi(token);
            } else {
                break;
            }
        }
        
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer);
        cmd->family = (strcmp(token, "inet6") == 0) ? ADDR_FAMILY_INET6 : ADDR_FAMILY_INET4;
        
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer);
        if (parse_address(token, cmd->family, &cmd->data.route_config.dest, &cmd->data.route_config.dest6) <= 0) {
            return -1;
        }
        
        if (!lexer_has_more(lexer)) return -1;
        token = lexer_next(lexer);
        if (parse_address(token, cmd->family, &cmd->data.route_config.gw, &cmd->data.route_config.gw6) <= 0) {
            return -1;
        }
    }
    
    return 0;
}

/**
 * Parse IP address string into binary format
 * @param addr_str Address string to parse
 * @param family Address family (ADDR_FAMILY_INET4 or ADDR_FAMILY_INET6)
 * @param addr Pointer to IPv4 address structure (for IPv4)
 * @param addr6 Pointer to IPv6 address structure (for IPv6)
 * @return 1 on success, 0 on failure
 */
static int parse_address(const char *addr_str, addr_family_t family, 
                        struct in_addr *addr, struct in6_addr *addr6) {
    if (family == ADDR_FAMILY_INET4) {
        return inet_pton(AF_INET, addr_str, addr);
    } else {
        return inet_pton(AF_INET6, addr_str, addr6);
    }
}

/**
 * Parse CIDR notation to extract prefix length
 * @param cidr_str CIDR string (e.g., "192.168.1.0/24")
 * @param prefix_len Pointer to store extracted prefix length
 * @return 0 on success, -1 on failure
 */
static int parse_cidr(const char *cidr_str, int *prefix_len) {
    char *slash = strchr(cidr_str, '/');
    if (!slash) {
        *prefix_len = (strlen(cidr_str) == 4) ? 32 : 128;
        return 0;
    }
    
    *prefix_len = atoi(slash + 1);
    return 0;
}

/**
 * Parse command line string into command structure
 * @param cmd_line Command line string to parse
 * @param cmd Pointer to command structure to populate
 * @return 0 on success, -1 on error
 */
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
    const cmd_def_t *cmd_def = find_cmd_def(token);
    if (!cmd_def) {
        lexer_cleanup(&lexer);
        return -1;
    }
    
    cmd->type = cmd_def->type;
    
    // Handle commands with no arguments
    if (cmd_def->min_args == 0) {
        if (lexer_remaining(&lexer) != 0) {
            lexer_cleanup(&lexer);
            return -1; // Too many arguments
        }
        lexer_cleanup(&lexer);
        return 0;
    }
    
    // Parse target for commands that need it
    if (!lexer_has_more(&lexer)) {
        lexer_cleanup(&lexer);
        return -1; // Missing target
    }
    
    token = lexer_next(&lexer);
    strncpy(cmd->target, token, sizeof(cmd->target) - 1);
    
    // Normalize "interfaces" to "interface"
    if (strcmp(cmd->target, "interfaces") == 0) {
        strcpy(cmd->target, "interface");
    }
    
    // Validate target
    const target_def_t *target_def = find_target_def(cmd->target);
    if (!target_def) {
        lexer_cleanup(&lexer);
        return -1; // Unknown target
    }
    
    // Parse target-specific arguments
    int result = -1;
    if (cmd->type == CMD_SHOW) {
    if (strcmp(cmd->target, "interface") == 0) {
            result = parse_interface_args(&lexer, cmd);
    } else if (strcmp(cmd->target, "route") == 0) {
            result = parse_route_args(&lexer, cmd);
        }
    } else if (cmd->type == CMD_SET) {
        result = parse_set_args(&lexer, cmd);
    }
    
    lexer_cleanup(&lexer);
    return result;
}

/**
 * Get usage text string for help display
 * @return Static string containing usage information
 */
const char* get_usage_text(void) {
    return "Usage:\n"
           "  net [command]                    - One-shot mode\n"
           "  net                              - Interactive mode\n"
           "\nCommands:\n"
           "  show interface [type]            - Show interfaces (optionally filtered by type)\n"
           "  show route [fib N] [protocol static|dynamic] [inet|inet6] - Show routing table\n"
           "  set interface <name> inet addr|address <addr>/<prefix> [fib N]\n"
           "  set interface <name> inet6 addr|address <addr>/<prefix> [fib N]\n"
           "  set route protocol static [fib N] inet <dest> <gw>\n"
           "  set route protocol static [fib N] inet6 <dest> <gw>\n"
           "  delete route protocol static [fib N]\n"
           "  commit                           - Apply queued changes\n"
           "  save                             - Persist configuration\n"
           "  help, ?                          - Show this help\n"
           "  quit, exit                       - Exit interactive mode\n"
           "\nInterface Types:\n"
           "  ethernet, bridge, gif, tun, tap, vlan, lo\n"
           "\nTab completion is available for all commands.\n";
}

/**
 * Print usage information to stdout
 */
void print_usage(void) {
    printf("%s", get_usage_text());
} 