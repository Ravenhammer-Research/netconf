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

// Initialize route table
static int route_table_init(route_table_t *table, int capacity) {
    table->entries = malloc(capacity * sizeof(route_entry_t));
    if (!table->entries) {
            return -1;
        }
    table->count = 0;
    table->capacity = capacity;
    return 0;
}

// Free route table
static void route_table_free(route_table_t *table) {
    if (table->entries) {
        free(table->entries);
        table->entries = NULL;
    }
    table->count = 0;
    table->capacity = 0;
}

// Add route entry to table
static int route_table_add(route_table_t *table, const route_entry_t *entry) {
    if (table->count >= table->capacity) {
            return -1;
        }
    table->entries[table->count] = *entry;
    table->count++;
    return 0;
}

// Initialize route entry with defaults
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

// Parse IPv4 address from sockaddr
static int parse_ipv4_addr(const struct sockaddr *sa, char *addr_str, size_t addr_len) {
    if (sa->sa_family != AF_INET) {
        return -1;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in*)sa;
    return inet_ntop(AF_INET, &sin->sin_addr, addr_str, addr_len) ? 0 : -1;
}

// Parse IPv6 address from sockaddr
static int parse_ipv6_addr(const struct sockaddr *sa, char *addr_str, size_t addr_len) {
    if (sa->sa_family != AF_INET6) {
            return -1;
        }
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)sa;
    return inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, addr_len) ? 0 : -1;
}

// Parse interface name from sockaddr
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

// Build flags string from route flags
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

// Parse route message and extract route entry
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

// Get route entries from kernel via sysctl
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
    mib[6] = fib_filter >= 0 ? fib_filter : 0;
    
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
        if (parse_route_message(rtm, &entry, fib_filter) == 0) {
            if (route_table_add(table, &entry) != 0) {
                break; // Table full
            }
        }
    }
    
    free(buf);
    return table->count;
}

// Check if route matches protocol filter
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

// Check if route matches family filter
static int route_matches_family(const route_entry_t *route, int family_filter) {
    if (family_filter == AF_UNSPEC) {
        return 1; // No filter, match all
    }
    return route->family == family_filter;
}

// Filter routes by protocol and family
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

// Write table header
static int write_route_header(char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "%-25s %-25s %-8s %-8s\n",
                   "Destination", "Gateway", "Flags", "Netif");
}

// Write FIB info
static int write_fib_info(char *buffer, size_t buffer_len, int fib) {
    return snprintf(buffer, buffer_len, "\nFIB %d:\n\n", fib);
}

// Write route entry as table row
static int write_route_row(const route_entry_t *route, char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "%-25s %-25s %-8s %-8s\n",
                   route->dest, route->gateway, route->flags, route->netif);
}

// Main function to show routes
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
    
    // Write FIB info first (before table header)
    if (table.count > 0 || fib_filter >= 0) {
        int actual_fib = fib_filter >= 0 ? fib_filter : 0;
        int written = write_fib_info(resp_ptr, remaining, actual_fib);
        if (written < 0 || (size_t)written >= remaining) {
            route_table_free(&table);
            return -1;
        }
        resp_ptr += written;
        remaining -= written;
    }
    
    // Write table header
    int written = write_route_header(resp_ptr, remaining);
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
    return 0;
}

// Public interface functions

int show_routes(char *response, size_t resp_len, int fib_filter, 
                const char *protocol_filter, int family_filter) {
    return show_routes_internal(response, resp_len, fib_filter, protocol_filter, family_filter);
}

// Legacy functions for compatibility
int configure_route(const route_config_t *config) {
    (void)config; // Suppress unused parameter warning
    // TODO: Implement route configuration
    return 0;
}

int remove_route(const route_config_t *config) {
    (void)config; // Suppress unused parameter warning
    // TODO: Implement route removal
    return 0;
} 