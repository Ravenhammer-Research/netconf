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
 * @file ifconfig.c
 * @brief Network interface configuration and management
 * 
 * This file implements comprehensive network interface management functionality
 * for the netd daemon, including:
 * - Interface creation and configuration
 * - IPv4 and IPv6 address assignment
 * - Interface enumeration and status reporting
 * - Interface flag management (up/down, etc.)
 * - Support for various interface types (ethernet, tap, etc.)
 * 
 * The implementation uses FreeBSD's ioctl interface and getifaddrs() system
 * calls to interact with the kernel networking subsystem.
 */

#include "common.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

// Interface information structure
typedef struct {
    char name[IFNAMSIZ];
    char ipv4_addr[18];
    char ipv6_addr[18];
    char mtu_str[8];
    char fib_str[8];
    char tunnel_fib_str[8];
    int flags;
    int mtu;
} interface_info_t;

/**
 * Get interface flags via ioctl
 * @param ifname Interface name
 * @param flags Pointer to store interface flags
 * @return 0 on success, -1 on failure
 */
static int get_interface_flags(const char *ifname, int *flags) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    
    int result = ioctl(sock, SIOCGIFFLAGS, &ifr);
    if (result >= 0) {
        *flags = ifr.ifr_flags;
    }
    
    close(sock);
    return result;
}

/**
 * Create a new interface (primarily for tap interfaces)
 * @param ifname Interface name to create
 * @return 0 on success, -1 on failure
 */
static int create_interface(const char *ifname) {
    // For FreeBSD, we can create interfaces using ioctl
    // This works for most interface types including tap interfaces
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("DEBUG: Failed to create socket for interface creation\n");
        return -1;
    }
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    
    // Try to create the interface using SIOCIFCREATE
    int result = ioctl(sock, SIOCIFCREATE, &ifr);
    if (result < 0) {
        // If SIOCIFCREATE fails, the interface might already exist
        // or we might need to use a different approach for this interface type
        printf("DEBUG: SIOCIFCREATE failed for %s (errno: %d, %s)\n", 
               ifname, errno, strerror(errno));
        
        // Check if interface already exists
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
            printf("DEBUG: Interface %s already exists\n", ifname);
            close(sock);
            return 0;
        }
        
        close(sock);
        return -1;
    }
    
    printf("DEBUG: Successfully created interface %s\n", ifname);
    close(sock);
    return 0;
}

/**
 * Set interface flags via ioctl
 * @param ifname Interface name
 * @param flags Interface flags to set
 * @return 0 on success, -1 on failure
 */
static int set_interface_flags(const char *ifname, int flags) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_flags = flags;
    
    int result = ioctl(sock, SIOCSIFFLAGS, &ifr);
    close(sock);
    return result;
}

/**
 * Get interface MTU (Maximum Transmission Unit) via ioctl
 * @param ifname Interface name
 * @param mtu Pointer to store the MTU value
 * @return 0 on success, -1 on failure
 */
static int get_interface_mtu(const char *ifname, int *mtu) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    
    int result = ioctl(sock, SIOCGIFMTU, &ifr);
    if (result >= 0) {
        *mtu = ifr.ifr_mtu;
    }
    
    close(sock);
    return result;
}

/**
 * Get IPv4 address and netmask for an interface
 * @param ifname Interface name
 * @param addr_str Buffer to store address string in CIDR format (e.g., "192.168.1.1/24")
 * @param addr_len Size of address string buffer
 * @return 0 on success, -1 on failure
 */
static int get_interface_ipv4(const char *ifname, char *addr_str, size_t addr_len) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    
    int result = ioctl(sock, SIOCGIFADDR, &ifr);
    if (result >= 0) {
        struct sockaddr_in *sin = (struct sockaddr_in*)&ifr.ifr_addr;
        char addr_buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, addr_buf, sizeof(addr_buf));
        
        // Get netmask
        struct ifreq ifr_netmask;
        memset(&ifr_netmask, 0, sizeof(ifr_netmask));
        strncpy(ifr_netmask.ifr_name, ifname, IFNAMSIZ - 1);
        
        if (ioctl(sock, SIOCGIFNETMASK, &ifr_netmask) >= 0) {
            struct sockaddr_in *sin_netmask = (struct sockaddr_in*)&ifr_netmask.ifr_addr;
            uint32_t netmask = ntohl(sin_netmask->sin_addr.s_addr);
            
            // Calculate prefix length
            int prefix_len = 0;
            while (netmask & 0x80000000) {
                prefix_len++;
                netmask <<= 1;
            }
            
            snprintf(addr_str, addr_len, "%s/%d", addr_buf, prefix_len);
        } else {
            strncpy(addr_str, addr_buf, addr_len);
        }
    }
    
    close(sock);
    return result;
}

/**
 * Get IPv6 address for an interface
 * @param ifname Interface name
 * @param addr_str Buffer to store IPv6 address string
 * @param addr_len Size of address string buffer
 * @return 0 on success, -1 on failure
 */
static int get_interface_ipv6(const char *ifname, char *addr_str, size_t addr_len) {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct in6_ifreq ifr6;
    memset(&ifr6, 0, sizeof(ifr6));
    strncpy(ifr6.ifr_name, ifname, IFNAMSIZ - 1);
    
    int result = ioctl(sock, SIOCGIFADDR_IN6, &ifr6);
    if (result >= 0) {
        inet_ntop(AF_INET6, &ifr6.ifr_addr.sin6_addr, addr_str, addr_len);
    }
    
    close(sock);
    return result;
}

/**
 * Get FIB number for an interface using routing socket API
 * @param ifname Interface name
 * @param fib_str Buffer to store FIB number as string
 * @param fib_len Size of FIB string buffer
 * @return 0 on success, -1 on failure
 */
static int get_interface_fib(const char *ifname, char *fib_str, size_t fib_len) {
    int sock = socket(AF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) {
        snprintf(fib_str, fib_len, "0");
        return 0;
    }
    
    // Try to get the number of FIBs first
    int num_fibs = 1;
    size_t len = sizeof(num_fibs);
    if (sysctlbyname("net.fibs", &num_fibs, &len, NULL, 0) == -1) {
        // Single FIB system, default to 0
        close(sock);
        snprintf(fib_str, fib_len, "0");
        return 0;
    }
    
    // Check each FIB to see if this interface has routes in it
    for (int fib = 0; fib < num_fibs; fib++) {
        // Set the FIB for this socket
        if (setsockopt(sock, SOL_SOCKET, SO_SETFIB, &fib, sizeof(fib)) < 0) {
            continue; // Skip this FIB if we can't set it
        }
        
        // Query routing table for this FIB
        int mib[7];
        size_t needed;
        char *buf;
        
        mib[0] = CTL_NET;
        mib[1] = PF_ROUTE;
        mib[2] = 0;        /* protocol */
        mib[3] = AF_UNSPEC;
        mib[4] = NET_RT_DUMP;
        mib[5] = 0;        /* no flags */
        mib[6] = fib;
        
        // Get required buffer size
        if (sysctl(mib, 7, NULL, &needed, NULL, 0) < 0) {
            continue; // Skip this FIB
        }
        
        // Allocate buffer
        buf = malloc(needed);
        if (!buf) {
            continue;
        }
        
        // Get route data
        if (sysctl(mib, 7, buf, &needed, NULL, 0) < 0) {
            free(buf);
            continue;
        }
        
        // Parse route messages to find interface
        char *next = buf;
        char *lim = buf + needed;
        struct rt_msghdr *rtm;
        
        while (next < lim) {
            rtm = (struct rt_msghdr *)next;
            
            // Check if this route uses our interface
            const struct sockaddr *sa = (const struct sockaddr *)(rtm + 1);
            for (int i = 0; i < RTAX_MAX; i++) {
                if (rtm->rtm_addrs & (1 << i)) {
                    if (i == RTAX_IFP && sa->sa_family == AF_LINK) {
                        const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)sa;
                        if (sdl->sdl_nlen > 0 && 
                            strncmp(sdl->sdl_data, ifname, sdl->sdl_nlen) == 0 &&
                            strlen(ifname) == sdl->sdl_nlen) {
                            // Found our interface in this FIB
                            free(buf);
                            close(sock);
                            snprintf(fib_str, fib_len, "%d", fib);
                            return 0;
                        }
                    }
                    sa = (const struct sockaddr *)((const char *)sa + sa->sa_len);
                }
            }
            next += rtm->rtm_msglen;
        }
        
        free(buf);
    }
    
    close(sock);
    
    // If no FIB found, default to 0
    snprintf(fib_str, fib_len, "0");
    return 0;
}

/**
 * Get tunnel FIB number for an interface
 * @param ifname Interface name
 * @param tunnel_fib_str Buffer to store tunnel FIB number as string
 * @param tunnel_fib_len Size of tunnel FIB string buffer
 * @return 0 on success, -1 on failure
 */
static int get_interface_tunnel_fib(const char *ifname, char *tunnel_fib_str, size_t tunnel_fib_len) {
    // Check if this is a tunnel interface (gif, tun, etc.)
    if (strncmp(ifname, "gif", 3) == 0 || 
        strncmp(ifname, "tun", 3) == 0) {
        
        // For tunnel interfaces, try to get tunnel-specific FIB
        snprintf(tunnel_fib_str, tunnel_fib_len, "0");
        return 0;
    }
    
    // Not a tunnel interface
    snprintf(tunnel_fib_str, tunnel_fib_len, "-");
    return 0;
}

/**
 * Populate interface information structure
 * @param ifname Interface name
 * @param info Pointer to interface_info_t structure to populate
 */
static void populate_interface_info(const char *ifname, interface_info_t *info) {
    // Initialize with defaults
    strncpy(info->name, ifname, sizeof(info->name) - 1);
    strcpy(info->ipv4_addr, "-");
    strcpy(info->ipv6_addr, "-");
    strcpy(info->mtu_str, "-");
    strcpy(info->fib_str, "-");
    strcpy(info->tunnel_fib_str, "-");
    info->flags = 0;
    info->mtu = 0;
    
    // Get interface data
    get_interface_flags(ifname, &info->flags);
    get_interface_mtu(ifname, &info->mtu);
    get_interface_ipv4(ifname, info->ipv4_addr, sizeof(info->ipv4_addr));
    get_interface_ipv6(ifname, info->ipv6_addr, sizeof(info->ipv6_addr));
    get_interface_fib(ifname, info->fib_str, sizeof(info->fib_str));
    get_interface_tunnel_fib(ifname, info->tunnel_fib_str, sizeof(info->tunnel_fib_str));
    
    // Update MTU string if we got a valid value
    if (info->mtu > 0) {
        snprintf(info->mtu_str, sizeof(info->mtu_str), "%d", info->mtu);
    }
}

/**
 * Check if interface name matches the specified filter
 * @param ifname Interface name to check
 * @param filter Filter string (interface type)
 * @return 1 if matches, 0 if no match
 */
static int interface_matches_filter(const char *ifname, const char *filter) {
    if (!filter || strlen(filter) == 0) {
        return 1; // No filter, match all
    }
    
    // Simple string matching - could be enhanced for type-based filtering
    return strstr(ifname, filter) != NULL;
}

/**
 * Format interface information as a table row
 * @param info Pointer to interface information structure
 * @param buffer Buffer to store formatted row
 * @param buffer_len Size of buffer
 * @return Number of bytes written, -1 on error
 */
static int format_interface_row(const interface_info_t *info, char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "%-12s %-18s %-18s %-8s %-9s %d\n",
                   info->name, info->ipv4_addr, info->ipv6_addr, 
                   info->fib_str, info->tunnel_fib_str, info->mtu);
}

/**
 * Write interface table header to buffer
 * @param buffer Buffer to write header to
 * @param buffer_len Size of buffer
 * @return Number of bytes written, -1 on error
 */
static int write_table_header(char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "Interface    IPv4 Address       IPv6 Address       FIB      TunnelFIB MTU\n");
}

/**
 * Internal function to show interfaces with optional filtering
 * @param response Buffer to store interface listing
 * @param resp_len Size of response buffer
 * @param filter Optional filter string (NULL for no filter)
 * @return 0 on success, -1 on failure
 */
static int show_interfaces_internal(char *response, size_t resp_len, const char *filter) {
    struct if_nameindex *if_ni, *i;
    char *resp_ptr = response;
    size_t remaining = resp_len;
    
    // Get interface list
    if_ni = if_nameindex();
    if (if_ni == NULL) {
        return -1;
    }
    
    // Write table header
    int written = write_table_header(resp_ptr, remaining);
    if (written < 0 || (size_t)written >= remaining) {
        if_freenameindex(if_ni);
        return -1;
    }
    resp_ptr += written;
    remaining -= written;
    
    // Process each interface
    for (i = if_ni; i->if_index != 0 && i->if_name != NULL; i++) {
        // Check filter
        if (!interface_matches_filter(i->if_name, filter)) {
            continue;
        }
        
        // Get interface information
        interface_info_t info;
        populate_interface_info(i->if_name, &info);
        
        // Format and write the row
        written = format_interface_row(&info, resp_ptr, remaining);
        if (written < 0 || (size_t)written >= remaining) {
            break;
        }
        resp_ptr += written;
        remaining -= written;
    }
    
    if_freenameindex(if_ni);
    return 0;
}

/**
 * Set IPv4 address on an interface
 * @param ifname Interface name
 * @param addr Pointer to IPv4 address structure
 * @param prefix_len Prefix length for netmask
 * @return 0 on success, -1 on failure
 */
static int set_interface_address_ipv4(const char *ifname, const struct in_addr *addr, int prefix_len) {
    printf("DEBUG: set_interface_address_ipv4 called for %s\n", ifname);
    printf("DEBUG: Address: %s, Prefix: %d\n", inet_ntoa(*addr), prefix_len);
    printf("DEBUG: Running as UID: %d\n", getuid());
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("DEBUG: Failed to create socket for IPv4 address setting\n");
        return -1;
    }
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    
    // Set the IPv4 address
    struct sockaddr_in *sin = (struct sockaddr_in*)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr = *addr;
    
    int result = ioctl(sock, SIOCSIFADDR, &ifr);
    if (result < 0) {
        printf("DEBUG: Failed to set IPv4 address with ioctl (errno: %d, %s)\n", 
               errno, strerror(errno));
        close(sock);
        return -1;
    }
    
    // Set the netmask based on prefix length
    if (prefix_len > 0 && prefix_len <= 32) {
        uint32_t netmask = 0xFFFFFFFF << (32 - prefix_len);
        struct sockaddr_in *sin_netmask = (struct sockaddr_in*)&ifr.ifr_addr;
        sin_netmask->sin_family = AF_INET;
        sin_netmask->sin_addr.s_addr = htonl(netmask);
        
        result = ioctl(sock, SIOCSIFNETMASK, &ifr);
        if (result < 0) {
            printf("DEBUG: Failed to set netmask with ioctl (errno: %d, %s)\n", 
                   errno, strerror(errno));
            // Don't fail completely if netmask setting fails
        }
    }
    
    close(sock);
    printf("DEBUG: Successfully set IPv4 address with ioctl\n");
    return 0;
}

/**
 * Set IPv6 address on an interface
 * @param ifname Interface name
 * @param addr6 Pointer to IPv6 address structure
 * @return 0 on success, -1 on failure
 */
static int set_interface_address_ipv6(const char *ifname, const struct in6_addr *addr6) {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
        struct in6_ifreq ifr6;
        memset(&ifr6, 0, sizeof(ifr6));
        strncpy(ifr6.ifr_name, ifname, IFNAMSIZ - 1);
        ifr6.ifr_addr.sin6_family = AF_INET6;
        ifr6.ifr_addr.sin6_addr = *addr6;
        
    int result = ioctl(sock, SIOCSIFADDR_IN6, &ifr6);
    close(sock);
    return result;
}

// Public interface functions

/**
 * Configure an interface with the specified configuration
 * @param config Pointer to interface configuration structure
 * @return 0 on success, -1 on failure
 */
int configure_interface(const if_config_t *config) {
    printf("DEBUG: Configuring interface %s\n", config->name);
    printf("DEBUG: Family: %d, Prefix: %d\n", config->family, config->prefix_len);
    
    // Get current flags
    int flags;
    if (get_interface_flags(config->name, &flags) < 0) {
        printf("DEBUG: Interface doesn't exist, creating...\n");
        // Interface doesn't exist, try to create it
        if (create_interface(config->name) < 0) {
            printf("DEBUG: Failed to create interface\n");
            return -1;
        }
        printf("DEBUG: Interface created successfully\n");
        
        // Give the interface a moment to be fully ready
        usleep(100000); // 100ms delay
        
        // Try to get flags again after creation
        if (get_interface_flags(config->name, &flags) < 0) {
            printf("DEBUG: Failed to get flags after creation\n");
            return -1;
        }
    }
    
    // Set interface up if not already
    if (!(flags & IFF_UP)) {
        flags |= IFF_UP;
        if (set_interface_flags(config->name, flags) < 0) {
            return -1;
        }
    }
    
    // Set address based on family
    if (config->family == ADDR_FAMILY_INET4) {
        printf("DEBUG: Setting IPv4 address for interface %s\n", config->name);
        int result = set_interface_address_ipv4(config->name, &config->addr, config->prefix_len);
        if (result < 0) {
            printf("DEBUG: Failed to set IPv4 address (errno: %d, %s)\n", errno, strerror(errno));
        } else {
            printf("DEBUG: Successfully set IPv4 address\n");
        }
        return result;
    } else {
        printf("DEBUG: Setting IPv6 address for interface %s\n", config->name);
        int result = set_interface_address_ipv6(config->name, &config->addr6);
        if (result < 0) {
            printf("DEBUG: Failed to set IPv6 address (errno: %d, %s)\n", errno, strerror(errno));
        } else {
            printf("DEBUG: Successfully set IPv6 address\n");
        }
        return result;
    }
}

/**
 * Show all interfaces without filtering
 * @param response Buffer to store interface listing
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
int show_interfaces(char *response, size_t resp_len) {
    return show_interfaces_filtered(response, resp_len, "");
}

/**
 * Show interfaces with optional type filtering
 * @param response Buffer to store interface listing
 * @param resp_len Size of response buffer
 * @param filter Interface type filter (NULL for all)
 * @return 0 on success, -1 on failure
 */
int show_interfaces_filtered(char *response, size_t resp_len, const char *filter) {
    return show_interfaces_internal(response, resp_len, filter);
} 