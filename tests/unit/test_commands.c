#include "../../src/common.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command>\n", argv[0]);
        return 1;
    }
    
    // Build command from arguments
    char cmd[MAX_CMD_LEN] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }
    
    // Parse the command
    command_t parsed_cmd;
    if (parse_command(cmd, &parsed_cmd) == 0) {
        printf("Command parsed successfully:\n");
        printf("  Type: %d\n", parsed_cmd.type);
        printf("  Target: %s\n", parsed_cmd.target);
        printf("  Subtype: %s\n", parsed_cmd.subtype);
        printf("  Name: %s\n", parsed_cmd.name);
        printf("  Family: %d\n", parsed_cmd.family);
        printf("  FIB: %d\n", parsed_cmd.fib);
        return 0;
    } else {
        fprintf(stderr, "Failed to parse command: %s\n", cmd);
        return 1;
    }
} 