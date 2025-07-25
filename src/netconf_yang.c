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
 * @file netconf_yang.c
 * @brief YANG data model support for netd NETCONF implementation
 * 
 * This file provides YANG data model support for the netd NETCONF server,
 * including:
 * - libyang context initialization and management
 * - YANG module loading (ietf-interfaces, ietf-ip, ietf-routing, etc.)
 * - Custom FreeBSD-specific augmentations (netd-augments)
 * - Conversion between internal data structures and YANG data trees
 * - YANG-compliant XML generation for NETCONF responses
 * - Data validation against YANG schemas
 * 
 * The implementation uses standard IETF YANG models (RFC 8343, RFC 8344, RFC 8349)
 * with custom augmentations for FreeBSD-specific features, providing standards-compliant 
 * YANG data modeling and validation capabilities as specified in RFC 7950.
 * 
 * VRF (Virtual Routing and Forwarding) support is provided through the 
 * ietf-network-instance model (RFC 8529), allowing interfaces and routing
 * to be separated into different network instances. Tunnel VRF support is
 * provided through custom augmentations for FreeBSD-specific functionality.
 */

#include "common.h"
#include <libnetconf2/netconf.h>
#include <libyang/libyang.h>
#include <sys/stat.h>

// Global libyang context
static struct ly_ctx *yang_ctx = NULL;

/**
 * Initialize YANG context and load required modules
 * @return 0 on success, -1 on failure
 */
int init_yang_context(void) {
    // Create libyang context - use local yang directory and standard IETF RFC directory
    LY_ERR ret = ly_ctx_new("./yang:./yang/std/standard/ietf/RFC:./yang/std/standard/iana", 0, &yang_ctx);
    if (ret != LY_SUCCESS || !yang_ctx) {
        fprintf(stderr, "Failed to create YANG context\n");
        return -1;
    }
    
    // Load standard IETF modules
    if (!ly_ctx_load_module(yang_ctx, "ietf-interfaces", NULL, NULL)) {
        fprintf(stderr, "Failed to load ietf-interfaces module\n");
        return -1;
    }
    
    // Load ietf-ip module for IP address configuration
    if (!ly_ctx_load_module(yang_ctx, "ietf-ip", NULL, NULL)) {
        fprintf(stderr, "Failed to load ietf-ip module\n");
        return -1;
    }
    
    // Load ietf-routing module for routing configuration
    if (!ly_ctx_load_module(yang_ctx, "ietf-routing", NULL, NULL)) {
        fprintf(stderr, "Failed to load ietf-routing module\n");
        return -1;
    }
    
    // Load ietf-network-instance module for VRF/network instance support
    if (!ly_ctx_load_module(yang_ctx, "ietf-network-instance", NULL, NULL)) {
        fprintf(stderr, "Failed to load ietf-network-instance module\n");
        return -1;
    }
    
    // Load supporting modules
    if (!ly_ctx_load_module(yang_ctx, "ietf-inet-types", NULL, NULL)) {
        fprintf(stderr, "Failed to load ietf-inet-types module\n");
        return -1;
    }
    
    if (!ly_ctx_load_module(yang_ctx, "ietf-yang-types", NULL, NULL)) {
        fprintf(stderr, "Failed to load ietf-yang-types module\n");
        return -1;
    }
    
    // Load IANA interface types module (required for interface type identities)
    if (!ly_ctx_load_module(yang_ctx, "iana-if-type", NULL, NULL)) {
        fprintf(stderr, "Failed to load iana-if-type module\n");
        return -1;
    }
    
    // Load custom netd augmentations for FreeBSD-specific features
    if (!ly_ctx_load_module(yang_ctx, "netd-augments", NULL, NULL)) {
        fprintf(stderr, "Failed to load netd-augments module\n");
        return -1;
    }
    
    printf("YANG context initialized successfully with standard IETF models and custom augmentations\n");
    return 0;
}

/**
 * Cleanup YANG context and free resources
 */
void cleanup_yang_context(void) {
    if (yang_ctx) {
        ly_ctx_destroy(yang_ctx);
        yang_ctx = NULL;
    }
}

/**
 * Convert interface configuration to YANG data tree
 * @param config Pointer to interface configuration
 * @return Pointer to YANG data node, or NULL on failure
 */
struct lyd_node* interface_to_yang(const if_config_t *config) {
    char path[512];
    char value[256];
    LY_ERR ret;
    
    // Create path for interface using standard IETF model
    snprintf(path, sizeof(path), "/ietf-interfaces:interfaces/interface[name='%s']", config->name);
    
    // Create interface node
    struct lyd_node *interface = NULL;
    ret = lyd_new_path(NULL, yang_ctx, path, NULL, 0, &interface);
    if (ret != LY_SUCCESS) {
        return NULL;
    }
    
    // Set interface name
    snprintf(path, sizeof(path), "/ietf-interfaces:interfaces/interface[name='%s']/name", config->name);
    lyd_new_path(interface, yang_ctx, path, config->name, 0, NULL);
    
    // Set interface type (using identity reference)
    snprintf(path, sizeof(path), "/ietf-interfaces:interfaces/interface[name='%s']/type", config->name);
    lyd_new_path(interface, yang_ctx, path, "iana-if-type:ethernetCsmacd", 0, NULL);
    
    // Set enabled state
    snprintf(path, sizeof(path), "/ietf-interfaces:interfaces/interface[name='%s']/enabled", config->name);
    lyd_new_path(interface, yang_ctx, path, "true", 0, NULL);
    
    // Bind interface to network instance (VRF) if FIB is specified
    if (config->fib > 0) {
        char vrf_name[64];
        snprintf(vrf_name, sizeof(vrf_name), "vrf-%d", config->fib);
        
        snprintf(path, sizeof(path), "/ietf-interfaces:interfaces/interface[name='%s']/ietf-network-instance:bind-ni-name", config->name);
        lyd_new_path(interface, yang_ctx, path, vrf_name, 0, NULL);
    }
    
    // Add tunnel VRF binding if specified (FreeBSD-specific)
    if (config->tunnel_fib > 0) {
        char tunnel_vrf_name[64];
        snprintf(tunnel_vrf_name, sizeof(tunnel_vrf_name), "tunnel-vrf-%d", config->tunnel_fib);
        
        snprintf(path, sizeof(path), "/ietf-interfaces:interfaces/interface[name='%s']/netd-augments:tunnel-vrf", config->name);
        lyd_new_path(interface, yang_ctx, path, tunnel_vrf_name, 0, NULL);
    }
    
    // Add IP address using ietf-ip model
    if (config->family == ADDR_FAMILY_INET4) {
        // IPv4 address configuration
        char ip_addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &config->addr, ip_addr, sizeof(ip_addr));
        
        snprintf(path, sizeof(path), 
                "/ietf-interfaces:interfaces/interface[name='%s']/ietf-ip:ipv4/address[ip='%s']", 
                config->name, ip_addr);
        
        struct lyd_node *addr = NULL;
        ret = lyd_new_path(interface, yang_ctx, path, NULL, 0, &addr);
        if (ret == LY_SUCCESS && addr) {
            // Set IP address
            snprintf(path, sizeof(path), 
                    "/ietf-interfaces:interfaces/interface[name='%s']/ietf-ip:ipv4/address[ip='%s']/ip", 
                    config->name, ip_addr);
            lyd_new_path(addr, yang_ctx, path, ip_addr, 0, NULL);
            
            // Set prefix length
            snprintf(path, sizeof(path), 
                    "/ietf-interfaces:interfaces/interface[name='%s']/ietf-ip:ipv4/address[ip='%s']/prefix-length", 
                    config->name, ip_addr);
            snprintf(value, sizeof(value), "%d", config->prefix_len);
            lyd_new_path(addr, yang_ctx, path, value, 0, NULL);
        }
    } else if (config->family == ADDR_FAMILY_INET6) {
        // IPv6 address configuration
        char ipv6_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &config->addr6, ipv6_str, sizeof(ipv6_str));
        
        snprintf(path, sizeof(path), 
                "/ietf-interfaces:interfaces/interface[name='%s']/ietf-ip:ipv6/address[ip='%s']", 
                config->name, ipv6_str);
        
        struct lyd_node *addr = NULL;
        ret = lyd_new_path(interface, yang_ctx, path, NULL, 0, &addr);
        if (ret == LY_SUCCESS && addr) {
            // Set IPv6 address
            snprintf(path, sizeof(path), 
                    "/ietf-interfaces:interfaces/interface[name='%s']/ietf-ip:ipv6/address[ip='%s']/ip", 
                    config->name, ipv6_str);
            lyd_new_path(addr, yang_ctx, path, ipv6_str, 0, NULL);
            
            // Set prefix length
            snprintf(path, sizeof(path), 
                    "/ietf-interfaces:interfaces/interface[name='%s']/ietf-ip:ipv6/address[ip='%s']/prefix-length", 
                    config->name, ipv6_str);
            snprintf(value, sizeof(value), "%d", config->prefix_len);
            lyd_new_path(addr, yang_ctx, path, value, 0, NULL);
        }
    }
    
    return interface;
}

/**
 * Create a network instance (VRF) in YANG data tree
 * @param vrf_name Name of the VRF to create
 * @return Pointer to YANG data node, or NULL on failure
 */
struct lyd_node* create_network_instance(const char *vrf_name) {
    char path[512];
    LY_ERR ret;
    
    // Create network instance path
    snprintf(path, sizeof(path), "/ietf-network-instance:network-instances/network-instance[name='%s']", vrf_name);
    
    // Create network instance node
    struct lyd_node *ni = NULL;
    ret = lyd_new_path(NULL, yang_ctx, path, NULL, 0, &ni);
    if (ret != LY_SUCCESS) {
        return NULL;
    }
    
    // Set network instance name
    snprintf(path, sizeof(path), "/ietf-network-instance:network-instances/network-instance[name='%s']/name", vrf_name);
    lyd_new_path(ni, yang_ctx, path, vrf_name, 0, NULL);
    
    // Set enabled state
    snprintf(path, sizeof(path), "/ietf-network-instance:network-instances/network-instance[name='%s']/enabled", vrf_name);
    lyd_new_path(ni, yang_ctx, path, "true", 0, NULL);
    
    // Set description
    snprintf(path, sizeof(path), "/ietf-network-instance:network-instances/network-instance[name='%s']/description", vrf_name);
    char description[128];
    snprintf(description, sizeof(description), "VRF %s for FIB separation", vrf_name);
    lyd_new_path(ni, yang_ctx, path, description, 0, NULL);
    
    // Set VRF root type
    snprintf(path, sizeof(path), "/ietf-network-instance:network-instances/network-instance[name='%s']/vrf-root", vrf_name);
    lyd_new_path(ni, yang_ctx, path, NULL, 0, NULL);
    
    return ni;
}

/**
 * Create a tunnel VRF network instance (FreeBSD-specific)
 * @param tunnel_fib FIB number for the tunnel VRF
 * @return Pointer to YANG data node, or NULL on failure
 */
struct lyd_node* create_tunnel_vrf(int tunnel_fib) {
    char vrf_name[64];
    snprintf(vrf_name, sizeof(vrf_name), "tunnel-vrf-%d", tunnel_fib);
    
    struct lyd_node *ni = create_network_instance(vrf_name);
    if (ni) {
        // Update description to indicate this is a tunnel VRF
        char path[512];
        char description[128];
        snprintf(path, sizeof(path), "/ietf-network-instance:network-instances/network-instance[name='%s']/description", vrf_name);
        snprintf(description, sizeof(description), "Tunnel VRF %d for FreeBSD tunnel interface routing", tunnel_fib);
        lyd_new_path(ni, yang_ctx, path, description, 0, NULL);
    }
    
    return ni;
}

/**
 * Convert route configuration to YANG data tree
 * @param config Pointer to route configuration
 * @return Pointer to YANG data node, or NULL on failure
 */
struct lyd_node* route_to_yang(const route_config_t *config) {
    char path[512];
    char value[256];
    LY_ERR ret;
    
    // Create static route entry using standard IETF routing model
    if (config->family == ADDR_FAMILY_INET4) {
        char dest_str[INET_ADDRSTRLEN];
        char gw_str[INET_ADDRSTRLEN];
        
        inet_ntop(AF_INET, &config->dest, dest_str, sizeof(dest_str));
        inet_ntop(AF_INET, &config->gw, gw_str, sizeof(gw_str));
        
        // Format destination with prefix length
        snprintf(value, sizeof(value), "%s/%d", dest_str, config->prefix_len);
        
        // Create the route path using standard IETF routing structure
        snprintf(path, sizeof(path), 
                "/ietf-routing:routing/control-plane-protocols/"
                "control-plane-protocol[type='ietf-routing:static'][name='static-routing']/"
                "ietf-routing:static-routes/ietf-routing:ipv4/route[destination-prefix='%s']", 
                value);
        
        struct lyd_node *route = NULL;
        ret = lyd_new_path(NULL, yang_ctx, path, NULL, 0, &route);
        if (ret == LY_SUCCESS && route) {
            // Set destination prefix
            snprintf(path, sizeof(path), 
                    "/ietf-routing:routing/control-plane-protocols/"
                    "control-plane-protocol[type='ietf-routing:static'][name='static-routing']/"
                    "ietf-routing:static-routes/ietf-routing:ipv4/route[destination-prefix='%s']/destination-prefix", 
                    value);
            lyd_new_path(route, yang_ctx, path, value, 0, NULL);
            
            // Set next hop address
            snprintf(path, sizeof(path), 
                    "/ietf-routing:routing/control-plane-protocols/"
                    "control-plane-protocol[type='ietf-routing:static'][name='static-routing']/"
                    "ietf-routing:static-routes/ietf-routing:ipv4/route[destination-prefix='%s']/next-hop/next-hop-address", 
                    value);
            lyd_new_path(route, yang_ctx, path, gw_str, 0, NULL);
        }
        return route;
    } else if (config->family == ADDR_FAMILY_INET6) {
        char dest_str[INET6_ADDRSTRLEN];
        char gw_str[INET6_ADDRSTRLEN];
        
        inet_ntop(AF_INET6, &config->dest6, dest_str, sizeof(dest_str));
        inet_ntop(AF_INET6, &config->gw6, gw_str, sizeof(gw_str));
        
        // Format destination with prefix length
        snprintf(value, sizeof(value), "%s/%d", dest_str, config->prefix_len);
        
        // Create the route path using standard IETF routing structure for IPv6
        snprintf(path, sizeof(path), 
                "/ietf-routing:routing/control-plane-protocols/"
                "control-plane-protocol[type='ietf-routing:static'][name='static-routing']/"
                "ietf-routing:static-routes/ietf-routing:ipv6/route[destination-prefix='%s']", 
                value);
        
        struct lyd_node *route = NULL;
        ret = lyd_new_path(NULL, yang_ctx, path, NULL, 0, &route);
        if (ret == LY_SUCCESS && route) {
            // Set destination prefix
            snprintf(path, sizeof(path), 
                    "/ietf-routing:routing/control-plane-protocols/"
                    "control-plane-protocol[type='ietf-routing:static'][name='static-routing']/"
                    "ietf-routing:static-routes/ietf-routing:ipv6/route[destination-prefix='%s']/destination-prefix", 
                    value);
            lyd_new_path(route, yang_ctx, path, value, 0, NULL);
            
            // Set next hop address
            snprintf(path, sizeof(path), 
                    "/ietf-routing:routing/control-plane-protocols/"
                    "control-plane-protocol[type='ietf-routing:static'][name='static-routing']/"
                    "ietf-routing:static-routes/ietf-routing:ipv6/route[destination-prefix='%s']/next-hop/next-hop-address", 
                    value);
            lyd_new_path(route, yang_ctx, path, gw_str, 0, NULL);
        }
        return route;
    }
    
    return NULL;
}

/**
 * Generate XML from YANG data tree
 * @param data_tree Pointer to YANG data tree
 * @param options Print options for XML generation
 * @return XML string, or NULL on failure (must be freed by caller)
 */
char* yang_to_xml(struct lyd_node *data_tree, int options) {
    char *xml = NULL;
    
    if (lyd_print_mem(&xml, data_tree, LYD_XML, options) != 0) {
        return NULL;
    }
    
    return xml;
}

/**
 * Parse XML and convert to YANG data tree
 * @param xml_data XML data string to parse
 * @return Pointer to YANG data tree, or NULL on failure
 */
struct lyd_node* xml_to_yang(const char *xml_data) {
    struct lyd_node *data_tree = NULL;
    
    if (lyd_parse_data_mem(yang_ctx, xml_data, LYD_XML, LYD_PARSE_STRICT, 0, &data_tree) != 0) {
        return NULL;
    }
    
    return data_tree;
}

/**
 * Handle NETCONF get-config request using YANG data models
 * @param filter Filter string for selective data retrieval
 * @param response Buffer to store NETCONF response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
int handle_netconf_get_config_yang(const char *filter, char *response, size_t resp_len) {
    (void)filter; // Suppress unused parameter warning
    struct lyd_node *data_tree = NULL;
    char *xml = NULL;
    
    // Create data tree from current system state using standard IETF models
    // Start with the root interfaces container
    LY_ERR ret = lyd_new_path(NULL, yang_ctx, "/ietf-interfaces:interfaces", NULL, 0, &data_tree);
    if (ret != LY_SUCCESS) {
        snprintf(response, resp_len, "Error: Failed to create interfaces data tree\n");
        return -1;
    }
    
    // Add routing container if needed
    struct lyd_node *routing_tree = NULL;
    ret = lyd_new_path(NULL, yang_ctx, "/ietf-routing:routing", NULL, 0, &routing_tree);
    if (ret == LY_SUCCESS && routing_tree) {
        // Merge routing tree with interfaces tree
        lyd_merge_siblings(&data_tree, routing_tree, 0);
    }
    
    // Add network instances container for VRF support
    struct lyd_node *ni_tree = NULL;
    ret = lyd_new_path(NULL, yang_ctx, "/ietf-network-instance:network-instances", NULL, 0, &ni_tree);
    if (ret == LY_SUCCESS && ni_tree) {
        // Merge network instances tree with main tree
        lyd_merge_siblings(&data_tree, ni_tree, 0);
    }
    
    // Convert to XML
    xml = yang_to_xml(data_tree, LYD_PRINT_SHRINK);
    if (!xml) {
        lyd_free_tree(data_tree);
        snprintf(response, resp_len, "Error: Failed to generate XML\n");
        return -1;
    }
    
    // Format NETCONF response
    snprintf(response, resp_len, 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\">\n"
        "  <data>\n"
        "%s\n"
        "  </data>\n"
        "</rpc-reply>\n", xml);
    
    free(xml);
    lyd_free_tree(data_tree);
    return 0;
}

/**
 * Handle NETCONF edit-config request using YANG data models
 * @param config_xml Configuration data in XML format
 * @param response Buffer to store NETCONF response
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on failure
 */
int handle_netconf_edit_config_yang(const char *config_xml, char *response, size_t resp_len) {
    struct lyd_node *data_tree = NULL;
    int ret = 0;
    
    // Parse XML to YANG data tree
    data_tree = xml_to_yang(config_xml);
    if (!data_tree) {
        snprintf(response, resp_len, "Error: Failed to parse configuration XML\n");
        return -1;
    }
    
    // Validate the configuration
    if (lyd_validate_all(&data_tree, yang_ctx, 0, NULL) != LY_SUCCESS) {
        snprintf(response, resp_len, "Error: Configuration validation failed\n");
        lyd_free_tree(data_tree);
        return -1;
    }
    
    // TODO: Apply the configuration to the system
    // This would involve parsing the YANG data tree and calling
    // configure_interface() and configure_route() functions
    
    // Format NETCONF response
    snprintf(response, resp_len,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\">\n"
        "  <ok/>\n"
        "</rpc-reply>\n");
    
    lyd_free_tree(data_tree);
    return ret;
} 