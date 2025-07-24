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
 * - YANG module loading (ietf-interfaces, netd-simple, etc.)
 * - Conversion between internal data structures and YANG data trees
 * - YANG-compliant XML generation for NETCONF responses
 * - Data validation against YANG schemas
 * 
 * The implementation uses libyang library to provide standards-compliant
 * YANG data modeling and validation capabilities as specified in RFC 7950.
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
    // Create libyang context - use local yang directory
    LY_ERR ret = ly_ctx_new("./yang", 0, &yang_ctx);
    if (ret != LY_SUCCESS || !yang_ctx) {
        fprintf(stderr, "Failed to create YANG context\n");
        return -1;
    }
    
    // Load netd-interfaces module
    if (!ly_ctx_load_module(yang_ctx, "netd-interfaces", NULL, NULL)) {
        fprintf(stderr, "Failed to load netd-interfaces module\n");
        return -1;
    }
    
    // Load netd-routing module
    if (!ly_ctx_load_module(yang_ctx, "netd-routing", NULL, NULL)) {
        fprintf(stderr, "Failed to load netd-routing module\n");
        return -1;
    }
    
    // Load netd-simple module
    if (!ly_ctx_load_module(yang_ctx, "netd-simple", NULL, NULL)) {
        fprintf(stderr, "Failed to load netd-simple module\n");
        return -1;
    }
    
    printf("YANG context initialized successfully\n");
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
    
    // Create path for interface
    snprintf(path, sizeof(path), "/netd-simple:netd-config/interface[name='%s']", config->name);
    
    // Create interface node
    struct lyd_node *interface = NULL;
    ret = lyd_new_path(NULL, yang_ctx, path, NULL, 0, &interface);
    if (ret != LY_SUCCESS) {
        return NULL;
    }
    
    // Set interface name
    snprintf(path, sizeof(path), "/netd-simple:netd-config/interface[name='%s']/name", config->name);
    lyd_new_path(interface, yang_ctx, path, config->name, 0, NULL);
    
    // Set enabled state
    snprintf(path, sizeof(path), "/netd-simple:netd-config/interface[name='%s']/enabled", config->name);
    lyd_new_path(interface, yang_ctx, path, "true", 0, NULL);
    
    // Set FIB if specified
    if (config->fib > 0) {
        snprintf(path, sizeof(path), "/netd-simple:netd-config/interface[name='%s']/fib", config->name);
        snprintf(value, sizeof(value), "%d", config->fib);
        lyd_new_path(interface, yang_ctx, path, value, 0, NULL);
    }
    
    // Add IP address
    if (config->family == ADDR_FAMILY_INET4) {
        snprintf(value, sizeof(value), "%s/%d", inet_ntoa(config->addr), config->prefix_len);
        snprintf(path, sizeof(path), "/netd-simple:netd-config/interface[name='%s']/address[ip='%s']", 
                config->name, value);
        
        struct lyd_node *addr = NULL;
        ret = lyd_new_path(interface, yang_ctx, path, NULL, 0, &addr);
        if (ret == LY_SUCCESS && addr) {
            lyd_new_path(addr, yang_ctx, "ip", value, 0, NULL);
            lyd_new_path(addr, yang_ctx, "family", "ipv4", 0, NULL);
        }
    } else if (config->family == ADDR_FAMILY_INET6) {
        char ipv6_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &config->addr6, ipv6_str, sizeof(ipv6_str));
        
        snprintf(value, sizeof(value), "%s/%d", ipv6_str, config->prefix_len);
        snprintf(path, sizeof(path), "/netd-simple:netd-config/interface[name='%s']/address[ip='%s']", 
                config->name, value);
        
        struct lyd_node *addr = NULL;
        ret = lyd_new_path(interface, yang_ctx, path, NULL, 0, &addr);
        if (ret == LY_SUCCESS && addr) {
            lyd_new_path(addr, yang_ctx, "ip", value, 0, NULL);
            lyd_new_path(addr, yang_ctx, "family", "ipv6", 0, NULL);
        }
    }
    
    return interface;
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
    
    // Create route entry
    if (config->family == ADDR_FAMILY_INET4) {
        snprintf(value, sizeof(value), "%s/%d", inet_ntoa(config->dest), config->prefix_len);
        snprintf(path, sizeof(path), "/netd-simple:netd-config/route[destination='%s']", value);
        
        struct lyd_node *route = NULL;
        ret = lyd_new_path(NULL, yang_ctx, path, NULL, 0, &route);
        if (ret == LY_SUCCESS && route) {
            lyd_new_path(route, yang_ctx, "destination", value, 0, NULL);
            lyd_new_path(route, yang_ctx, "gateway", inet_ntoa(config->gw), 0, NULL);
            
            if (config->fib > 0) {
                snprintf(value, sizeof(value), "%d", config->fib);
                lyd_new_path(route, yang_ctx, "fib", value, 0, NULL);
            }
        }
        return route;
    } else if (config->family == ADDR_FAMILY_INET6) {
        char dest_str[INET6_ADDRSTRLEN];
        char gw_str[INET6_ADDRSTRLEN];
        
        inet_ntop(AF_INET6, &config->dest6, dest_str, sizeof(dest_str));
        inet_ntop(AF_INET6, &config->gw6, gw_str, sizeof(gw_str));
        
        snprintf(value, sizeof(value), "%s/%d", dest_str, config->prefix_len);
        snprintf(path, sizeof(path), "/netd-simple:netd-config/route[destination='%s']", value);
        
        struct lyd_node *route = NULL;
        ret = lyd_new_path(NULL, yang_ctx, path, NULL, 0, &route);
        if (ret == LY_SUCCESS && route) {
            lyd_new_path(route, yang_ctx, "destination", value, 0, NULL);
            lyd_new_path(route, yang_ctx, "gateway", gw_str, 0, NULL);
            
            if (config->fib > 0) {
                snprintf(value, sizeof(value), "%d", config->fib);
                lyd_new_path(route, yang_ctx, "fib", value, 0, NULL);
            }
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
    
    // Create data tree from current system state
    // This would need to be populated with actual interface and route data
    LY_ERR ret = lyd_new_path(NULL, yang_ctx, "/netd-simple:netd-config", NULL, 0, &data_tree);
    if (ret != LY_SUCCESS) {
        snprintf(response, resp_len, "Error: Failed to create data tree\n");
        return -1;
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