#include "common.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#define SOCKET_PATH "/var/run/netd.sock"
#define MAX_CMD_LEN 1024
#define MAX_RESPONSE_LEN 4096

// Forward declarations
extern int load_configuration(void);
extern int init_yang_context(void);
extern void cleanup_yang_context(void);

static int server_socket = -1;

static void cleanup(void) {
    if (server_socket >= 0) {
        close(server_socket);
        unlink(SOCKET_PATH);
    }
    cleanup_yang_context();
}

static void signal_handler(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    cleanup();
    exit(0);
}

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
                
                // Parse and execute command
                command_t cmd;
                if (parse_command(cmd_buf, &cmd) == 0) {
                    printf("DEBUG: Parsed command type: %d\n", cmd.type);
                    char response[MAX_RESPONSE_LEN];
                    if (execute_command(&cmd, response, sizeof(response)) == 0) {
                        printf("DEBUG: Command executed successfully, response length: %zu\n", strlen(response));
                        // Send response with length prefix
                        size_t resp_len = strlen(response);
                        send(client_socket, &resp_len, sizeof(resp_len), 0);
                        send(client_socket, response, resp_len, 0);
                } else {
                        printf("DEBUG: Command execution failed\n");
                        size_t resp_len = strlen(response);
                        send(client_socket, &resp_len, sizeof(resp_len), 0);
                        send(client_socket, response, resp_len, 0);
                }
                } else {
                    printf("DEBUG: Command parsing failed\n");
                    const char *error = "Error: Invalid command\n";
                    size_t resp_len = strlen(error);
                    send(client_socket, &resp_len, sizeof(resp_len), 0);
                    send(client_socket, error, resp_len, 0);
        }
            }
        }
        
        close(client_socket);
    }
    
    cleanup();
    return 0;
} 