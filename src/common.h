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
 * @file common.h
 * @brief Common definitions and declarations for netd
 * 
 * This header file contains shared definitions, data structures, constants,
 * and function prototypes used throughout the netd project. It includes:
 * 
 * - System includes for networking, sockets, and system calls
 * - Constants for socket paths and buffer sizes
 * - Enumerations for command types and address families
 * - Data structures for interface and route configurations
 * - Function prototypes for all major subsystems
 * 
 * This file should be included by all source files in the netd project
 * to ensure consistent type definitions and API declarations.
 */

#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <libnetconf2/netconf.h>

#define SOCKET_PATH "/var/run/netd.sock"
#define MAX_CMD_LEN 1024
#define MAX_RESPONSE_LEN 16384

// Command types
typedef enum {
    CMD_SHOW,
    CMD_SET,
    CMD_DELETE,
    CMD_COMMIT,
    CMD_DISCARD,
    CMD_SAVE,
    CMD_UNKNOWN
} cmd_type_t;

// Address family
typedef enum {
    ADDR_FAMILY_INET4,
    ADDR_FAMILY_INET6
} addr_family_t;

// Interface configuration
typedef struct {
    char name[IF_NAMESIZE];
    addr_family_t family;
    struct in_addr addr;
    struct in6_addr addr6;
    int prefix_len;
    int fib;
    int tunnel_fib;  // FreeBSD-specific tunnel VRF/FIB
} if_config_t;

// Route configuration
typedef struct {
    addr_family_t family;
    struct in_addr dest;
    struct in6_addr dest6;
    struct in_addr gw;
    struct in6_addr gw6;
    int prefix_len;
    int fib;
} route_config_t;

// Command structure
typedef struct {
    cmd_type_t type;
    char target[64];  // "interface", "route", etc.
    char subtype[64]; // "ethernet", "static", etc.
    char name[IF_NAMESIZE]; // interface name or route identifier
    addr_family_t family;
    int fib;
    union {
        if_config_t if_config;
        route_config_t route_config;
    } data;
} command_t;

// Function prototypes
int parse_command(const char *cmd_line, command_t *cmd);
int execute_command(command_t *cmd, char *response, size_t resp_len);
const char* get_usage_text(void);
void print_usage(void);
int show_interfaces_filtered(char *response, size_t resp_len, const char *filter);

// Configuration file handling
int save_configuration(void);
int load_configuration(void);

// NETCONF function prototypes
int parse_netconf_message(const char *xml_msg, command_t *cmd);
int handle_netconf_get_config(const char *filter, char *response, size_t resp_len);
int handle_netconf_edit_config(const char *config, char *response, size_t resp_len);
int handle_netconf_commit(char *response, size_t resp_len);

// YANG/NETCONF function prototypes
int init_yang_context(void);
void cleanup_yang_context(void);
int handle_netconf_get_config_yang(const char *filter, char *response, size_t resp_len);
int handle_netconf_edit_config_yang(const char *config_xml, char *response, size_t resp_len);

#endif // COMMON_H 