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
 * @file route.c
 * @brief Route management functionality for netd
 * 
 * This file implements comprehensive route management capabilities including:
 * - Querying routing tables via sysctl interface
 * - Filtering routes by protocol (static/dynamic) and address family
 * - Adding and removing static routes
 * - FIB (Forwarding Information Base) support
 * - Formatted output generation for CLI display
 * 
 * The implementation uses FreeBSD's routing socket API and sysctl interface
 * to interact with the kernel routing subsystem. It supports both IPv4 and
 * IPv6 routes across multiple FIBs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/route.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"

// Route entry structure
typedef struct {
    char dest[64];
    char gateway[64];
    char flags[16];
    char netif[16];
    int fib;
    int is_static;
    int is_dynamic;
    int family; // AF_INET or AF_INET6
} route_entry_t;

// Route table structure
typedef struct {
    route_entry_t *entries;
    int count;
    int capacity;
} route_table_t;

/**
 * Initialize a route table with specified capacity
 * @param table Pointer to route table structure to initialize
 * @param capacity Maximum number of route entries the table can hold
 * @return 0 on success, -1 on failure (memory allocation error)
 */
static int route_table_init(route_table_t *table, int capacity) {
    table->entries = malloc(capacity * sizeof(route_entry_t));
    if (!table->entries) {
            return -1;
        }
    table->count = 0;
    table->capacity = capacity;
    return 0;
}

/**
 * Free memory allocated for route table and reset counters
 * @param table Pointer to route table structure to free
 */
static void route_table_free(route_table_t *table) {
    if (table->entries) {
        free(table->entries);
        table->entries = NULL;
    }
    table->count = 0;
    table->capacity = 0;
}

/**
 * Add a route entry to the route table
 * @param table Pointer to route table to add entry to
 * @param entry Pointer to route entry to add
 * @return 0 on success, -1 if table is full
 */
static int route_table_add(route_table_t *table, const route_entry_t *entry) {
    if (table->count >= table->capacity) {
            return -1;
        }
    table->entries[table->count] = *entry;
    table->count++;
    return 0;
}

/**
 * Initialize a route entry with default values
 * @param entry Pointer to route entry structure to initialize
 */
static void route_entry_init(route_entry_t *entry) {
    memset(entry, 0, sizeof(route_entry_t));
    strcpy(entry->dest, "-");
    strcpy(entry->gateway, "-");
    strcpy(entry->netif, "-");
    entry->fib = 0;
    entry->is_static = 0;
    entry->is_dynamic = 0;
    entry->family = AF_UNSPEC;
}

/**
 * Parse IPv4 address from sockaddr structure
 * @param sa Pointer to sockaddr structure to parse
 * @param addr_str Buffer to store the parsed IPv4 address string
 * @param addr_len Size of the address string buffer
 * @return 0 on success, -1 on failure (wrong family or parsing error)
 */
static int parse_ipv4_addr(const struct sockaddr *sa, char *addr_str, size_t addr_len) {
    if (sa->sa_family != AF_INET) {
        return -1;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in*)sa;
    return inet_ntop(AF_INET, &sin->sin_addr, addr_str, addr_len) ? 0 : -1;
}

/**
 * Parse IPv6 address from sockaddr structure
 * @param sa Pointer to sockaddr structure to parse
 * @param addr_str Buffer to store the parsed IPv6 address string
 * @param addr_len Size of the address string buffer
 * @return 0 on success, -1 on failure (wrong family or parsing error)
 */
static int parse_ipv6_addr(const struct sockaddr *sa, char *addr_str, size_t addr_len) {
    if (sa->sa_family != AF_INET6) {
            return -1;
        }
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)sa;
    return inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, addr_len) ? 0 : -1;
}

/**
 * Parse interface name from sockaddr_dl structure
 * @param sa Pointer to sockaddr structure (must be AF_LINK)
 * @param ifname Buffer to store the interface name
 * @param ifname_len Size of the interface name buffer
 * @return 0 on success, -1 on failure (wrong family or invalid data)
 */
static int parse_interface_name(const struct sockaddr *sa, char *ifname, size_t ifname_len) {
    if (sa->sa_family != AF_LINK) {
        return -1;
    }
    const struct sockaddr_dl *sdl = (const struct sockaddr_dl*)sa;
    if (sdl->sdl_nlen > 0 && sdl->sdl_nlen < ifname_len) {
        strncpy(ifname, sdl->sdl_data, sdl->sdl_nlen);
        ifname[sdl->sdl_nlen] = '\0';
        return 0;
    }
    return -1;
}

/**
 * Build a flags string from route message flags
 * @param rtm_flags Route message flags from rt_msghdr
 * @param flags_str Buffer to store the resulting flags string
 * @param flags_len Size of the flags string buffer
 */
static void build_flags_string(uint32_t rtm_flags, char *flags_str, size_t flags_len) {
    if (flags_len == 0) return;
    
    flags_str[0] = '\0';
    size_t current_len = 0;
    
    if (rtm_flags & RTF_UP && current_len + 1 < flags_len) {
        strcat(flags_str, "U");
        current_len++;
    }
    if (rtm_flags & RTF_GATEWAY && current_len + 1 < flags_len) {
        strcat(flags_str, "G");
        current_len++;
    }
    if (rtm_flags & RTF_HOST && current_len + 1 < flags_len) {
        strcat(flags_str, "H");
        current_len++;
    }
    if (rtm_flags & RTF_REJECT && current_len + 1 < flags_len) {
        strcat(flags_str, "R");
        current_len++;
    }
    if (rtm_flags & RTF_DYNAMIC && current_len + 1 < flags_len) {
        strcat(flags_str, "D");
        current_len++;
    }
    if (rtm_flags & RTF_MODIFIED && current_len + 1 < flags_len) {
        strcat(flags_str, "M");
        current_len++;
    }
    if (rtm_flags & RTF_STATIC && current_len + 1 < flags_len) {
        strcat(flags_str, "S");
        current_len++;
    }
    if (rtm_flags & RTF_BLACKHOLE && current_len + 1 < flags_len) {
        strcat(flags_str, "B");
        current_len++;
    }
}

/**
 * Parse a route message and extract route entry information
 * @param rtm Pointer to route message header
 * @param entry Pointer to route entry structure to populate
 * @param fib FIB number to associate with this route entry
 * @return 0 on success, -1 on failure (invalid message type)
 */
static int parse_route_message(const struct rt_msghdr *rtm, route_entry_t *entry, int fib) {
    if (rtm->rtm_type != RTM_GET) {
        return -1;
    }
    
    // Initialize entry
    route_entry_init(entry);
    entry->fib = fib;
    entry->is_static = (rtm->rtm_flags & RTF_STATIC) ? 1 : 0;
    entry->is_dynamic = (rtm->rtm_flags & RTF_DYNAMIC) ? 1 : 0;
    
    // Build flags string
    build_flags_string(rtm->rtm_flags, entry->flags, sizeof(entry->flags));
    
    // Parse addresses
    const struct sockaddr *sa = (const struct sockaddr *)(rtm + 1);
    for (int i = 0; i < RTAX_MAX; i++) {
        if (rtm->rtm_addrs & (1 << i)) {
            switch (i) {
                case RTAX_DST:
                    if (parse_ipv4_addr(sa, entry->dest, sizeof(entry->dest)) == 0) {
                        entry->family = AF_INET;
                    } else if (parse_ipv6_addr(sa, entry->dest, sizeof(entry->dest)) == 0) {
                        entry->family = AF_INET6;
                    }
                    break;
                    
                case RTAX_GATEWAY:
                    parse_ipv4_addr(sa, entry->gateway, sizeof(entry->gateway));
                    parse_ipv6_addr(sa, entry->gateway, sizeof(entry->gateway));
                    break;
                    
                case RTAX_NETMASK:
                    // Calculate prefix length from netmask
                    if (sa->sa_family == AF_INET) {
                        const struct sockaddr_in *sin = (const struct sockaddr_in*)sa;
                        uint32_t netmask = ntohl(sin->sin_addr.s_addr);
                        int prefix_len = 0;
                        while (netmask & 0x80000000) {
                            prefix_len++;
                            netmask <<= 1;
                        }
                        // Add prefix length to destination if it's not /32 (host route)
                        if (prefix_len != 32 && entry->family == AF_INET) {
                            char prefix_str[8];
                            snprintf(prefix_str, sizeof(prefix_str), "/%d", prefix_len);
                            strncat(entry->dest, prefix_str, sizeof(entry->dest) - strlen(entry->dest) - 1);
                        }
                    } else if (sa->sa_family == AF_INET6) {
                        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)sa;
                        int prefix_len = 0;
                        for (int i = 0; i < 16; i++) {
                            uint8_t byte = sin6->sin6_addr.s6_addr[i];
                            if (byte == 0xFF) {
                                prefix_len += 8;
                            } else {
                                while (byte & 0x80) {
                                    prefix_len++;
                                    byte <<= 1;
                                }
                                break;
                            }
                        }
                        // Add prefix length to destination if it's not /128 (host route)
                        if (prefix_len != 128 && entry->family == AF_INET6) {
                            char prefix_str[8];
                            snprintf(prefix_str, sizeof(prefix_str), "/%d", prefix_len);
                            strncat(entry->dest, prefix_str, sizeof(entry->dest) - strlen(entry->dest) - 1);
                        }
                    }
                    break;
                    
                case RTAX_IFP:
                    parse_interface_name(sa, entry->netif, sizeof(entry->netif));
                    break;
            }
            sa = (const struct sockaddr *)((const char *)sa + sa->sa_len);
        }
    }
    
    // Get interface name from index if not already set
    if (strcmp(entry->netif, "-") == 0 && rtm->rtm_index > 0) {
        char ifname[IF_NAMESIZE];
        if (if_indextoname(rtm->rtm_index, ifname) != NULL) {
            strncpy(entry->netif, ifname, sizeof(entry->netif) - 1);
        }
    }
    
    return 0;
}

/**
 * Get route entries from kernel via sysctl interface
 * @param table Pointer to route table to populate with entries
 * @param fib_filter FIB number to filter routes (-1 for all FIBs)
 * @return Number of routes found, -1 on error
 */
static int get_route_entries_sysctl(route_table_t *table, int fib_filter) {
    int mib[7];
    size_t needed;
    char *buf, *next, *lim;
    struct rt_msghdr *rtm;
    
    // Setup sysctl parameters
    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;        /* protocol */
    mib[3] = AF_UNSPEC;
    mib[4] = NET_RT_DUMP;
    mib[5] = 0;        /* no flags */
    
    // Always query specific FIB (default to 0 if not specified)
    int fib = (fib_filter >= 0) ? fib_filter : 0;
    mib[6] = fib;
    
    // Get required buffer size
    if (sysctl(mib, 7, NULL, &needed, NULL, 0) < 0) {
        return -1;
    }
    
    // Allocate buffer
    buf = malloc(needed);
    if (!buf) {
        return -1;
    }
    
    // Get route data
    if (sysctl(mib, 7, buf, &needed, NULL, 0) < 0) {
        free(buf);
        return -1;
    }
    
    // Parse route messages
    lim = buf + needed;
    for (next = buf; next < lim; next += rtm->rtm_msglen) {
        rtm = (struct rt_msghdr *)next;
        
        route_entry_t entry;
        if (parse_route_message(rtm, &entry, fib) == 0) {
            if (route_table_add(table, &entry) != 0) {
                break; // Table full
            }
        }
    }
    
    free(buf);
    return table->count;
}

/**
 * Check if a route matches the specified protocol filter
 * @param route Pointer to route entry to check
 * @param protocol Protocol filter string ("static", "dynamic", or NULL for all)
 * @return 1 if route matches filter, 0 otherwise
 */
static int route_matches_protocol(const route_entry_t *route, const char *protocol) {
    if (!protocol || strlen(protocol) == 0) {
        return 1; // No filter, match all
    }
    
    if (strcmp(protocol, "static") == 0) {
        return route->is_static;
    } else if (strcmp(protocol, "dynamic") == 0) {
        return route->is_dynamic;
    }
    
    return 0;
}

/**
 * Check if a route matches the specified address family filter
 * @param route Pointer to route entry to check
 * @param family_filter Address family filter (AF_INET, AF_INET6, or AF_UNSPEC for all)
 * @return 1 if route matches filter, 0 otherwise
 */
static int route_matches_family(const route_entry_t *route, int family_filter) {
    if (family_filter == AF_UNSPEC) {
        return 1; // No filter, match all
    }
    return route->family == family_filter;
}

/**
 * Filter routes in table by protocol and address family
 * @param table Pointer to route table to filter (modified in place)
 * @param protocol Protocol filter string ("static", "dynamic", or NULL for all)
 * @param family_filter Address family filter (AF_INET, AF_INET6, or AF_UNSPEC for all)
 * @return Number of routes remaining after filtering
 */
static int filter_routes(route_table_t *table, const char *protocol, int family_filter) {
    int filtered_count = 0;
    
    for (int i = 0; i < table->count; i++) {
        route_entry_t *route = &table->entries[i];
        
        if (route_matches_protocol(route, protocol) && 
            route_matches_family(route, family_filter)) {
            // Keep this route
            if (filtered_count != i) {
                table->entries[filtered_count] = table->entries[i];
            }
            filtered_count++;
        }
    }
    
    table->count = filtered_count;
    return filtered_count;
}

/**
 * Write the routing tables header to buffer
 * @param buffer Buffer to write to
 * @param buffer_len Size of buffer
 * @return Number of bytes written, or -1 on error
 */
static int write_routing_tables_header(char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "Routing tables\n");
}

/**
 * Write the route table column headers to buffer
 * @param buffer Buffer to write to
 * @param buffer_len Size of buffer
 * @return Number of bytes written, or -1 on error
 */
static int write_route_header(char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "%-25s %-18s %-8s %-8s %s\n",
                   "Destination", "Gateway", "Flags", "Netif", "Expire");
}

/**
 * Write FIB information line to buffer
 * @param buffer Buffer to write to
 * @param buffer_len Size of buffer
 * @param fib FIB number to display
 * @return Number of bytes written, or -1 on error
 */
static int write_fib_info(char *buffer, size_t buffer_len, int fib) {
    return snprintf(buffer, buffer_len, "FIB: %d\n", fib);
}

/**
 * Write a single route entry as a formatted table row
 * @param route Pointer to route entry to write
 * @param buffer Buffer to write to
 * @param buffer_len Size of buffer
 * @return Number of bytes written, or -1 on error
 */
static int write_route_row(const route_entry_t *route, char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "%-25s %-18s %-8s %-8s %s\n",
                   route->dest, route->gateway, route->flags, route->netif, "");
}

/**
 * Internal function to generate route table output
 * @param response Buffer to store the formatted route table output
 * @param resp_len Size of the response buffer
 * @param fib_filter FIB number to filter routes (-1 for all FIBs)
 * @param protocol_filter Protocol filter string ("static", "dynamic", or NULL for all)
 * @param family_filter Address family filter (AF_INET, AF_INET6, or AF_UNSPEC for all)
 * @return Number of bytes written to response buffer, -1 on error
 */
static int show_routes_internal(char *response, size_t resp_len, int fib_filter, 
                               const char *protocol_filter, int family_filter) {
    route_table_t table;
    char *resp_ptr = response;
    size_t remaining = resp_len;
    
    // Initialize route table
    if (route_table_init(&table, 1024) != 0) {
        snprintf(response, resp_len, "Error: Unable to allocate route table\n");
        return -1;
    }
    
    // Get route entries
    int count = get_route_entries_sysctl(&table, fib_filter);
    if (count < 0) {
        route_table_free(&table);
        snprintf(response, resp_len, "Error: Unable to get routing table\n");
        return -1;
    }
    
    // Filter routes
    filter_routes(&table, protocol_filter, family_filter);
    
    // Write routing tables header
    int written = write_routing_tables_header(resp_ptr, remaining);
    if (written < 0 || (size_t)written >= remaining) {
        route_table_free(&table);
        return -1;
    }
    resp_ptr += written;
    remaining -= written;
    
    // Write FIB info if we have routes or if a specific FIB was requested
    if (table.count > 0 || fib_filter >= 0) {
        int actual_fib = fib_filter >= 0 ? fib_filter : 0;
        written = write_fib_info(resp_ptr, remaining, actual_fib);
        if (written < 0 || (size_t)written >= remaining) {
            route_table_free(&table);
            return -1;
        }
        resp_ptr += written;
        remaining -= written;
    }
    
    // Write table header
    written = write_route_header(resp_ptr, remaining);
    if (written < 0 || (size_t)written >= remaining) {
        route_table_free(&table);
        return -1;
    }
    resp_ptr += written;
    remaining -= written;
    
    // Write route entries
    for (int i = 0; i < table.count; i++) {
        written = write_route_row(&table.entries[i], resp_ptr, remaining);
        if (written < 0 || (size_t)written >= remaining) {
            break;
        }
        resp_ptr += written;
        remaining -= written;
    }
    
    route_table_free(&table);
    
    // Calculate actual response length
    size_t actual_len = resp_len - remaining;
    
    // Ensure response is null-terminated
    if (actual_len < resp_len) {
        response[actual_len] = '\0';
    }
    
    return actual_len;
}

/**
 * Public interface to display routing table information
 * @param response Buffer to store the formatted route table output
 * @param resp_len Size of the response buffer
 * @param fib_filter FIB number to filter routes (-1 for all FIBs)
 * @param protocol_filter Protocol filter string ("static", "dynamic", or NULL for all)
 * @param family_filter Address family filter (AF_INET, AF_INET6, or AF_UNSPEC for all)
 * @return Number of bytes written to response buffer, -1 on error
 */
int show_routes(char *response, size_t resp_len, int fib_filter, 
                const char *protocol_filter, int family_filter) {
    return show_routes_internal(response, resp_len, fib_filter, protocol_filter, family_filter);
}

/**
 * Configure (add) a static route to the routing table
 * @param config Pointer to route configuration structure containing route details
 * @return 0 on success, -1 on failure
 */
int configure_route(const route_config_t *config) {
    if (!config) return -1;
    
    int sock = socket(AF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) return -1;
    
    struct {
        struct rt_msghdr rtm;
        struct sockaddr_in dst;
        struct sockaddr_in gw;
        struct sockaddr_in netmask;
    } msg;
    
    memset(&msg, 0, sizeof(msg));
    
    // Setup route message header
    msg.rtm.rtm_type = RTM_ADD;
    msg.rtm.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
    msg.rtm.rtm_version = RTM_VERSION;
    msg.rtm.rtm_seq = 1;
    msg.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    msg.rtm.rtm_pid = getpid();
    
    // Set destination address
    msg.dst.sin_family = AF_INET;
    msg.dst.sin_len = sizeof(struct sockaddr_in);
    if (config->family == ADDR_FAMILY_INET6) {
        // Convert IPv6 to IPv4-mapped IPv6 for routing socket
        msg.dst.sin_addr.s_addr = config->dest6.s6_addr32[3];
    } else {
        msg.dst.sin_addr = config->dest;
    }
    
    // Set gateway address
    msg.gw.sin_family = AF_INET;
    msg.gw.sin_len = sizeof(struct sockaddr_in);
    if (config->family == ADDR_FAMILY_INET6) {
        // Convert IPv6 to IPv4-mapped IPv6 for routing socket
        msg.gw.sin_addr.s_addr = config->gw6.s6_addr32[3];
    } else {
        msg.gw.sin_addr = config->gw;
    }
    
    // Set netmask based on prefix length
    uint32_t netmask = 0xFFFFFFFF << (32 - config->prefix_len);
    msg.netmask.sin_family = AF_INET;
    msg.netmask.sin_len = sizeof(struct sockaddr_in);
    msg.netmask.sin_addr.s_addr = htonl(netmask);
    
    // Set FIB if specified (FreeBSD doesn't have rtm_fib, use setsockopt)
    if (config->fib >= 0) {
        setsockopt(sock, SOL_SOCKET, SO_SETFIB, &config->fib, sizeof(config->fib));
    }
    
    int result = write(sock, &msg, sizeof(msg));
    close(sock);
    
    return (result < 0) ? -1 : 0;
}

/**
 * Remove a static route from the routing table
 * @param config Pointer to route configuration structure containing route details
 * @return 0 on success, -1 on failure
 */
int remove_route(const route_config_t *config) {
    if (!config) return -1;
    
    int sock = socket(AF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) return -1;
    
    struct {
        struct rt_msghdr rtm;
        struct sockaddr_in dst;
        struct sockaddr_in gw;
        struct sockaddr_in netmask;
    } msg;
    
    memset(&msg, 0, sizeof(msg));
    
    // Setup route message header
    msg.rtm.rtm_type = RTM_DELETE;
    msg.rtm.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
    msg.rtm.rtm_version = RTM_VERSION;
    msg.rtm.rtm_seq = 1;
    msg.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    msg.rtm.rtm_pid = getpid();
    
    // Set destination address
    msg.dst.sin_family = AF_INET;
    msg.dst.sin_len = sizeof(struct sockaddr_in);
    if (config->family == ADDR_FAMILY_INET6) {
        // Convert IPv6 to IPv4-mapped IPv6 for routing socket
        msg.dst.sin_addr.s_addr = config->dest6.s6_addr32[3];
    } else {
        msg.dst.sin_addr = config->dest;
    }
    
    // Set gateway address
    msg.gw.sin_family = AF_INET;
    msg.gw.sin_len = sizeof(struct sockaddr_in);
    if (config->family == ADDR_FAMILY_INET6) {
        // Convert IPv6 to IPv4-mapped IPv6 for routing socket
        msg.gw.sin_addr.s_addr = config->gw6.s6_addr32[3];
    } else {
        msg.gw.sin_addr = config->gw;
    }
    
    // Set netmask based on prefix length
    uint32_t netmask = 0xFFFFFFFF << (32 - config->prefix_len);
    msg.netmask.sin_family = AF_INET;
    msg.netmask.sin_len = sizeof(struct sockaddr_in);
    msg.netmask.sin_addr.s_addr = htonl(netmask);
    
    // Set FIB if specified (FreeBSD doesn't have rtm_fib, use setsockopt)
    if (config->fib >= 0) {
        setsockopt(sock, SOL_SOCKET, SO_SETFIB, &config->fib, sizeof(config->fib));
    }
    
    int result = write(sock, &msg, sizeof(msg));
    close(sock);
    
    return (result < 0) ? -1 : 0;
} 