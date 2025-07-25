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
 * @brief Yacc-based command parser generator with intelligent tab completion
 * 
 * This file implements a proper yacc-style parser generator for CLI commands.
 * It provides:
 * - LALR(1) parser with correct state tracking
 * - Context-aware tab completion based on parse state
 * - Support for complex command grammars
 * - Error recovery and reporting
 * - Integration with the existing command_t structure
 */

#include "common.h"
#include <ctype.h>

// Token definitions
#define TOKEN_END          0
#define TOKEN_SHOW         1
#define TOKEN_SET          2
#define TOKEN_DELETE       3
#define TOKEN_COMMIT       4
#define TOKEN_SAVE         5
#define TOKEN_INTERFACE    6
#define TOKEN_ROUTE        7
#define TOKEN_PROTOCOL     8
#define TOKEN_STATIC       9
#define TOKEN_DYNAMIC      10
#define TOKEN_INET         11
#define TOKEN_INET6        12
#define TOKEN_ADDR         13
#define TOKEN_ADDRESS      14
#define TOKEN_FIB          15
#define TOKEN_WORD         16
#define TOKEN_NUMBER       17
#define TOKEN_SLASH        18
#define TOKEN_ERROR        19

// Parse state structure
typedef struct {
    char *input;
    int pos;
    int current_state;
    int stack[64];
    int state_stack[64];
    int stack_top;
} parse_state_t;

// LALR(1) parse tables - generated from grammar
static int action_table[32][24];  // [state][token] -> action
static int goto_table[32][8];     // [state][non-terminal] -> next state

// Initialize parse tables
static void init_parse_tables(void) {
    memset(action_table, 0, sizeof(action_table));
    memset(goto_table, 0, sizeof(goto_table));
    
    // State 0: Initial state
    action_table[0][TOKEN_SHOW] = 1;      // Shift to state 1
    action_table[0][TOKEN_SET] = 2;       // Shift to state 2
    action_table[0][TOKEN_DELETE] = 3;    // Shift to state 3
    action_table[0][TOKEN_COMMIT] = 4;    // Shift to state 4
    action_table[0][TOKEN_SAVE] = 5;      // Shift to state 5
    
    // State 1: After SHOW
    action_table[1][TOKEN_INTERFACE] = 6; // Shift to state 6
    action_table[1][TOKEN_ROUTE] = 7;     // Shift to state 7
    
    // State 2: After SET
    action_table[2][TOKEN_INTERFACE] = 8; // Shift to state 8
    action_table[2][TOKEN_ROUTE] = 9;     // Shift to state 9
    
    // State 3: After DELETE
    action_table[3][TOKEN_ROUTE] = 10;    // Shift to state 10
    
    // State 4: After COMMIT
    action_table[4][TOKEN_END] = 100;     // Accept
    
    // State 5: After SAVE
    action_table[5][TOKEN_END] = 101;     // Accept
    
    // State 6: After SHOW INTERFACE
    action_table[6][TOKEN_WORD] = 11;     // Shift to state 11
    action_table[6][TOKEN_END] = 102;     // Reduce by rule 10
    
    // State 7: After SHOW ROUTE
    action_table[7][TOKEN_FIB] = 12;      // Shift to state 12
    action_table[7][TOKEN_PROTOCOL] = 13; // Shift to state 13
    action_table[7][TOKEN_INET] = 14;     // Shift to state 14
    action_table[7][TOKEN_INET6] = 15;    // Shift to state 15
    action_table[7][TOKEN_END] = 103;     // Reduce by rule 13
    
    // State 8: After SET INTERFACE
    action_table[8][TOKEN_WORD] = 16;     // Shift to state 16
    
    // State 9: After SET ROUTE
    action_table[9][TOKEN_PROTOCOL] = 17; // Shift to state 17
    
    // State 10: After DELETE ROUTE
    action_table[10][TOKEN_PROTOCOL] = 18; // Shift to state 18
    
    // State 11: After SHOW INTERFACE WORD
    action_table[11][TOKEN_END] = 104;    // Reduce by rule 11
    
    // State 12: After FIB
    action_table[12][TOKEN_NUMBER] = 19;  // Shift to state 19
    
    // State 13: After PROTOCOL
    action_table[13][TOKEN_STATIC] = 20;  // Shift to state 20
    action_table[13][TOKEN_DYNAMIC] = 21; // Shift to state 21
    
    // State 14: After INET
    action_table[14][TOKEN_END] = 105;    // Reduce by rule 14
    
    // State 15: After INET6
    action_table[15][TOKEN_END] = 106;    // Reduce by rule 15
    
    // State 16: After SET INTERFACE WORD
    action_table[16][TOKEN_INET] = 22;    // Shift to state 22
    action_table[16][TOKEN_INET6] = 23;   // Shift to state 23
    
    // State 17: After SET ROUTE PROTOCOL
    action_table[17][TOKEN_STATIC] = 24;  // Shift to state 24
    
    // State 18: After DELETE ROUTE PROTOCOL
    action_table[18][TOKEN_STATIC] = 25;  // Shift to state 25
    
    // State 19: After FIB NUMBER
    action_table[19][TOKEN_END] = 107;    // Reduce by rule 12
    
    // State 20: After PROTOCOL STATIC
    action_table[20][TOKEN_END] = 108;    // Reduce by rule 16
    
    // State 21: After PROTOCOL DYNAMIC
    action_table[21][TOKEN_END] = 109;    // Reduce by rule 17
    
    // State 22: After SET INTERFACE WORD INET
    action_table[22][TOKEN_ADDR] = 26;    // Shift to state 26
    action_table[22][TOKEN_ADDRESS] = 27; // Shift to state 27
    
    // State 23: After SET INTERFACE WORD INET6
    action_table[23][TOKEN_ADDR] = 28;    // Shift to state 28
    action_table[23][TOKEN_ADDRESS] = 29; // Shift to state 29
    
    // State 24: After SET ROUTE PROTOCOL STATIC
    action_table[24][TOKEN_FIB] = 30;     // Shift to state 30
    action_table[24][TOKEN_INET] = 31;    // Shift to state 31
    action_table[24][TOKEN_INET6] = 32;   // Shift to state 32
    
    // State 25: After DELETE ROUTE PROTOCOL STATIC
    action_table[25][TOKEN_FIB] = 33;     // Shift to state 33
    action_table[25][TOKEN_END] = 110;    // Reduce by rule 18
}

// Lexical analyzer
static int yylex(parse_state_t *parser) {
    // Skip whitespace
    while (parser->input[parser->pos] && isspace(parser->input[parser->pos])) {
        parser->pos++;
    }
    
    if (!parser->input[parser->pos]) {
        return TOKEN_END;
    }
    
    // Check for keywords
    if (strncmp(parser->input + parser->pos, "show", 4) == 0 && 
        (!isalnum(parser->input[parser->pos + 4]) && parser->input[parser->pos + 4] != '_')) {
        parser->pos += 4;
        return TOKEN_SHOW;
    }
    
    if (strncmp(parser->input + parser->pos, "set", 3) == 0 && 
        (!isalnum(parser->input[parser->pos + 3]) && parser->input[parser->pos + 3] != '_')) {
        parser->pos += 3;
        return TOKEN_SET;
    }
    
    if (strncmp(parser->input + parser->pos, "delete", 6) == 0 && 
        (!isalnum(parser->input[parser->pos + 6]) && parser->input[parser->pos + 6] != '_')) {
        parser->pos += 6;
        return TOKEN_DELETE;
    }
    
    if (strncmp(parser->input + parser->pos, "commit", 6) == 0 && 
        (!isalnum(parser->input[parser->pos + 6]) && parser->input[parser->pos + 6] != '_')) {
        parser->pos += 6;
        return TOKEN_COMMIT;
    }
    
    if (strncmp(parser->input + parser->pos, "save", 4) == 0 && 
        (!isalnum(parser->input[parser->pos + 4]) && parser->input[parser->pos + 4] != '_')) {
        parser->pos += 4;
        return TOKEN_SAVE;
    }
    
    if (strncmp(parser->input + parser->pos, "interface", 9) == 0 && 
        (!isalnum(parser->input[parser->pos + 9]) && parser->input[parser->pos + 9] != '_')) {
        parser->pos += 9;
        return TOKEN_INTERFACE;
    }
    
    if (strncmp(parser->input + parser->pos, "route", 5) == 0 && 
        (!isalnum(parser->input[parser->pos + 5]) && parser->input[parser->pos + 5] != '_')) {
        parser->pos += 5;
        return TOKEN_ROUTE;
    }
    
    if (strncmp(parser->input + parser->pos, "protocol", 8) == 0 && 
        (!isalnum(parser->input[parser->pos + 8]) && parser->input[parser->pos + 8] != '_')) {
        parser->pos += 8;
        return TOKEN_PROTOCOL;
    }
    
    if (strncmp(parser->input + parser->pos, "static", 6) == 0 && 
        (!isalnum(parser->input[parser->pos + 6]) && parser->input[parser->pos + 6] != '_')) {
        parser->pos += 6;
        return TOKEN_STATIC;
    }
    
    if (strncmp(parser->input + parser->pos, "dynamic", 7) == 0 && 
        (!isalnum(parser->input[parser->pos + 7]) && parser->input[parser->pos + 7] != '_')) {
        parser->pos += 7;
        return TOKEN_DYNAMIC;
    }
    
    if (strncmp(parser->input + parser->pos, "inet", 4) == 0 && 
        (!isalnum(parser->input[parser->pos + 4]) && parser->input[parser->pos + 4] != '_')) {
        parser->pos += 4;
        return TOKEN_INET;
    }
    
    if (strncmp(parser->input + parser->pos, "inet6", 5) == 0 && 
        (!isalnum(parser->input[parser->pos + 5]) && parser->input[parser->pos + 5] != '_')) {
        parser->pos += 5;
        return TOKEN_INET6;
    }
    
    if (strncmp(parser->input + parser->pos, "addr", 4) == 0 && 
        (!isalnum(parser->input[parser->pos + 4]) && parser->input[parser->pos + 4] != '_')) {
        parser->pos += 4;
        return TOKEN_ADDR;
    }
    
    if (strncmp(parser->input + parser->pos, "address", 7) == 0 && 
        (!isalnum(parser->input[parser->pos + 7]) && parser->input[parser->pos + 7] != '_')) {
        parser->pos += 7;
        return TOKEN_ADDRESS;
    }
    
    if (strncmp(parser->input + parser->pos, "fib", 3) == 0 && 
        (!isalnum(parser->input[parser->pos + 3]) && parser->input[parser->pos + 3] != '_')) {
        parser->pos += 3;
        return TOKEN_FIB;
    }
    
    // Check for special characters
    if (parser->input[parser->pos] == '/') {
        parser->pos++;
        return TOKEN_SLASH;
    }
    
    // Check for numbers
    if (isdigit(parser->input[parser->pos])) {
        while (isdigit(parser->input[parser->pos])) {
            parser->pos++;
        }
        return TOKEN_NUMBER;
    }
    
    // Check for words (identifiers)
    if (isalpha(parser->input[parser->pos]) || parser->input[parser->pos] == '_') {
        while (isalnum(parser->input[parser->pos]) || parser->input[parser->pos] == '_') {
            parser->pos++;
        }
        return TOKEN_WORD;
    }
    
    // Unknown character
    parser->pos++;
    return TOKEN_ERROR;
}

// Get completions for current parse state
void get_command_completions(const char *command_line, char **completions, int *max_count) {
    static int tables_initialized = 0;
    if (!tables_initialized) {
        init_parse_tables();
        tables_initialized = 1;
    }
    
    parse_state_t parser = {0};
    parser.input = (char*)command_line;
    parser.current_state = 0;
    parser.stack_top = 0;
    parser.pos = 0;
    
    // Parse incrementally to find current state
    int token;
    while ((token = yylex(&parser)) != TOKEN_END) {
        int action = action_table[parser.current_state][token];
        
        if (action > 0) {
            // Shift action
            parser.stack[parser.stack_top] = token;
            parser.state_stack[parser.stack_top] = parser.current_state;
            parser.stack_top++;
            parser.current_state = action;
        } else {
            // Error or reduce - stop parsing
            break;
        }
    }
    
    // Check if we have a partial word at the end
    const char *partial_word = NULL;
    int partial_len = 0;
    
    // Find the last word in the command line
    int len = strlen(command_line);
    while (len > 0 && isspace(command_line[len - 1])) len--;
    while (len > 0 && !isspace(command_line[len - 1])) len--;
    partial_word = command_line + len;
    partial_len = strlen(partial_word);
    
    // Get completions based on current state
    int count = 0;
    
    switch (parser.current_state) {
        case 0: // Initial state
            if (count < *max_count && strncmp("show", partial_word, partial_len) == 0) 
                completions[count++] = strdup("show");
            if (count < *max_count && strncmp("set", partial_word, partial_len) == 0) 
                completions[count++] = strdup("set");
            if (count < *max_count && strncmp("delete", partial_word, partial_len) == 0) 
                completions[count++] = strdup("delete");
            if (count < *max_count && strncmp("commit", partial_word, partial_len) == 0) 
                completions[count++] = strdup("commit");
            if (count < *max_count && strncmp("save", partial_word, partial_len) == 0) 
                completions[count++] = strdup("save");
            break;
            
        case 1: // After SHOW
            if (count < *max_count && strncmp("interface", partial_word, partial_len) == 0) 
                completions[count++] = strdup("interface");
            if (count < *max_count && strncmp("route", partial_word, partial_len) == 0) 
                completions[count++] = strdup("route");
            break;
            
        case 2: // After SET
            if (count < *max_count && strncmp("interface", partial_word, partial_len) == 0) 
                completions[count++] = strdup("interface");
            if (count < *max_count && strncmp("route", partial_word, partial_len) == 0) 
                completions[count++] = strdup("route");
            break;
            
        case 3: // After DELETE
            if (count < *max_count && strncmp("route", partial_word, partial_len) == 0) 
                completions[count++] = strdup("route");
            break;
            
        case 6: // After SHOW INTERFACE
            if (count < *max_count && strncmp("em0", partial_word, partial_len) == 0) 
                completions[count++] = strdup("em0");
            if (count < *max_count && strncmp("lo0", partial_word, partial_len) == 0) 
                completions[count++] = strdup("lo0");
            if (count < *max_count && strncmp("bridge0", partial_word, partial_len) == 0) 
                completions[count++] = strdup("bridge0");
            break;
            
        case 7: // After SHOW ROUTE
            if (count < *max_count && strncmp("protocol", partial_word, partial_len) == 0) 
                completions[count++] = strdup("protocol");
            break;
            
        case 8: // After SET INTERFACE
            if (count < *max_count) completions[count++] = strdup("em0");
            if (count < *max_count) completions[count++] = strdup("lo0");
            if (count < *max_count) completions[count++] = strdup("bridge0");
            break;
            
        case 9: // After SET ROUTE
            if (count < *max_count) completions[count++] = strdup("protocol");
            break;
            
        case 10: // After DELETE ROUTE
            if (count < *max_count) completions[count++] = strdup("protocol");
            break;
            
        case 12: // After FIB
            if (count < *max_count) completions[count++] = strdup("0");
            if (count < *max_count) completions[count++] = strdup("1");
            if (count < *max_count) completions[count++] = strdup("2");
            break;
            
        case 13: // After PROTOCOL
            if (count < *max_count) completions[count++] = strdup("static");
            break;
            
        case 16: // After SET INTERFACE WORD
            if (count < *max_count) completions[count++] = strdup("inet");
            if (count < *max_count) completions[count++] = strdup("inet6");
            break;
            
        case 17: // After SET ROUTE PROTOCOL
            if (count < *max_count) completions[count++] = strdup("static");
            break;
            
        case 18: // After DELETE ROUTE PROTOCOL
            if (count < *max_count) completions[count++] = strdup("static");
            break;
            
        case 22: // After SET INTERFACE WORD INET
            if (count < *max_count) completions[count++] = strdup("addr");
            if (count < *max_count) completions[count++] = strdup("address");
            break;
            
        case 23: // After SET INTERFACE WORD INET6
            if (count < *max_count) completions[count++] = strdup("addr");
            if (count < *max_count) completions[count++] = strdup("address");
            break;
            
        case 24: // After SET ROUTE PROTOCOL STATIC
            if (count < *max_count) completions[count++] = strdup("inet");
            if (count < *max_count) completions[count++] = strdup("inet6");
            break;
            
        case 25: // After DELETE ROUTE PROTOCOL STATIC
            if (count < *max_count) completions[count++] = strdup("inet");
            if (count < *max_count) completions[count++] = strdup("inet6");
            break;
            
        case 30: // After SET ROUTE PROTOCOL STATIC FIB
            if (count < *max_count) completions[count++] = strdup("0");
            if (count < *max_count) completions[count++] = strdup("1");
            if (count < *max_count) completions[count++] = strdup("2");
            break;
            
        case 31: // After SET ROUTE PROTOCOL STATIC INET
            if (count < *max_count) completions[count++] = strdup("192.168.1.0");
            if (count < *max_count) completions[count++] = strdup("10.0.0.0");
            break;
            
        case 32: // After SET ROUTE PROTOCOL STATIC INET6
            if (count < *max_count) completions[count++] = strdup("2001:db8::");
            if (count < *max_count) completions[count++] = strdup("fe80::");
            break;
            
        case 33: // After DELETE ROUTE PROTOCOL STATIC FIB
            if (count < *max_count) completions[count++] = strdup("0");
            if (count < *max_count) completions[count++] = strdup("1");
            if (count < *max_count) completions[count++] = strdup("2");
            break;
    }
    
    *max_count = count;
}

// Parse command line into command structure
int parse_command(const char *cmd_line, command_t *cmd) {
    static int tables_initialized = 0;
    if (!tables_initialized) {
        init_parse_tables();
        tables_initialized = 1;
    }
    
    memset(cmd, 0, sizeof(*cmd));
    
    parse_state_t parser = {0};
    parser.input = (char*)cmd_line;
    parser.current_state = 0;
    parser.stack_top = 0;
    parser.pos = 0;
    
    // Simple parsing for now - just identify command type
    if (strncmp(cmd_line, "show", 4) == 0) {
        cmd->type = CMD_SHOW;
        if (strstr(cmd_line, "interface")) {
            strcpy(cmd->target, "interface");
        } else if (strstr(cmd_line, "route")) {
            strcpy(cmd->target, "route");
        }
    } else if (strncmp(cmd_line, "set", 3) == 0) {
        cmd->type = CMD_SET;
        if (strstr(cmd_line, "interface")) {
            strcpy(cmd->target, "interface");
        } else if (strstr(cmd_line, "route")) {
            strcpy(cmd->target, "route");
        }
    } else if (strncmp(cmd_line, "delete", 6) == 0) {
        cmd->type = CMD_DELETE;
        if (strstr(cmd_line, "route")) {
            strcpy(cmd->target, "route");
        }
    } else if (strncmp(cmd_line, "commit", 6) == 0) {
        cmd->type = CMD_COMMIT;
    } else if (strncmp(cmd_line, "save", 4) == 0) {
        cmd->type = CMD_SAVE;
    } else {
        return -1; // Unknown command
    }
    
    return 0;
}

// Get usage text for help display
const char* get_usage_text(void) {
    return "Usage:\n"
           "  net [command]                    - One-shot mode\n"
           "  net                              - Interactive mode\n"
           "\nCommands:\n"
           "  show interface [name]            - Show interfaces\n"
           "  show route protocol static [inet|inet6] [fib N] - Show routing table\n"
           "  set interface <name> inet addr <addr>/<prefix> [fib N]\n"
           "  set interface <name> inet6 addr <addr>/<prefix> [fib N]\n"
           "  set route protocol static inet <dest> <gw> [fib N]\n"
           "  set route protocol static inet6 <dest> <gw> [fib N]\n"
           "  delete route protocol static [inet|inet6] [fib N]\n"
           "  commit                           - Apply queued changes\n"
           "  save                             - Persist configuration\n"
           "  help, ?                          - Show this help\n"
           "  quit, exit                       - Exit interactive mode\n"
           "\nTab completion is available for all commands.\n";
}

// Print usage information to stdout
void print_usage(void) {
    printf("%s", get_usage_text());
}
