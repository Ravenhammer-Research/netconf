#include "common.h"

static int server_sock = -1;
static int client_sock = -1;

static void cleanup(void) {
    if (client_sock >= 0) {
        close(client_sock);
    }
    if (server_sock >= 0) {
        close(server_sock);
        unlink(SOCKET_PATH);
    }
}

static int setup_server(void) {
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return -1;
    }

    // Remove existing socket file
    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }

    if (listen(server_sock, 5) < 0) {
        perror("listen");
        return -1;
    }

    // Set socket permissions
    chmod(SOCKET_PATH, 0666);

    return 0;
}

static int accept_client(void) {
    struct sockaddr_un client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
    if (client_sock < 0) {
        perror("accept");
        return -1;
    }
    
    return 0;
}

static int receive_command(char *cmd, size_t max_len) {
    size_t len;
    if (recv(client_sock, &len, sizeof(len), 0) < 0) {
        perror("recv length");
        return -1;
    }
    
    if (len >= max_len) {
        fprintf(stderr, "Command too large\n");
        return -1;
    }
    
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(client_sock, cmd + received, len - received, 0);
        if (n < 0) {
            perror("recv command");
            return -1;
        }
        received += n;
    }
    
    cmd[len] = '\0';
    return 0;
}

static int send_response(const char *response) {
    size_t len = strlen(response);
    if (send(client_sock, &len, sizeof(len), 0) < 0) {
        perror("send length");
        return -1;
    }
    
    if (send(client_sock, response, len, 0) < 0) {
        perror("send response");
        return -1;
    }
    
    return 0;
}

static void handle_client(void) {
    char cmd[MAX_CMD_LEN];
    char response[MAX_RESPONSE_LEN];
    
    while (1) {
        if (receive_command(cmd, sizeof(cmd)) < 0) {
            break;
        }
        
        // Check if this is a NETCONF XML message
        if (strstr(cmd, "<?xml") != NULL || strstr(cmd, "<rpc") != NULL) {
            // This is a NETCONF message, handle it directly
            if (strstr(cmd, "<get-config>") != NULL) {
                // Handle get-config
                char response_buf[MAX_RESPONSE_LEN];
                if (handle_netconf_get_config("", response_buf, sizeof(response_buf)) == 0) {
                    send_response(response_buf);
                } else {
                    send_response("Error: Failed to execute NETCONF get-config\n");
                }
            } else if (strstr(cmd, "<edit-config>") != NULL) {
                // Handle edit-config
                char response_buf[MAX_RESPONSE_LEN];
                if (handle_netconf_edit_config("", response_buf, sizeof(response_buf)) == 0) {
                    send_response(response_buf);
                } else {
                    send_response("Error: Failed to execute NETCONF edit-config\n");
                }
            } else if (strstr(cmd, "<commit/>") != NULL) {
                // Handle commit
                char response_buf[MAX_RESPONSE_LEN];
                if (handle_netconf_commit(response_buf, sizeof(response_buf)) == 0) {
                    send_response(response_buf);
                } else {
                    send_response("Error: Failed to execute NETCONF commit\n");
                }
            } else {
                send_response("Error: Unknown NETCONF operation\n");
            }
        } else {
            // Regular command parsing
            command_t parsed_cmd;
            if (parse_command(cmd, &parsed_cmd) == 0) {
                if (execute_command(&parsed_cmd, response, sizeof(response)) == 0) {
                    send_response(response);
                } else {
                    send_response("Error: Failed to execute command\n");
                }
            } else {
                send_response("Error: Invalid command format\n");
            }
        }
    }
}

int main(void) {
    atexit(cleanup);
    
    if (setup_server() < 0) {
        fprintf(stderr, "Failed to setup server\n");
        return 1;
    }
    
    printf("netd server started, listening on %s\n", SOCKET_PATH);
    
    while (1) {
        if (accept_client() < 0) {
            continue;
        }
        
        handle_client();
        close(client_sock);
        client_sock = -1;
    }
    
    return 0;
} 