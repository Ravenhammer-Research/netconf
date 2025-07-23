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

#include "common.h"

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

// Helper function to get interface flags
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

// Helper function to create interface
static int create_interface(const char *ifname) {
    // For FreeBSD, we need to create tap interfaces properly
    // First, create the tap interface
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ifconfig %s create", ifname);
    
    int result = system(cmd);
    if (result != 0) {
        printf("DEBUG: Failed to create interface with ifconfig (result: %d)\n", result);
        return -1;
    }
    
    // Then bring it up
    snprintf(cmd, sizeof(cmd), "ifconfig %s up", ifname);
    result = system(cmd);
    if (result != 0) {
        printf("DEBUG: Failed to bring interface up (result: %d)\n", result);
        return -1;
    }
    
    printf("DEBUG: Successfully created and brought up interface %s\n", ifname);
    return 0;
}

// Helper function to set interface flags
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

// Helper function to get interface MTU
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

// Helper function to get IPv4 address with netmask
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

// Helper function to get IPv6 address
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

// Helper function to get FIB value via sysctl
static int get_interface_fib(const char *ifname, char *fib_str, size_t fib_len) {
    char sysctl_name[256];
    int fib_value;
    size_t fib_value_len = sizeof(fib_value);
    
    // Try different sysctl paths for FIB
    snprintf(sysctl_name, sizeof(sysctl_name), "net.if.%s.fib", ifname);
    if (sysctlbyname(sysctl_name, &fib_value, &fib_value_len, NULL, 0) == 0) {
        snprintf(fib_str, fib_len, "%d", fib_value);
        return 0;
    }
    
    // Try alternative path
    snprintf(sysctl_name, sizeof(sysctl_name), "net.link.ether.inet.fib.%s", ifname);
    if (sysctlbyname(sysctl_name, &fib_value, &fib_value_len, NULL, 0) == 0) {
        snprintf(fib_str, fib_len, "%d", fib_value);
        return 0;
    }
    
    // Try using system call to get FIB via ifconfig
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ifconfig %s | grep 'fib:' | awk '{print $2}'", ifname);
    
    FILE *fp = popen(cmd, "r");
    if (fp != NULL) {
        char result[16];
        if (fgets(result, sizeof(result), fp) != NULL) {
            // Remove newline
            result[strcspn(result, "\n")] = 0;
            if (strlen(result) > 0) {
                strncpy(fib_str, result, fib_len);
                pclose(fp);
                return 0;
            }
        }
        pclose(fp);
    }
    
    return -1;
}

// Helper function to get tunnel FIB value via sysctl
static int get_interface_tunnel_fib(const char *ifname, char *tunnel_fib_str, size_t tunnel_fib_len) {
    char sysctl_name[256];
    int tunnel_fib_value;
    size_t tunnel_fib_value_len = sizeof(tunnel_fib_value);
    
    snprintf(sysctl_name, sizeof(sysctl_name), "net.link.ether.inet.tunnel_fib.%s", ifname);
    if (sysctlbyname(sysctl_name, &tunnel_fib_value, &tunnel_fib_value_len, NULL, 0) == 0) {
        snprintf(tunnel_fib_str, tunnel_fib_len, "%d", tunnel_fib_value);
        return 0;
    }
    return -1;
}

// Helper function to populate interface information
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

// Helper function to check if interface matches filter
static int interface_matches_filter(const char *ifname, const char *filter) {
    if (!filter || strlen(filter) == 0) {
        return 1; // No filter, match all
    }
    
    if (strcmp(filter, "ethernet") == 0) {
        // Check for common ethernet interface prefixes
        const char *ethernet_prefixes[] = {
            "em", "igb", "ix", "bge", "fxp", "re", "rl", "sis", 
            "sk", "ste", "ti", "tx", "vx", "xl", NULL
        };
        
        for (int i = 0; ethernet_prefixes[i] != NULL; i++) {
            if (strncmp(ifname, ethernet_prefixes[i], strlen(ethernet_prefixes[i])) == 0) {
                return 1;
            }
        }
        return 0;
    }
    
    // Direct prefix matching for other types
    return strncmp(ifname, filter, strlen(filter)) == 0;
}

// Helper function to format interface info as table row
static int format_interface_row(const interface_info_t *info, char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "%-12s %-18s %-18s %-8s %-8s %s\n",
                   info->name, info->ipv4_addr, info->ipv6_addr, 
                   info->fib_str, info->tunnel_fib_str, info->mtu_str);
}

// Helper function to write table header
static int write_table_header(char *buffer, size_t buffer_len) {
    return snprintf(buffer, buffer_len, "%-12s %-18s %-18s %-8s %-8s %s\n", 
                   "Interface", "IPv4 Address", "IPv6 Address", "FIB", "TunnelFIB", "MTU");
}

// Main function to show interfaces
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

// Set interface address (IPv4)
static int set_interface_address_ipv4(const char *ifname, const struct in_addr *addr, int prefix_len) {
    printf("DEBUG: set_interface_address_ipv4 called for %s\n", ifname);
    printf("DEBUG: Address: %s, Prefix: %d\n", inet_ntoa(*addr), prefix_len);
    printf("DEBUG: Running as UID: %d\n", getuid());
    
    // Use system call to run ifconfig directly
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ifconfig %s inet %s/%d", ifname, inet_ntoa(*addr), prefix_len);
    
    printf("DEBUG: Running command: %s\n", cmd);
    int result = system(cmd);
    if (result != 0) {
        printf("DEBUG: Failed to set address with ifconfig (result: %d)\n", result);
        return -1;
    }
    
    printf("DEBUG: Successfully set address with ifconfig\n");
    return 0;
}

// Set interface address (IPv6)
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

int show_interfaces(char *response, size_t resp_len) {
    return show_interfaces_internal(response, resp_len, NULL);
}

int show_interfaces_filtered(char *response, size_t resp_len, const char *filter) {
    return show_interfaces_internal(response, resp_len, filter);
} 