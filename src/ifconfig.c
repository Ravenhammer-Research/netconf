#include "common.h"

static int get_interface_flags(const char *ifname, int *flags) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        close(sock);
        return -1;
    }
    
    *flags = ifr.ifr_flags;
    close(sock);
    return 0;
}

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

static int set_interface_address(const char *ifname, addr_family_t family,
                               const struct in_addr *addr, const struct in6_addr *addr6,
                               int prefix_len) {
    int sock = socket(family == ADDR_FAMILY_INET4 ? AF_INET : AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    if (family == ADDR_FAMILY_INET4) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
        
        struct sockaddr_in *sin = (struct sockaddr_in*)&ifr.ifr_addr;
        sin->sin_family = AF_INET;
        sin->sin_addr = *addr;
        
        if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
            close(sock);
            return -1;
        }
        
        // Set netmask
        struct ifreq ifr_netmask;
        memset(&ifr_netmask, 0, sizeof(ifr_netmask));
        strncpy(ifr_netmask.ifr_name, ifname, IFNAMSIZ - 1);
        
        struct sockaddr_in *sin_netmask = (struct sockaddr_in*)&ifr_netmask.ifr_addr;
        sin_netmask->sin_family = AF_INET;
        
        // Calculate netmask from prefix length
        uint32_t netmask = 0xFFFFFFFF << (32 - prefix_len);
        sin_netmask->sin_addr.s_addr = htonl(netmask);
        
        if (ioctl(sock, SIOCSIFNETMASK, &ifr_netmask) < 0) {
            close(sock);
            return -1;
        }
    } else {
        struct in6_ifreq ifr6;
        memset(&ifr6, 0, sizeof(ifr6));
        strncpy(ifr6.ifr_name, ifname, IFNAMSIZ - 1);
        ifr6.ifr_addr.sin6_family = AF_INET6;
        ifr6.ifr_addr.sin6_addr = *addr6;
        
        // For IPv6, FreeBSD typically handles prefix length differently
        // We'll set the address and let the system use default prefix
        // In a full implementation, we'd need to use a different approach
        // such as setting the address with the prefix in the address string
        
        if (ioctl(sock, SIOCSIFADDR_IN6, &ifr6) < 0) {
            close(sock);
            return -1;
        }
    }
    
    close(sock);
    return 0;
}

int configure_interface(const if_config_t *config) {
    // Get current flags
    int flags;
    if (get_interface_flags(config->name, &flags) < 0) {
        return -1;
    }
    
    // Set interface up if not already
    if (!(flags & IFF_UP)) {
        flags |= IFF_UP;
        if (set_interface_flags(config->name, flags) < 0) {
            return -1;
        }
    }
    
    // Set address
    if (set_interface_address(config->name, config->family,
                            &config->addr, &config->addr6,
                            config->prefix_len) < 0) {
        return -1;
    }
    
    return 0;
}

int show_interfaces(char *response, size_t resp_len) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    char *resp_ptr = response;
    size_t remaining = resp_len;
    
    // Get interface list
    struct if_nameindex *if_ni, *i;
    if_ni = if_nameindex();
    if (if_ni == NULL) {
        close(sock);
        return -1;
    }
    
    snprintf(resp_ptr, remaining, "%-12s %-8s %-18s %-18s %s\n", 
             "Interface", "Flags", "IPv4 Address", "IPv6 Address", "MTU");
    resp_ptr += strlen(resp_ptr);
    remaining = resp_len - (resp_ptr - response);
    
    for (i = if_ni; i->if_index != 0 && i->if_name != NULL; i++) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, i->if_name, IFNAMSIZ - 1);
        
        char ipv4_addr[18] = "-";
        char ipv6_addr[18] = "-";
        char mtu_str[8] = "-";
        char flags_str[64] = "";
        
        // Get flags and MTU
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
            // Build flags string
            if (ifr.ifr_flags & IFF_LOOPBACK) strcat(flags_str, "LOOPBACK ");
            if (ifr.ifr_flags & IFF_POINTOPOINT) strcat(flags_str, "POINTOPOINT ");
            if (ifr.ifr_flags & IFF_MULTICAST) strcat(flags_str, "MULTICAST ");
            
            // Get MTU
            if (ioctl(sock, SIOCGIFMTU, &ifr) >= 0) {
                snprintf(mtu_str, sizeof(mtu_str), "%d", ifr.ifr_mtu);
            }
        }
        
        // Get IPv4 address
        if (ioctl(sock, SIOCGIFADDR, &ifr) >= 0) {
            struct sockaddr_in *sin = (struct sockaddr_in*)&ifr.ifr_addr;
            inet_ntop(AF_INET, &sin->sin_addr, ipv4_addr, sizeof(ipv4_addr));
        }
        
        // Get IPv6 address (simplified - would need more complex logic for multiple addresses)
        int sock6 = socket(AF_INET6, SOCK_DGRAM, 0);
        if (sock6 >= 0) {
            struct in6_ifreq ifr6;
            memset(&ifr6, 0, sizeof(ifr6));
            strncpy(ifr6.ifr_name, i->if_name, IFNAMSIZ - 1);
            
            if (ioctl(sock6, SIOCGIFADDR_IN6, &ifr6) >= 0) {
                inet_ntop(AF_INET6, &ifr6.ifr_addr.sin6_addr, ipv6_addr, sizeof(ipv6_addr));
            }
            close(sock6);
        }
        
        snprintf(resp_ptr, remaining, "%-12s %-8s %-18s %-18s %s\n",
                 i->if_name, flags_str, ipv4_addr, ipv6_addr, mtu_str);
        resp_ptr += strlen(resp_ptr);
        remaining = resp_len - (resp_ptr - response);
        
        if (remaining <= 0) break;
    }
    
    if_freenameindex(if_ni);
    close(sock);
    return 0;
}

int show_interfaces_filtered(char *response, size_t resp_len, const char *filter) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    char *resp_ptr = response;
    size_t remaining = resp_len;
    
    // Get interface list
    struct if_nameindex *if_ni, *i;
    if_ni = if_nameindex();
    if (if_ni == NULL) {
        close(sock);
        return -1;
    }
    
    // If no filter, show all interfaces in tabular format
    if (!filter || strlen(filter) == 0) {
        // Table header
        snprintf(resp_ptr, remaining, "%-12s %-18s %-18s %s\n", 
                 "Interface", "IPv4 Address", "IPv6 Address", "MTU");
        resp_ptr += strlen(resp_ptr);
        remaining = resp_len - (resp_ptr - response);
        
        for (i = if_ni; i->if_index != 0 && i->if_name != NULL; i++) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, i->if_name, IFNAMSIZ - 1);
            
            char ipv4_addr[18] = "-";
            char ipv6_addr[18] = "-";
            char mtu_str[8] = "-";
            
            // Get MTU
            if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
                if (ioctl(sock, SIOCGIFMTU, &ifr) >= 0) {
                    snprintf(mtu_str, sizeof(mtu_str), "%d", ifr.ifr_mtu);
                }
            }
            
            // Get IPv4 address
            if (ioctl(sock, SIOCGIFADDR, &ifr) >= 0) {
                struct sockaddr_in *sin = (struct sockaddr_in*)&ifr.ifr_addr;
                inet_ntop(AF_INET, &sin->sin_addr, ipv4_addr, sizeof(ipv4_addr));
            }
            
            // Get IPv6 address
            int sock6 = socket(AF_INET6, SOCK_DGRAM, 0);
            if (sock6 >= 0) {
                struct in6_ifreq ifr6;
                memset(&ifr6, 0, sizeof(ifr6));
                strncpy(ifr6.ifr_name, i->if_name, IFNAMSIZ - 1);
                
                if (ioctl(sock6, SIOCGIFADDR_IN6, &ifr6) >= 0) {
                    inet_ntop(AF_INET6, &ifr6.ifr_addr.sin6_addr, ipv6_addr, sizeof(ipv6_addr));
                }
                close(sock6);
            }
            
            int written = snprintf(resp_ptr, remaining, "%-12s %-18s %-18s %s\n",
                     i->if_name, ipv4_addr, ipv6_addr, mtu_str);
            if (written < 0 || (size_t)written >= remaining) break;
            resp_ptr += written;
            remaining -= written;
        }
    } else {
        // Filter by interface type
        // Add table header for filtered display
        snprintf(resp_ptr, remaining, "%-12s %-18s %-18s %s\n", 
                 "Interface", "IPv4 Address", "IPv6 Address", "MTU");
        resp_ptr += strlen(resp_ptr);
        remaining = resp_len - (resp_ptr - response);
        
        for (i = if_ni; i->if_index != 0 && i->if_name != NULL; i++) {
            // Check if interface name matches the filter type
            // For ethernet interfaces, look for em*, igb*, ix*, etc.
            if (strcmp(filter, "ethernet") == 0) {
                if (strncmp(i->if_name, "em", 2) == 0 || 
                    strncmp(i->if_name, "igb", 3) == 0 || 
                    strncmp(i->if_name, "ix", 2) == 0 ||
                    strncmp(i->if_name, "bge", 3) == 0 ||
                    strncmp(i->if_name, "fxp", 3) == 0 ||
                    strncmp(i->if_name, "re", 2) == 0 ||
                    strncmp(i->if_name, "rl", 2) == 0 ||
                    strncmp(i->if_name, "sis", 3) == 0 ||
                    strncmp(i->if_name, "sk", 2) == 0 ||
                    strncmp(i->if_name, "ste", 3) == 0 ||
                    strncmp(i->if_name, "ti", 2) == 0 ||
                    strncmp(i->if_name, "tx", 2) == 0 ||
                    strncmp(i->if_name, "vx", 2) == 0 ||
                    strncmp(i->if_name, "xl", 2) == 0) {
                    // This is an ethernet interface
                } else {
                    continue; // Skip non-ethernet interfaces
                }
            } else if (strcmp(filter, "bridge") == 0) {
                if (strncmp(i->if_name, "bridge", 6) != 0) {
                    continue; // Skip non-bridge interfaces
                }
            } else if (strcmp(filter, "gif") == 0) {
                if (strncmp(i->if_name, "gif", 3) != 0) {
                    continue; // Skip non-gif interfaces
                }
            } else if (strcmp(filter, "tun") == 0) {
                if (strncmp(i->if_name, "tun", 3) != 0) {
                    continue; // Skip non-tun interfaces
                }
            } else if (strcmp(filter, "tap") == 0) {
                if (strncmp(i->if_name, "tap", 3) != 0) {
                    continue; // Skip non-tap interfaces
                }
            } else if (strcmp(filter, "vlan") == 0) {
                if (strncmp(i->if_name, "vlan", 4) != 0) {
                    continue; // Skip non-vlan interfaces
                }
            } else if (strcmp(filter, "lo") == 0) {
                if (strncmp(i->if_name, "lo", 2) != 0) {
                    continue; // Skip non-loopback interfaces
                }
            } else {
                // For other filters, use prefix matching
                if (strncmp(i->if_name, filter, strlen(filter)) != 0) {
                    continue;
                }
            }
            
            // Process the interface
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, i->if_name, IFNAMSIZ - 1);
            
            char ipv4_addr[18] = "-";
            char ipv6_addr[18] = "-";
            char mtu_str[8] = "-";
            
            // Get MTU
            if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
                if (ioctl(sock, SIOCGIFMTU, &ifr) >= 0) {
                    snprintf(mtu_str, sizeof(mtu_str), "%d", ifr.ifr_mtu);
                }
            }
            
            // Get IPv4 address
            if (ioctl(sock, SIOCGIFADDR, &ifr) >= 0) {
                struct sockaddr_in *sin = (struct sockaddr_in*)&ifr.ifr_addr;
                inet_ntop(AF_INET, &sin->sin_addr, ipv4_addr, sizeof(ipv4_addr));
            }
            
            // Get IPv6 address
            int sock6 = socket(AF_INET6, SOCK_DGRAM, 0);
            if (sock6 >= 0) {
                struct in6_ifreq ifr6;
                memset(&ifr6, 0, sizeof(ifr6));
                strncpy(ifr6.ifr_name, i->if_name, IFNAMSIZ - 1);
                
                if (ioctl(sock6, SIOCGIFADDR_IN6, &ifr6) >= 0) {
                    inet_ntop(AF_INET6, &ifr6.ifr_addr.sin6_addr, ipv6_addr, sizeof(ipv6_addr));
                }
                close(sock6);
            }
            
            snprintf(resp_ptr, remaining, "%-12s %-18s %-18s %s\n",
                     i->if_name, ipv4_addr, ipv6_addr, mtu_str);
            resp_ptr += strlen(resp_ptr);
            remaining = resp_len - (resp_ptr - response);
            
            if (remaining <= 0) break;
        }
    }
    
    if_freenameindex(if_ni);
    close(sock);
    return 0;
} 