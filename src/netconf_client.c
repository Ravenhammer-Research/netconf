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
// For now, we'll implement a simplified NETCONF client
// that generates proper NETCONF XML messages without using libnetconf2 session management
// since we're communicating directly over UNIX sockets

int netconf_connect(const char *host __attribute__((unused)), int port __attribute__((unused))) {
    // For UNIX socket communication, no special connection setup needed
    // The client already connects to the server via UNIX socket
    return 0;
}

int netconf_disconnect(void) {
    // No special cleanup needed for UNIX socket communication
    return 0;
}

int netconf_get_config(const char *filter, char *response, size_t resp_len) {
    // Send NETCONF get-config message
    // This should use proper NETCONF XML format
    
    // Create NETCONF get-config message
    char *xml_msg = NULL;
    if (filter && strlen(filter) > 0) {
        asprintf(&xml_msg, 
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
            "  <get-config>\n"
            "    <source>\n"
            "      <running/>\n"
            "    </source>\n"
            "    <filter type=\"subtree\">\n"
            "      %s\n"
            "    </filter>\n"
            "  </get-config>\n"
            "</rpc>\n", filter);
    } else {
        asprintf(&xml_msg,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<rpc message-id=\"1\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
            "  <get-config>\n"
            "    <source>\n"
            "      <running/>\n"
            "    </source>\n"
            "  </get-config>\n"
            "</rpc>\n");
    }
    
    if (xml_msg == NULL) {
        return -1;
    }
    
    // Copy the message to response
    strncpy(response, xml_msg, resp_len - 1);
    response[resp_len - 1] = '\0';
    
    free(xml_msg);
    return 0;
}

int netconf_edit_config(const char *config, char *response, size_t resp_len) {
    // Send NETCONF edit-config message
    
    // Create NETCONF edit-config message
    char *xml_msg = NULL;
    asprintf(&xml_msg,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<rpc message-id=\"2\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <edit-config>\n"
        "    <target>\n"
        "      <running/>\n"
        "    </target>\n"
        "    <config>\n"
        "      %s\n"
        "    </config>\n"
        "  </edit-config>\n"
        "</rpc>\n", config);
    
    if (xml_msg == NULL) {
        return -1;
    }
    
    // Copy the message to response
    strncpy(response, xml_msg, resp_len - 1);
    response[resp_len - 1] = '\0';
    
    free(xml_msg);
    return 0;
}

int netconf_commit(char *response, size_t resp_len) {
    // Send NETCONF commit message
    
    // Create NETCONF commit message
    const char *xml_msg = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<rpc message-id=\"3\" xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <commit/>\n"
        "</rpc>\n";
    
    strncpy(response, xml_msg, resp_len - 1);
    response[resp_len - 1] = '\0';
    
    return 0;
} 