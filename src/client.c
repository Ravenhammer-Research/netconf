#include "common.h"
#include <readline/readline.h>
#include <readline/history.h>

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

// Tab completion function
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

char** command_completion(const char* text, int start __attribute__((unused)), int end __attribute__((unused))) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, command_generator);
}

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
                printf("%s\n", response);
            }
        }
        
        close(sock);
        free(line);
    }
}

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
            printf("%s\n", response);
        }
    }
    
    close(sock);
}

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