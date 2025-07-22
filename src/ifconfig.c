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
             "Interface", "Status", "IPv4 Address", "IPv6 Address", "Flags");
    resp_ptr += strlen(resp_ptr);
    remaining = resp_len - (resp_ptr - response);
    
    for (i = if_ni; i->if_index != 0 || i->if_name != NULL; i++) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, i->if_name, IFNAMSIZ - 1);
        
        char status[16] = "DOWN";
        char ipv4_addr[18] = "-";
        char ipv6_addr[18] = "-";
        char flags_str[64] = "";
        
        // Get flags
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
            if (ifr.ifr_flags & IFF_UP) strcpy(status, "UP");
            
            // Build flags string
            if (ifr.ifr_flags & IFF_LOOPBACK) strcat(flags_str, "LOOPBACK ");
            if (ifr.ifr_flags & IFF_POINTOPOINT) strcat(flags_str, "POINTOPOINT ");
            if (ifr.ifr_flags & IFF_MULTICAST) strcat(flags_str, "MULTICAST ");
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
                 i->if_name, status, ipv4_addr, ipv6_addr, flags_str);
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
    
    // If no filter, show all interfaces in detailed format
    if (!filter || strlen(filter) == 0) {
        for (i = if_ni; i->if_index != 0 || i->if_name != NULL; i++) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, i->if_name, IFNAMSIZ - 1);
            
            // Get flags
            if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
                snprintf(resp_ptr, remaining, "%s: flags=%x<", i->if_name, ifr.ifr_flags);
                resp_ptr += strlen(resp_ptr);
                remaining = resp_len - (resp_ptr - response);
                
                // Build flags string
                if (ifr.ifr_flags & IFF_UP) { strcat(resp_ptr, "UP,"); resp_ptr += 3; }
                if (ifr.ifr_flags & IFF_BROADCAST) { strcat(resp_ptr, "BROADCAST,"); resp_ptr += 10; }
                if (ifr.ifr_flags & IFF_DEBUG) { strcat(resp_ptr, "DEBUG,"); resp_ptr += 6; }
                if (ifr.ifr_flags & IFF_LOOPBACK) { strcat(resp_ptr, "LOOPBACK,"); resp_ptr += 9; }
                if (ifr.ifr_flags & IFF_POINTOPOINT) { strcat(resp_ptr, "POINTOPOINT,"); resp_ptr += 12; }
                // IFF_SMART doesn't exist on FreeBSD, removing this line
                if (ifr.ifr_flags & IFF_RUNNING) { strcat(resp_ptr, "RUNNING,"); resp_ptr += 8; }
                if (ifr.ifr_flags & IFF_NOARP) { strcat(resp_ptr, "NOARP,"); resp_ptr += 6; }
                if (ifr.ifr_flags & IFF_PROMISC) { strcat(resp_ptr, "PROMISC,"); resp_ptr += 8; }
                if (ifr.ifr_flags & IFF_ALLMULTI) { strcat(resp_ptr, "ALLMULTI,"); resp_ptr += 9; }
                if (ifr.ifr_flags & IFF_OACTIVE) { strcat(resp_ptr, "OACTIVE,"); resp_ptr += 8; }
                if (ifr.ifr_flags & IFF_SIMPLEX) { strcat(resp_ptr, "SIMPLEX,"); resp_ptr += 8; }
                if (ifr.ifr_flags & IFF_LINK0) { strcat(resp_ptr, "LINK0,"); resp_ptr += 6; }
                if (ifr.ifr_flags & IFF_LINK1) { strcat(resp_ptr, "LINK1,"); resp_ptr += 6; }
                if (ifr.ifr_flags & IFF_LINK2) { strcat(resp_ptr, "LINK2,"); resp_ptr += 6; }
                if (ifr.ifr_flags & IFF_MULTICAST) { strcat(resp_ptr, "MULTICAST,"); resp_ptr += 10; }
                
                // Remove trailing comma
                if (resp_ptr > response && *(resp_ptr-1) == ',') {
                    *(resp_ptr-1) = '>';
                    resp_ptr++;
                } else {
                    strcat(resp_ptr, ">");
                    resp_ptr++;
                }
                
                snprintf(resp_ptr, remaining, " metric 0 mtu %d\n", ifr.ifr_mtu);
                resp_ptr += strlen(resp_ptr);
                remaining = resp_len - (resp_ptr - response);
                
                // Get IPv4 address and netmask
                if (ioctl(sock, SIOCGIFADDR, &ifr) >= 0) {
                    struct sockaddr_in *sin = (struct sockaddr_in*)&ifr.ifr_addr;
                    
                    // Get netmask separately
                    struct ifreq ifr_netmask;
                    memset(&ifr_netmask, 0, sizeof(ifr_netmask));
                    strncpy(ifr_netmask.ifr_name, i->if_name, IFNAMSIZ - 1);
                    if (ioctl(sock, SIOCGIFNETMASK, &ifr_netmask) >= 0) {
                        struct sockaddr_in *sin_netmask = (struct sockaddr_in*)&ifr_netmask.ifr_addr;
                        
                        // Get broadcast address
                        struct ifreq ifr_broadcast;
                        memset(&ifr_broadcast, 0, sizeof(ifr_broadcast));
                        strncpy(ifr_broadcast.ifr_name, i->if_name, IFNAMSIZ - 1);
                        if (ioctl(sock, SIOCGIFBRDADDR, &ifr_broadcast) >= 0) {
                            struct sockaddr_in *sin_broadcast = (struct sockaddr_in*)&ifr_broadcast.ifr_addr;
                            snprintf(resp_ptr, remaining, "\tinet %s netmask 0x%x broadcast %s\n",
                                     inet_ntoa(sin->sin_addr), 
                                     ntohl(sin_netmask->sin_addr.s_addr),
                                     inet_ntoa(sin_broadcast->sin_addr));
                        } else {
                            snprintf(resp_ptr, remaining, "\tinet %s netmask 0x%x\n",
                                     inet_ntoa(sin->sin_addr), 
                                     ntohl(sin_netmask->sin_addr.s_addr));
                        }
                        resp_ptr += strlen(resp_ptr);
                        remaining = resp_len - (resp_ptr - response);
                    }
                }
                
                // Get IPv6 address
                int sock6 = socket(AF_INET6, SOCK_DGRAM, 0);
                if (sock6 >= 0) {
                    struct in6_ifreq ifr6;
                    memset(&ifr6, 0, sizeof(ifr6));
                    strncpy(ifr6.ifr_name, i->if_name, IFNAMSIZ - 1);
                    
                    if (ioctl(sock6, SIOCGIFADDR_IN6, &ifr6) >= 0) {
                        char ipv6_str[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, &ifr6.ifr_addr.sin6_addr, ipv6_str, sizeof(ipv6_str));
                        snprintf(resp_ptr, remaining, "\tinet6 %s prefixlen %d\n", ipv6_str, ifr6.ifr_addr.sin6_scope_id);
                        resp_ptr += strlen(resp_ptr);
                        remaining = resp_len - (resp_ptr - response);
                    }
                    close(sock6);
                }
                
                snprintf(resp_ptr, remaining, "\tmedia: Ethernet autoselect\n");
                resp_ptr += strlen(resp_ptr);
                remaining = resp_len - (resp_ptr - response);
                
                if (remaining <= 0) break;
            }
        }
    } else {
        // Filter by interface type
        // Add table header for filtered display
        snprintf(resp_ptr, remaining, "%-12s %-8s %-18s %-18s %s\n", 
                 "Interface", "Status", "IPv4 Address", "IPv6 Address", "Flags");
        resp_ptr += strlen(resp_ptr);
        remaining = resp_len - (resp_ptr - response);
        
        for (i = if_ni; i->if_index != 0 || i->if_name != NULL; i++) {
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
            
            char status[16] = "DOWN";
            char ipv4_addr[18] = "-";
            char ipv6_addr[18] = "-";
            char flags_str[64] = "";
            
            // Get flags
            if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
                if (ifr.ifr_flags & IFF_UP) strcpy(status, "UP");
                
                // Build flags string
                if (ifr.ifr_flags & IFF_LOOPBACK) strcat(flags_str, "LOOPBACK ");
                if (ifr.ifr_flags & IFF_POINTOPOINT) strcat(flags_str, "POINTOPOINT ");
                if (ifr.ifr_flags & IFF_MULTICAST) strcat(flags_str, "MULTICAST ");
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
            
            snprintf(resp_ptr, remaining, "%-12s %-8s %-18s %-18s %s\n",
                     i->if_name, status, ipv4_addr, ipv6_addr, flags_str);
            resp_ptr += strlen(resp_ptr);
            remaining = resp_len - (resp_ptr - response);
            
            if (remaining <= 0) break;
        }
    }
    
    if_freenameindex(if_ni);
    close(sock);
    return 0;
} 