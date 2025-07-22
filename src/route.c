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

// Simple route entry structure
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

// Get route entries from kernel
static int get_route_entries(route_entry_t *routes, int max_routes, int fib_filter) {
    int mib[7];
    size_t needed;
    char *buf, *next, *lim;
    struct rt_msghdr *rtm;
    struct sockaddr *sa;
    int count = 0;
    
    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;        /* protocol */
    mib[3] = AF_UNSPEC;
    mib[4] = NET_RT_DUMP;
    mib[5] = 0;        /* no flags */
    mib[6] = fib_filter >= 0 ? fib_filter : 0;
    
    if (sysctl(mib, 7, NULL, &needed, NULL, 0) < 0) {
        return -1;
    }
    
    buf = malloc(needed);
    if (buf == NULL) {
        return -1;
    }
    
    if (sysctl(mib, 7, buf, &needed, NULL, 0) < 0) {
        free(buf);
        return -1;
    }
    
    lim = buf + needed;
    for (next = buf; next < lim && count < max_routes; next += rtm->rtm_msglen) {
        rtm = (struct rt_msghdr *)next;
        if (rtm->rtm_type == RTM_GET) {
            route_entry_t *route = &routes[count];
            
            // Initialize route entry
            memset(route, 0, sizeof(route_entry_t));
            strcpy(route->dest, "-");
            strcpy(route->gateway, "-");
            strcpy(route->netif, "-");
            route->fib = fib_filter >= 0 ? fib_filter : 0;
            route->is_static = (rtm->rtm_flags & RTF_STATIC) ? 1 : 0;
            route->is_dynamic = (rtm->rtm_flags & RTF_DYNAMIC) ? 1 : 0;
            
            // Parse addresses
            sa = (struct sockaddr *)(rtm + 1);
            for (int i = 0; i < RTAX_MAX; i++) {
                if (rtm->rtm_addrs & (1 << i)) {
                    if (i == RTAX_DST) {
                        if (sa->sa_family == AF_INET) {
                            struct sockaddr_in *sin = (struct sockaddr_in*)sa;
                            inet_ntop(AF_INET, &sin->sin_addr, route->dest, sizeof(route->dest));
                            route->family = AF_INET;
                        } else if (sa->sa_family == AF_INET6) {
                            struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
                            inet_ntop(AF_INET6, &sin6->sin6_addr, route->dest, sizeof(route->dest));
                            route->family = AF_INET6;
                        }
                    } else if (i == RTAX_GATEWAY) {
                        if (sa->sa_family == AF_INET) {
                            struct sockaddr_in *sin = (struct sockaddr_in*)sa;
                            inet_ntop(AF_INET, &sin->sin_addr, route->gateway, sizeof(route->gateway));
                        } else if (sa->sa_family == AF_INET6) {
                            struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
                            inet_ntop(AF_INET6, &sin6->sin6_addr, route->gateway, sizeof(route->gateway));
                        }
                    } else if (i == RTAX_IFP) {
                        if (sa->sa_family == AF_LINK) {
                            struct sockaddr_dl *sdl = (struct sockaddr_dl*)sa;
                            if (sdl->sdl_nlen > 0) {
                                strncpy(route->netif, sdl->sdl_data, sdl->sdl_nlen);
                                route->netif[sdl->sdl_nlen] = '\0';
                            }
                        }
                    }
                    sa = (struct sockaddr *)((char *)sa + sa->sa_len);
                }
            }
            
            // Build flags string
            if (rtm->rtm_flags & RTF_UP) strcat(route->flags, "U");
            if (rtm->rtm_flags & RTF_GATEWAY) strcat(route->flags, "G");
            if (rtm->rtm_flags & RTF_HOST) strcat(route->flags, "H");
            if (rtm->rtm_flags & RTF_REJECT) strcat(route->flags, "R");
            if (rtm->rtm_flags & RTF_DYNAMIC) strcat(route->flags, "D");
            if (rtm->rtm_flags & RTF_MODIFIED) strcat(route->flags, "M");
            if (rtm->rtm_flags & RTF_STATIC) strcat(route->flags, "S");
            if (rtm->rtm_flags & RTF_BLACKHOLE) strcat(route->flags, "B");
            
            // Get interface name from index if not already set
            if (strcmp(route->netif, "-") == 0 && rtm->rtm_index > 0) {
                char ifname[IF_NAMESIZE];
                if (if_indextoname(rtm->rtm_index, ifname) != NULL) {
                    strncpy(route->netif, ifname, sizeof(route->netif) - 1);
                }
            }
            
            count++;
        }
    }
    
    free(buf);
    return count;
}

// Filter routes by protocol and family
static int filter_routes(route_entry_t *routes, int count, const char *protocol, int family_filter) {
    int filtered_count = 0;
    
    for (int i = 0; i < count; i++) {
        route_entry_t *route = &routes[i];
        int keep = 1;
        
        // Filter by protocol
        if (protocol && strlen(protocol) > 0) {
            if (strcmp(protocol, "static") == 0) {
                if (!route->is_static) keep = 0;
            } else if (strcmp(protocol, "dynamic") == 0) {
                if (!route->is_dynamic) keep = 0;
            }
        }
        
        // Filter by family
        if (family_filter != AF_UNSPEC) {
            if (route->family != family_filter) keep = 0;
        }
        
        // Keep the route if it passes all filters
        if (keep) {
            if (filtered_count != i) {
                routes[filtered_count] = routes[i];
            }
            filtered_count++;
        }
    }
    
    return filtered_count;
}

int show_routes(char *response, size_t resp_len, int fib_filter, const char *protocol_filter, int family_filter) {
    route_entry_t routes[1024]; // Max 1024 routes
    int count, filtered_count;
    char *resp_ptr = response;
    size_t remaining = resp_len;
    
    // Get all routes from the specified FIB
    count = get_route_entries(routes, 1024, fib_filter);
    if (count < 0) {
        snprintf(response, resp_len, "Error: Unable to get routing table\n");
        return -1;
    }
    
    // Filter routes by protocol and family
    filtered_count = filter_routes(routes, count, protocol_filter, family_filter);
    
    // Print header
    snprintf(resp_ptr, remaining, "%-25s %-25s %-8s %-8s\n",
             "Destination", "Gateway", "Flags", "Netif");
    resp_ptr += strlen(resp_ptr);
    remaining = resp_len - (resp_ptr - response);
    
    // Print FIB info
    int actual_fib = fib_filter >= 0 ? fib_filter : 0;
    snprintf(resp_ptr, remaining, "FIB %d:\n", actual_fib);
    resp_ptr += strlen(resp_ptr);
    remaining = resp_len - (resp_ptr - response);
    
    // Print filtered routes
    for (int i = 0; i < filtered_count; i++) {
        route_entry_t *route = &routes[i];
        snprintf(resp_ptr, remaining, "%-25s %-25s %-8s %-8s\n",
                 route->dest, route->gateway, route->flags, route->netif);
        resp_ptr += strlen(resp_ptr);
        remaining = resp_len - (resp_ptr - response);
        
        if (remaining <= 0) break;
    }
    
    return 0;
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