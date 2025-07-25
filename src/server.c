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
 * @file server.c
 * @brief Main netd server implementation
 * 
 * This file implements the core netd server functionality including:
 * - Unix domain socket server setup and management
 * - Client connection handling and message processing
 * - NETCONF protocol message detection and routing
 * - Integration between CLI commands and NETCONF operations
 * - Signal handling for graceful shutdown
 * - Configuration loading and YANG context initialization
 * 
 * The server accepts both plain text CLI commands and NETCONF XML messages
 * from clients, routing them to the appropriate subsystems for processing.
 */

#include "common.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#define SOCKET_PATH "/var/run/netd.sock"
#define MAX_CMD_LEN 1024

// Forward declarations
extern int load_configuration(void);
extern int init_yang_context(void);
extern void cleanup_yang_context(void);
extern int parse_netconf_message(const char *xml_msg, command_t *cmd);
extern int handle_netconf_get_config(const char *filter, char *response, size_t resp_len);
extern int handle_netconf_edit_config(const char *config, char *response, size_t resp_len);
extern int handle_netconf_commit(char *response, size_t resp_len);
extern int show_routes(char *response, size_t resp_len, int fib_filter, const char *protocol_filter, int family_filter);

static int server_socket = -1;

/**
 * Cleanup function to close socket and remove socket file
 */
static void cleanup(void) {
    if (server_socket >= 0) {
        close(server_socket);
        unlink(SOCKET_PATH);
    }
    cleanup_yang_context();
}

/**
 * Signal handler for graceful shutdown
 * @param sig Signal number received
 */
static void signal_handler(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    cleanup();
    exit(0);
}

/**
 * Main server entry point
 * @return Exit status
 */
int main(void) {
    struct sockaddr_un addr;
    struct sigaction sa;
    
    // Set up signal handlers
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Create socket
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return 1;
    }

    // Remove existing socket file
    unlink(SOCKET_PATH);

    // Bind socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        cleanup();
        return 1;
    }

    // Set socket permissions
    chmod(SOCKET_PATH, 0666);

    // Listen for connections
    if (listen(server_socket, 5) < 0) {
        perror("listen");
        cleanup();
        return 1;
}

    printf("netd server started, listening on %s\n", SOCKET_PATH);
    
    // Initialize YANG context
    if (init_yang_context() != 0) {
        fprintf(stderr, "Failed to initialize YANG context\n");
        cleanup();
        return 1;
        }
    
    // Load configuration on startup
    if (load_configuration() == 0) {
        printf("Configuration loaded from /usr/local/etc/net.conf\n");
    } else {
        printf("No configuration file found, starting with default settings\n");
    }
    
    // Main server loop
    while (1) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        
        // Handle client request
        size_t cmd_len;
        ssize_t n = recv(client_socket, &cmd_len, sizeof(cmd_len), 0);
        if (n == sizeof(cmd_len)) {
            if (cmd_len > 0 && cmd_len < MAX_CMD_LEN) {
                char cmd_buf[MAX_CMD_LEN];
                size_t received = 0;
                while (received < cmd_len) {
                    n = recv(client_socket, cmd_buf + received, cmd_len - received, 0);
                    if (n <= 0) break;
                    received += n;
                }
                cmd_buf[cmd_len] = '\0';
                printf("DEBUG: Received command: '%s'\n", cmd_buf);
                
                char response[MAX_RESPONSE_LEN];
                
                // Check if this is a NETCONF XML message
                if (strstr(cmd_buf, "<?xml") != NULL || strstr(cmd_buf, "<rpc") != NULL) {
                    printf("DEBUG: Detected NETCONF XML message\n");
                    
                    // Parse NETCONF message
                    command_t cmd;
                    if (parse_netconf_message(cmd_buf, &cmd) == 0) {
                        printf("DEBUG: NETCONF message parsed successfully\n");
                        
                        // Handle based on command type
                        if (cmd.type == CMD_SHOW) {
                            // Handle get-config
                            if (strstr(cmd_buf, "get-config") != NULL) {
                                const char *filter = strstr(cmd_buf, "<filter");
                                handle_netconf_get_config(filter ? filter : "", response, sizeof(response));
                            } else {
                                // Handle get
                                if (strstr(cmd_buf, "interface")) {
                                    show_interfaces_filtered(response, sizeof(response), "");
                                } else if (strstr(cmd_buf, "route")) {
                                    show_routes(response, sizeof(response), -1, NULL, AF_UNSPEC);
                                }
                            }
                        } else if (cmd.type == CMD_SET) {
                            // Handle edit-config
                            const char *config = strstr(cmd_buf, "<config>");
                            handle_netconf_edit_config(config ? config : "", response, sizeof(response));
                        } else if (cmd.type == CMD_COMMIT) {
                            // Handle commit
                            handle_netconf_commit(response, sizeof(response));
                        }
                        
                        printf("DEBUG: NETCONF response generated, length: %zu\n", strlen(response));
                    } else {
                        printf("DEBUG: Failed to parse NETCONF message\n");
                        snprintf(response, sizeof(response), "Error: Invalid NETCONF message\n");
                    }
                } else {
                    // Convert CLI command to NETCONF XML and process it
                    printf("DEBUG: Converting CLI command to NETCONF XML\n");
                    
                    // Parse CLI command first
                    command_t cmd;
                    if (parse_command(cmd_buf, &cmd) == 0) {
                        printf("DEBUG: CLI command parsed, type: %d, target: %s\n", cmd.type, cmd.target);
                        
                        // Convert to NETCONF XML based on command type
                        char netconf_xml[2048];
                        if (cmd.type == CMD_SHOW) {
                            // Convert show command to get-config
                            if (strcmp(cmd.target, "interface") == 0) {
                                snprintf(netconf_xml, sizeof(netconf_xml),
                                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                    "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
                                    "  <get-config>\n"
                                    "    <source>\n"
                                    "      <running/>\n"
                                    "    </source>\n"
                                    "    <filter type=\"subtree\">\n"
                                    "      <interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\"/>\n"
                                    "    </filter>\n"
                                    "  </get-config>\n"
                                    "</rpc>");
                            } else if (strcmp(cmd.target, "route") == 0) {
                                // Include protocol filter and FIB filter in the XML if specified
                                const char *protocol_filter = (strlen(cmd.subtype) > 0) ? cmd.subtype : NULL;
                                int fib_filter = cmd.fib;
                                
                                if (protocol_filter && fib_filter >= 0) {
                                    snprintf(netconf_xml, sizeof(netconf_xml),
                                        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                        "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
                                        "  <get-config>\n"
                                        "    <source>\n"
                                        "      <running/>\n"
                                        "    </source>\n"
                                        "    <filter type=\"subtree\">\n"
                                        "      <routing xmlns=\"urn:ietf:params:xml:ns:yang:ietf-routing\">\n"
                                        "        <control-plane-protocols>\n"
                                        "          <control-plane-protocol>\n"
                                        "            <type xmlns:rt=\"urn:ietf:params:xml:ns:yang:ietf-routing\">rt:%s</type>\n"
                                        "            <name>static-routing</name>\n"
                                        "          </control-plane-protocol>\n"
                                        "        </control-plane-protocols>\n"
                                        "      </routing>\n"
                                        "    </filter>\n"
                                        "  </get-config>\n"
                                        "</rpc>", protocol_filter);
                                } else if (protocol_filter) {
                                    snprintf(netconf_xml, sizeof(netconf_xml),
                                        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                        "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
                                        "  <get-config>\n"
                                        "    <source>\n"
                                        "      <running/>\n"
                                        "    </source>\n"
                                        "    <filter type=\"subtree\">\n"
                                        "      <routing xmlns=\"urn:ietf:params:xml:ns:yang:ietf-routing\">\n"
                                        "        <control-plane-protocols>\n"
                                        "          <control-plane-protocol>\n"
                                        "            <type xmlns:rt=\"urn:ietf:params:xml:ns:yang:ietf-routing\">rt:%s</type>\n"
                                        "            <name>static-routing</name>\n"
                                        "          </control-plane-protocol>\n"
                                        "        </control-plane-protocols>\n"
                                        "      </routing>\n"
                                        "    </filter>\n"
                                        "  </get-config>\n"
                                        "</rpc>", protocol_filter);
                                } else {
                                    snprintf(netconf_xml, sizeof(netconf_xml),
                                        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                        "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
                                        "  <get-config>\n"
                                        "    <source>\n"
                                        "      <running/>\n"
                                        "    </source>\n"
                                        "    <filter type=\"subtree\">\n"
                                        "      <routing xmlns=\"urn:ietf:params:xml:ns:yang:ietf-routing\"/>\n"
                                        "    </filter>\n"
                                        "  </get-config>\n"
                                        "</rpc>");
                                }
                            }
                        } else if (cmd.type == CMD_SET) {
                            // Convert set command to edit-config
                            if (strcmp(cmd.target, "interface") == 0) {
                                // Convert binary address to string
                                char addr_str[INET6_ADDRSTRLEN];
                                if (cmd.data.if_config.family == ADDR_FAMILY_INET6) {
                                    inet_ntop(AF_INET6, &cmd.data.if_config.addr6, addr_str, sizeof(addr_str));
                                } else {
                                    inet_ntop(AF_INET, &cmd.data.if_config.addr, addr_str, sizeof(addr_str));
                                }
                                
                                snprintf(netconf_xml, sizeof(netconf_xml),
                                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                    "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
                                    "  <edit-config>\n"
                                    "    <target>\n"
                                    "      <running/>\n"
                                    "    </target>\n"
                                    "    <config>\n"
                                    "      <interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\">\n"
                                    "        <interface>\n"
                                    "          <name>%s</name>\n"
                                    "          <type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>\n"
                                    "          <enabled>true</enabled>\n"
                                    "          <ipv4 xmlns=\"urn:ietf:params:xml:ns:yang:ietf-ip\">\n"
                                    "            <enabled>true</enabled>\n"
                                    "            <address>\n"
                                    "              <ip>%s</ip>\n"
                                    "              <prefix-length>%d</prefix-length>\n"
                                    "            </address>\n"
                                    "          </ipv4>\n"
                                    "        </interface>\n"
                                    "      </interfaces>\n"
                                    "    </config>\n"
                                    "  </edit-config>\n"
                                    "</rpc>",
                                    cmd.data.if_config.name,
                                    addr_str,
                                    cmd.data.if_config.prefix_len);
                            } else if (strcmp(cmd.target, "route") == 0) {
                                // Convert binary addresses to strings
                                char dest_str[INET6_ADDRSTRLEN];
                                char gw_str[INET6_ADDRSTRLEN];
                                
                                if (cmd.data.route_config.family == ADDR_FAMILY_INET6) {
                                    inet_ntop(AF_INET6, &cmd.data.route_config.dest6, dest_str, sizeof(dest_str));
                                    inet_ntop(AF_INET6, &cmd.data.route_config.gw6, gw_str, sizeof(gw_str));
                                } else {
                                    inet_ntop(AF_INET, &cmd.data.route_config.dest, dest_str, sizeof(dest_str));
                                    inet_ntop(AF_INET, &cmd.data.route_config.gw, gw_str, sizeof(gw_str));
                                }
                                
                                snprintf(netconf_xml, sizeof(netconf_xml),
                                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                    "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
                                    "  <edit-config>\n"
                                    "    <target>\n"
                                    "      <running/>\n"
                                    "    </target>\n"
                                    "    <config>\n"
                                    "      <routing xmlns=\"urn:ietf:params:xml:ns:yang:ietf-routing\">\n"
                                    "        <control-plane-protocols>\n"
                                    "          <control-plane-protocol>\n"
                                    "            <type xmlns:rt=\"urn:ietf:params:xml:ns:yang:ietf-routing\">rt:static</type>\n"
                                    "            <name>static-routing</name>\n"
                                    "            <static-routes>\n"
                                    "              <ipv4>\n"
                                    "                <route>\n"
                                    "                  <destination-prefix>%s</destination-prefix>\n"
                                    "                  <next-hop>\n"
                                    "                    <next-hop-address>%s</next-hop-address>\n"
                                    "                  </next-hop>\n"
                                    "                </route>\n"
                                    "              </ipv4>\n"
                                    "            </static-routes>\n"
                                    "          </control-plane-protocol>\n"
                                    "        </control-plane-protocols>\n"
                                    "      </routing>\n"
                                    "    </config>\n"
                                    "  </edit-config>\n"
                                    "</rpc>",
                                    dest_str,
                                    gw_str);
                            }
                        }
                        
                        printf("DEBUG: Generated NETCONF XML:\n%s\n", netconf_xml);
                        
                        // Parse the generated NETCONF XML
                        command_t netconf_cmd;
                        if (parse_netconf_message(netconf_xml, &netconf_cmd) == 0) {
                            printf("DEBUG: NETCONF message parsed successfully\n");
                            
                            // Handle based on command type
                            if (netconf_cmd.type == CMD_SHOW) {
                                // Handle get-config - always use the generated XML filter
                                if (strstr(netconf_xml, "get-config") != NULL) {
                                    const char *filter = strstr(netconf_xml, "<filter");
                                    printf("DEBUG: Using generated XML filter for get-config\n");
                                    handle_netconf_get_config(filter ? filter : "", response, sizeof(response));
                                } else {
                                    // This should not happen since we always generate get-config XML
                                    printf("DEBUG: Unexpected: get-config not found in generated XML\n");
                                    snprintf(response, sizeof(response), "Error: Internal error - get-config not found\n");
                                }
                            } else if (netconf_cmd.type == CMD_SET) {
                                // Handle edit-config
                                const char *config = strstr(netconf_xml, "<config>");
                                handle_netconf_edit_config(config ? config : "", response, sizeof(response));
                            } else if (netconf_cmd.type == CMD_COMMIT) {
                                // Handle commit
                                handle_netconf_commit(response, sizeof(response));
                            }
                            
                            printf("DEBUG: NETCONF response generated, length: %zu\n", strlen(response));
                        } else {
                            printf("DEBUG: Failed to parse generated NETCONF message\n");
                            snprintf(response, sizeof(response), "Error: Failed to process command\n");
                        }
                    } else {
                        printf("DEBUG: CLI command parsing failed\n");
                        snprintf(response, sizeof(response), "Error: Invalid command\n");
                    }
                }
                
                // Send response with length prefix
                size_t resp_len = strlen(response);
                send(client_socket, &resp_len, sizeof(resp_len), 0);
                send(client_socket, response, resp_len, 0);
            }
        }
        
        close(client_socket);
    }
    
    cleanup();
    return 0;
} 