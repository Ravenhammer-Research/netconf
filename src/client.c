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
 * @file client.c
 * @brief netd client implementation with CLI and NETCONF support
 * 
 * This file implements the netd client application that communicates with
 * the netd server via Unix domain sockets. Key features include:
 * - Interactive CLI mode with readline support and tab completion
 * - One-shot command mode for scripting and automation
 * - XML response parsing using FreeBSD's BSDXML library
 * - Support for both plain text and NETCONF XML responses
 * - Command history and editing capabilities
 * - Formatted output display for network configuration data
 * 
 * The client can operate in both interactive mode (similar to network CLI
 * shells) and batch mode for integration with scripts and automation tools.
 */

#include "common.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <bsdxml.h>
 
// Structure to hold parsing state
typedef struct {
    char current_element[64];
    char ifname[64];
    char ipv4[64];
    char prefix[8];
    char fib[8];
    char tunnel_fib[8];
    char dest[64];
    char next[64];
    int in_interface;
    int in_route;
    int in_name;
    int in_ip;
    int in_prefix;
    int in_fib;
    int in_tunnel_fib;
    int in_dest;
    int in_next;
    int header_printed;
} parse_state_t;

/**
 * Establish connection to the netd server via Unix domain socket
 * @return Socket file descriptor on success, -1 on failure
 */
static int connect_to_server(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

/**
 * Send a command string to the server
 * @param sock Socket file descriptor
 * @param cmd Command string to send
 * @return 0 on success, -1 on failure
 */
static int send_command(int sock, const char *cmd) {
    size_t len = strlen(cmd);
    if (send(sock, &len, sizeof(len), 0) < 0) {
        perror("send length");
        return -1;
    }
    
    if (send(sock, cmd, len, 0) < 0) {
        perror("send command");
        return -1;
    }
    
    return 0;
}

/**
 * Receive response from the server
 * @param sock Socket file descriptor
 * @param response Buffer to store the response
 * @param max_len Maximum size of response buffer
 * @return 0 on success, -1 on failure
 */
static int receive_response(int sock, char *response, size_t max_len) {
    size_t len;
    if (recv(sock, &len, sizeof(len), 0) < 0) {
        perror("recv length");
        return -1;
    }
    
    if (len >= max_len) {
        fprintf(stderr, "Response too large\n");
        return -1;
    }
    
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(sock, response + received, len - received, 0);
        if (n < 0) {
            perror("recv response");
            return -1;
        }
        received += n;
    }
    
    response[len] = '\0';
    return 0;
}

/**
 * XML start element handler for parsing NETCONF responses
 * @param userData Pointer to parse context structure
 * @param name Element name
 * @param atts Element attributes (unused)
 */
static void XMLCALL start_element_handler(void *userData, const XML_Char *name, const XML_Char **atts __attribute__((unused))) {
    parse_state_t *state = (parse_state_t *)userData;
    strncpy(state->current_element, name, sizeof(state->current_element) - 1);
    state->current_element[sizeof(state->current_element) - 1] = '\0';
    
    // Handle namespaced elements by checking the local name part
    const char *local_name = strrchr(name, ':');
    if (local_name) {
        local_name++; // Skip the colon
    } else {
        local_name = name;
    }
    
    if (strcmp(local_name, "interface") == 0) {
        state->in_interface = 1;
        memset(state->ifname, 0, sizeof(state->ifname));
        memset(state->ipv4, 0, sizeof(state->ipv4));
        memset(state->prefix, 0, sizeof(state->prefix));
        memset(state->fib, 0, sizeof(state->fib));
        memset(state->tunnel_fib, 0, sizeof(state->tunnel_fib));
    } else if (strcmp(local_name, "route") == 0) {
        state->in_route = 1;
        memset(state->dest, 0, sizeof(state->dest));
        memset(state->next, 0, sizeof(state->next));
    } else if (strcmp(local_name, "name") == 0) {
        state->in_name = 1;
    } else if (strcmp(local_name, "ip") == 0) {
        state->in_ip = 1;
    } else if (strcmp(local_name, "prefix-length") == 0) {
        state->in_prefix = 1;
    } else if (strcmp(local_name, "fib") == 0) {
        state->in_fib = 1;
    } else if (strcmp(local_name, "tunnel-fib") == 0) {
        state->in_tunnel_fib = 1;
    } else if (strcmp(local_name, "destination-prefix") == 0) {
        state->in_dest = 1;
    } else if (strcmp(local_name, "next-hop-address") == 0) {
        state->in_next = 1;
    }
}

/**
 * XML end element handler for parsing NETCONF responses
 * @param userData Pointer to parse context structure
 * @param name Element name
 */
static void XMLCALL end_element_handler(void *userData, const XML_Char *name) {
    parse_state_t *state = (parse_state_t *)userData;
    
    // Handle namespaced elements by checking the local name part
    const char *local_name = strrchr(name, ':');
    if (local_name) {
        local_name++; // Skip the colon
    } else {
        local_name = name;
    }
    
    if (strcmp(local_name, "interface") == 0) {
        // Display the interface
        if (strlen(state->ifname) > 0) {
            char ipv4_display[64];
            if (strlen(state->ipv4) > 0 && strlen(state->prefix) > 0) {
                snprintf(ipv4_display, sizeof(ipv4_display), "%s/%s", state->ipv4, state->prefix);
            } else {
                strcpy(ipv4_display, "-");
            }
            
            // Use FIB and TunnelFIB from XML, or default to "-" if not present
            const char *fib_display = (strlen(state->fib) > 0) ? state->fib : "-";
            const char *tunnel_fib_display = (strlen(state->tunnel_fib) > 0) ? state->tunnel_fib : "-";
            
            printf("%-12s %-18s %-18s %-8s %-8s %s\n", 
                   state->ifname, ipv4_display, "-", fib_display, tunnel_fib_display, "1500");
        }
        state->in_interface = 0;
    } else if (strcmp(local_name, "route") == 0) {
        // Display the route
        if (strlen(state->dest) > 0 && strlen(state->next) > 0) {
            printf("%-18s %-18s %-9s %-6s %s\n", state->dest, state->next, "UGS", "em0", "");
        }
        state->in_route = 0;
    } else if (strcmp(local_name, "name") == 0) {
        state->in_name = 0;
    } else if (strcmp(local_name, "ip") == 0) {
        state->in_ip = 0;
    } else if (strcmp(local_name, "prefix-length") == 0) {
        state->in_prefix = 0;
    } else if (strcmp(local_name, "fib") == 0) {
        state->in_fib = 0;
    } else if (strcmp(local_name, "tunnel-fib") == 0) {
        state->in_tunnel_fib = 0;
    } else if (strcmp(local_name, "destination-prefix") == 0) {
        state->in_dest = 0;
    } else if (strcmp(local_name, "next-hop-address") == 0) {
        state->in_next = 0;
    }
}

/**
 * XML character data handler for parsing NETCONF responses
 * @param userData Pointer to parse context structure
 * @param s Character data
 * @param len Length of character data
 */
static void XMLCALL character_data_handler(void *userData, const XML_Char *s, int len) {
    parse_state_t *state = (parse_state_t *)userData;
    
    if (state->in_name && state->in_interface) {
        strncat(state->ifname, s, len);
    } else if (state->in_ip && state->in_interface) {
        strncat(state->ipv4, s, len);
    } else if (state->in_prefix && state->in_interface) {
        strncat(state->prefix, s, len);
    } else if (state->in_fib && state->in_interface) {
        strncat(state->fib, s, len);
    } else if (state->in_tunnel_fib && state->in_interface) {
        strncat(state->tunnel_fib, s, len);
    } else if (state->in_dest && state->in_route) {
        strncat(state->dest, s, len);
    } else if (state->in_next && state->in_route) {
        strncat(state->next, s, len);
    }
}

/**
 * Parse and display server response (XML or plain text)
 * @param response Response string from server
 */
static void parse_and_display_response(const char *response) {
    // Always try to parse as XML using BSDXML
    XML_Parser parser = XML_ParserCreate(NULL);
    if (!parser) {
        printf("Failed to create XML parser\n");
        printf("%s\n", response);
        return;
    }
    
    parse_state_t state = {0};
    XML_SetUserData(parser, &state);
    XML_SetElementHandler(parser, start_element_handler, end_element_handler);
    XML_SetCharacterDataHandler(parser, character_data_handler);
    
    // Check response type and print header if needed
    if (strstr(response, "interfaces") || strstr(response, "ietf-interfaces")) {
        printf("Interface    IPv4 Address       IPv6 Address       FIB      TunnelFIB MTU\n");
    }
    
    // Try to parse the response as XML
    if (XML_Parse(parser, response, strlen(response), 1) == XML_STATUS_OK) {
        // XML parsed successfully
        if (strstr(response, "<ok/>")) {
            printf("OK\n");
        }
    } else {
        // Not valid XML - this is plain text response (CLI mode)
        printf("%s", response);
    }
    
    XML_ParserFree(parser);
}

/**
 * Command generator for readline tab completion
 * @param text Text being completed
 * @param state Completion state (0 for first call, non-zero for subsequent)
 * @return Next possible completion or NULL when done
 */
char* command_generator(const char* text, int state) {
    static int list_index, len;
    static const char* commands[] = {
        "show", "set", "delete", "commit", "save", "help", "quit", "exit",
        "interface", "route", "ethernet", "bridge", "gif", "tun", "tap",
        "inet", "inet6", "addr", "protocol", "protocols", "static", "fib"
    };
    
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    
    while (list_index < (int)(sizeof(commands)/sizeof(commands[0]))) {
        const char* name = commands[list_index++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    
    return NULL;
}

/**
 * Command completion function for readline
 * @param text Text being completed
 * @param start Start position in line (unused)
 * @param end End position in line (unused)
 * @return Array of possible completions
 */
char** command_completion(const char* text, int start __attribute__((unused)), int end __attribute__((unused))) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, command_generator);
}

/**
 * Interactive mode with readline support and tab completion
 */
static void interactive_mode(void) {
    // Set up readline
    rl_readline_name = "net";
    rl_attempted_completion_function = command_completion;
    
    char *line;
    while ((line = readline("net> ")) != NULL) {
        if (strlen(line) == 0) {
            free(line);
            continue;
        }
        
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            free(line);
            break;
        }
        
        // Add to history
        add_history(line);
        
        int sock = connect_to_server();
        if (sock < 0) {
            free(line);
            continue;
        }
        
        if (send_command(sock, line) == 0) {
            char response[MAX_RESPONSE_LEN];
            if (receive_response(sock, response, sizeof(response)) == 0) {
                parse_and_display_response(response);
            }
        }
        
        close(sock);
        free(line);
    }
}

/**
 * One-shot mode for single command execution
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 */
static void one_shot_mode(int argc, char *argv[]) {
    // Build command from arguments
    char cmd[MAX_CMD_LEN] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }
    
    int sock = connect_to_server();
    if (sock < 0) {
        exit(1);
    }
    
    if (send_command(sock, cmd) == 0) {
        char response[MAX_RESPONSE_LEN];
        if (receive_response(sock, response, sizeof(response)) == 0) {
            parse_and_display_response(response);
        }
    }
    
    close(sock);
}

/**
 * Main entry point for the netd client
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status
 */
int main(int argc, char *argv[]) {
    if (argc == 1) {
        // Interactive mode
        interactive_mode();
    } else {
        // One-shot mode
        one_shot_mode(argc, argv);
    }
    
    return 0;
} 