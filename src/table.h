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
 * @file table.h
 * @brief Header for common table formatting functionality
 * 
 * This header provides function declarations for the table formatting
 * system used by both show route and show interface commands.
 */

#ifndef TABLE_H
#define TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

// Table formatting functions
void print_table_header(void *table);
void print_table_row(void *table, const char **values);
void update_column_widths(void *table, const char **values);
void precalculate_column_widths(void *table, const char ***data_rows, int num_rows);

// Interface table functions
void print_interface_row(const char *ifname, const char *ipv4, const char *ipv6, 
                        const char *vrf, const char *tunnel_vrf, const char *mtu);

// Route table functions
void print_route_row(const char *dest, const char *gateway, const char *flags,
                    const char *interface, const char *description);

// Utility functions
void reset_table(int table_type);
void print_status(const char *message);
void print_error(const char *message);
void print_warning(const char *message);
void print_section_header(const char *title);
void print_subsection_header(const char *title);

#ifdef __cplusplus
}
#endif

#endif // TABLE_H 