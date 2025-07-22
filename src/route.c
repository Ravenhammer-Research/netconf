#include "common.h"
#include <net/if_dl.h>

static int add_route(const route_config_t *config) {
    int sock = socket(AF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) return -1;
    
    // Set FIB if specified (0 is default FIB)
    if (config->fib > 0) {
        if (setfib(config->fib) < 0) {
            close(sock);
            return -1;
        }
    }
    
    struct {
        struct rt_msghdr rtm;
        char buf[1024];
    } msg;
    
    memset(&msg, 0, sizeof(msg));
    msg.rtm.rtm_type = RTM_ADD;
    msg.rtm.rtm_flags = RTF_UP | RTF_GATEWAY;
    msg.rtm.rtm_version = RTM_VERSION;
    msg.rtm.rtm_seq = 1;
    msg.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY;
    
    // Note: FIB support in FreeBSD requires different approach
    // For now, we'll use the default FIB (0)
    // In a full implementation, we'd need to use setfib() or similar
    
    char *cp = msg.buf;
    
    if (config->family == ADDR_FAMILY_INET4) {
        // Destination address
        struct sockaddr_in *sin = (struct sockaddr_in*)cp;
        sin->sin_family = AF_INET;
        sin->sin_len = sizeof(*sin);
        sin->sin_addr = config->dest;
        cp += sizeof(*sin);
        
        // Gateway address
        sin = (struct sockaddr_in*)cp;
        sin->sin_family = AF_INET;
        sin->sin_len = sizeof(*sin);
        sin->sin_addr = config->gw;
        cp += sizeof(*sin);
        
        msg.rtm.rtm_msglen = cp - (char*)&msg;
    } else {
        // Destination address
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)cp;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_len = sizeof(*sin6);
        sin6->sin6_addr = config->dest6;
        cp += sizeof(*sin6);
        
        // Gateway address
        sin6 = (struct sockaddr_in6*)cp;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_len = sizeof(*sin6);
        sin6->sin6_addr = config->gw6;
        cp += sizeof(*sin6);
        
        msg.rtm.rtm_msglen = cp - (char*)&msg;
    }
    
    int result = write(sock, &msg, msg.rtm.rtm_msglen);
    close(sock);
    return result;
}

static int delete_route(const route_config_t *config) {
    int sock = socket(AF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) return -1;
    
    // Set FIB if specified (0 is default FIB)
    if (config->fib > 0) {
        if (setfib(config->fib) < 0) {
            close(sock);
            return -1;
        }
    }
    
    struct {
        struct rt_msghdr rtm;
        char buf[1024];
    } msg;
    
    memset(&msg, 0, sizeof(msg));
    msg.rtm.rtm_type = RTM_DELETE;
    msg.rtm.rtm_flags = RTF_UP | RTF_GATEWAY;
    msg.rtm.rtm_version = RTM_VERSION;
    msg.rtm.rtm_seq = 1;
    msg.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY;
    
    char *cp = msg.buf;
    
    if (config->family == ADDR_FAMILY_INET4) {
        // Destination address
        struct sockaddr_in *sin = (struct sockaddr_in*)cp;
        sin->sin_family = AF_INET;
        sin->sin_len = sizeof(*sin);
        sin->sin_addr = config->dest;
        cp += sizeof(*sin);
        
        // Gateway address
        sin = (struct sockaddr_in*)cp;
        sin->sin_family = AF_INET;
        sin->sin_len = sizeof(*sin);
        sin->sin_addr = config->gw;
        cp += sizeof(*sin);
        
        msg.rtm.rtm_msglen = cp - (char*)&msg;
    } else {
        // Destination address
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)cp;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_len = sizeof(*sin6);
        sin6->sin6_addr = config->dest6;
        cp += sizeof(*sin6);
        
        // Gateway address
        sin6 = (struct sockaddr_in6*)cp;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_len = sizeof(*sin6);
        sin6->sin6_addr = config->gw6;
        cp += sizeof(*sin6);
        
        msg.rtm.rtm_msglen = cp - (char*)&msg;
    }
    
    int result = write(sock, &msg, msg.rtm.rtm_msglen);
    close(sock);
    return result;
}

static int delete_all_static_routes(int fib, int family_mask) {
    int sock = socket(AF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) return -1;
    
    // Set FIB if specified (0 is default FIB)
    if (fib > 0) {
        if (setfib(fib) < 0) {
            close(sock);
            return -1;
        }
    }
    
    // Get routing table
    struct {
        struct rt_msghdr rtm;
        char buf[1024];
    } msg;
    
    memset(&msg, 0, sizeof(msg));
    msg.rtm.rtm_type = RTM_GET;
    msg.rtm.rtm_flags = RTF_UP;
    msg.rtm.rtm_version = RTM_VERSION;
    msg.rtm.rtm_seq = 1;
    msg.rtm.rtm_addrs = RTA_DST;
    
    // Request all routes
    if (write(sock, &msg, sizeof(msg.rtm)) < 0) {
        close(sock);
        return -1;
    }
    
    // Read and delete static routes
    while (1) {
        ssize_t n = read(sock, &msg, sizeof(msg));
        if (n <= 0) break;
        
        if (msg.rtm.rtm_type == RTM_GET) {
            // Check if this is a static route (not interface route)
            if (msg.rtm.rtm_flags & RTF_GATEWAY) {
                // Check family filter
                struct sockaddr *sa = (struct sockaddr*)(&msg.rtm + 1);
                int route_family = sa->sa_family;
                
                int should_delete = 0;
                if (family_mask == 0) {
                    // No filter, delete all
                    should_delete = 1;
                } else if (family_mask & 1 && route_family == AF_INET) {
                    // INET filter matches
                    should_delete = 1;
                } else if (family_mask & 2 && route_family == AF_INET6) {
                    // INET6 filter matches
                    should_delete = 1;
                }
                
                if (should_delete) {
                    // Delete this route
                    msg.rtm.rtm_type = RTM_DELETE;
                    if (write(sock, &msg, msg.rtm.rtm_msglen) < 0) {
                        // Continue even if some deletions fail
                    }
                }
            }
        }
    }
    
    close(sock);
    return 0;
}

int configure_route(const route_config_t *config) {
    return add_route(config);
}

int remove_route(const route_config_t *config) {
    if (config->family == ADDR_FAMILY_INET4 && config->dest.s_addr == 0) {
        // Delete all static routes
        // Determine family mask based on config
        int family_mask = 0;
        if (config->family == ADDR_FAMILY_INET4) {
            family_mask = 1; // INET only
        } else if (config->family == ADDR_FAMILY_INET6) {
            family_mask = 2; // INET6 only
        } else {
            family_mask = 0; // Both (default)
        }
        return delete_all_static_routes(config->fib, family_mask);
    } else {
        return delete_route(config);
    }
}

int show_routes(char *response, size_t resp_len, int fib_filter) {
    printf("DEBUG: show_routes called with fib_filter = %d\n", fib_filter);
    
    // Use NETCONF get-config to retrieve routing information
    // This should be implemented using libnetconf2
    
    // For now, implement a basic routing table display using system calls
    // In a full implementation, this would use NETCONF get-config
    

    
    // Check if FIB exists if specified
    if (fib_filter >= 0) {
        int fibs = 0;
        size_t len = sizeof(fibs);
        if (sysctlbyname("net.fibs", &fibs, &len, NULL, 0) < 0) {
            snprintf(response, resp_len, "Error: Unable to check available FIBs\n");
            return -1;
        }
        
        if (fib_filter >= fibs) {
            snprintf(response, resp_len, "Error: FIB %d does not exist (only %d FIBs available)\n", fib_filter, fibs);
            return -1;
        }
    }
    
    int sock = socket(AF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) {
        snprintf(response, resp_len, "Error: Unable to access routing socket\n");
        return -1;
    }
    
    // Set FIB for the socket if specified
    if (fib_filter >= 0) {
        if (setsockopt(sock, SOL_SOCKET, SO_SETFIB, &fib_filter, sizeof(fib_filter)) < 0) {
            close(sock);
            snprintf(response, resp_len, "Error: Failed to set FIB %d on socket\n", fib_filter);
            return -1;
        }
    }
    
    char *resp_ptr = response;
    size_t remaining = resp_len;
    
    snprintf(resp_ptr, remaining, "%-25s %-25s %-8s %-8s %s\n",
             "Destination", "Gateway", "Flags", "Netif", "Expire");
    resp_ptr += strlen(resp_ptr);
    remaining = resp_len - (resp_ptr - response);
    
    // Add FIB header - always show which FIB we're querying
    int actual_fib = (fib_filter >= 0) ? fib_filter : 0;
    if (fib_filter < 0) {
        // Get current process FIB for display
        size_t len = sizeof(actual_fib);
        if (sysctlbyname("net.my_fibnum", &actual_fib, &len, NULL, 0) < 0) {
            actual_fib = 0; // Default to FIB 0 if we can't get current FIB
        }
    }
    
    snprintf(resp_ptr, remaining, "FIB %d:\n", actual_fib);
    resp_ptr += strlen(resp_ptr);
    remaining = resp_len - (resp_ptr - response);
    
    // Debug: Check what FIB we're actually querying
    snprintf(resp_ptr, remaining, "Debug: Querying FIB %d directly via sysctl\n", actual_fib);
    resp_ptr += strlen(resp_ptr);
    remaining = resp_len - (resp_ptr - response);
    
        // Use sysctl to get routing table information (like netstat -F)
    int mib[7];
    size_t needed;
    char *buf, *next, *lim;
    struct rt_msghdr *rtm;
    struct sockaddr *sa;
    
    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;        /* protocol */
    mib[3] = AF_UNSPEC;
    mib[4] = NET_RT_DUMP;
    mib[5] = 0;        /* no flags */
    // If no FIB specified, use current process FIB, otherwise use specified FIB
    if (fib_filter < 0) {
        // Get current process FIB
        int current_fib = 0;
        size_t len = sizeof(current_fib);
        if (sysctlbyname("net.my_fibnum", &current_fib, &len, NULL, 0) < 0) {
            current_fib = 0; // Default to FIB 0 if we can't get current FIB
        }
        mib[6] = current_fib;
    } else {
        mib[6] = fib_filter;
    }
    
    if (sysctl(mib, 7, NULL, &needed, NULL, 0) < 0) {
        close(sock);
        snprintf(response, resp_len, "Error: Unable to get routing table size\n");
        return -1;
    }
    
    if (needed == 0) {
        close(sock);
        return 0;
    }
    
    buf = malloc(needed);
    if (buf == NULL) {
        close(sock);
        snprintf(response, resp_len, "Error: Memory allocation failed\n");
        return -1;
    }
    
    if (sysctl(mib, 7, buf, &needed, NULL, 0) < 0) {
        free(buf);
        close(sock);
        snprintf(response, resp_len, "Error: Unable to get routing table\n");
        return -1;
    }
    
    lim = buf + needed;
    for (next = buf; next < lim; next += rtm->rtm_msglen) {
        rtm = (struct rt_msghdr *)next;
        if (rtm->rtm_type == RTM_GET) {
            char dest[64] = "-";
            char gateway[64] = "-";
            char flags[16] = "";
            char netif[16] = "-";
            char expire[16] = "-";
            
            sa = (struct sockaddr *)(rtm + 1);
            for (int i = 0; i < RTAX_MAX; i++) {
                if (rtm->rtm_addrs & (1 << i)) {
                    if (i == RTAX_DST) {
                        if (sa->sa_family == AF_INET) {
                            struct sockaddr_in *sin = (struct sockaddr_in*)sa;
                            inet_ntop(AF_INET, &sin->sin_addr, dest, sizeof(dest));
                        } else if (sa->sa_family == AF_INET6) {
                            struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
                            inet_ntop(AF_INET6, &sin6->sin6_addr, dest, sizeof(dest));
                        }
                    } else if (i == RTAX_GATEWAY) {
                        if (sa->sa_family == AF_INET) {
                            struct sockaddr_in *sin = (struct sockaddr_in*)sa;
                            inet_ntop(AF_INET, &sin->sin_addr, gateway, sizeof(gateway));
                        } else if (sa->sa_family == AF_INET6) {
                            struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
                            inet_ntop(AF_INET6, &sin6->sin6_addr, gateway, sizeof(gateway));
                        }
                    } else if (i == RTAX_IFP) {
                        // Get interface name
                        if (sa->sa_family == AF_LINK) {
                            struct sockaddr_dl *sdl = (struct sockaddr_dl*)sa;
                            if (sdl->sdl_nlen > 0) {
                                strncpy(netif, sdl->sdl_data, sdl->sdl_nlen);
                                netif[sdl->sdl_nlen] = '\0';
                            }
                        }
                    }
                    sa = (struct sockaddr *)((char *)sa + sa->sa_len);
                }
            }
            
            // Build flags string
            if (rtm->rtm_flags & RTF_UP) strcat(flags, "U");
            if (rtm->rtm_flags & RTF_GATEWAY) strcat(flags, "G");
            if (rtm->rtm_flags & RTF_HOST) strcat(flags, "H");
            if (rtm->rtm_flags & RTF_REJECT) strcat(flags, "R");
            if (rtm->rtm_flags & RTF_DYNAMIC) strcat(flags, "D");
            if (rtm->rtm_flags & RTF_MODIFIED) strcat(flags, "M");
            if (rtm->rtm_flags & RTF_DONE) strcat(flags, "d");
            if (rtm->rtm_flags & RTF_XRESOLVE) strcat(flags, "X");
            if (rtm->rtm_flags & RTF_LLINFO) strcat(flags, "L");
            if (rtm->rtm_flags & RTF_LLDATA) strcat(flags, "l");
            if (rtm->rtm_flags & RTF_STATIC) strcat(flags, "S");
            if (rtm->rtm_flags & RTF_BLACKHOLE) strcat(flags, "B");
            if (rtm->rtm_flags & RTF_PROTO2) strcat(flags, "2");
            if (rtm->rtm_flags & RTF_PROTO1) strcat(flags, "1");
            
            // If we still don't have an interface name, try to get it from the routing message
            if (strcmp(netif, "-") == 0 && rtm->rtm_index > 0) {
                char ifname[IF_NAMESIZE];
                if (if_indextoname(rtm->rtm_index, ifname) != NULL) {
                    strncpy(netif, ifname, sizeof(netif) - 1);
                    netif[sizeof(netif) - 1] = '\0';
                }
            }
            
            snprintf(resp_ptr, remaining, "%-25s %-25s %-8s %-8s %s\n",
                     dest, gateway, flags, netif, expire);
            resp_ptr += strlen(resp_ptr);
            remaining = resp_len - (resp_ptr - response);
            
            if (remaining <= 0) break;
        }
    }
    
    free(buf);
    close(sock);
    
    return 0;
} 