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
 * @file table.c
 * @brief Common table formatting functionality for network data display
 * 
 * This file provides a unified table formatting system for displaying
 * network configuration data in a consistent, well-formatted way.
 * It handles:
 * - Table headers with proper column alignment
 * - Row formatting with consistent spacing
 * - Dynamic column width calculation
 * - Support for different table types (interfaces, routes, etc.)
 * - Proper handling of empty or missing data
 */

#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Table column definition structure
typedef struct {
    const char *name;      // Column header name
    int min_width;         // Minimum column width in characters
    int current_width;     // Current calculated width
    int align_left;        // 1 for left alignment, 0 for right
} table_column_t;

// Table structure
typedef struct {
    const char *title;           // Table title
    table_column_t *columns;     // Column definitions
    int num_columns;            // Number of columns
    int header_printed;         // Whether header has been printed
    int widths_calculated;      // Whether column widths have been calculated
} table_t;

// Interface table columns
static table_column_t interface_columns[] = {
    {"Interface", 12, 0, 1},
    {"IPv4 Address", 18, 0, 1},
    {"IPv6 Address", 18, 0, 1},
    {"VRF", 8, 0, 1},
    {"TunnelVRF", 8, 0, 1},
    {"MTU", 6, 0, 0}
};

// Route table columns
static table_column_t route_columns[] = {
    {"Destination", 18, 0, 1},
    {"Gateway", 18, 0, 1},
    {"Flags", 9, 0, 1},
    {"Interface", 10, 0, 1},
    {"Description", 20, 0, 1}
};

// Static table instances
static table_t interface_table = {
    "Interface Configuration",
    interface_columns,
    sizeof(interface_columns) / sizeof(interface_columns[0]),
    0,
    0
};

static table_t route_table = {
    "Routing Table",
    route_columns,
    sizeof(route_columns) / sizeof(route_columns[0]),
    0,
    0
};

/**
 * Calculate optimal column widths based on header and data
 * @param table Pointer to table structure
 */
static void calculate_column_widths(table_t *table) {
    if (table->widths_calculated) {
        return;
    }
    
    // Initialize widths to minimum values
    for (int i = 0; i < table->num_columns; i++) {
        table_column_t *col = &table->columns[i];
        col->current_width = col->min_width;
        
        // Check if header is longer than minimum
        int header_len = strlen(col->name);
        if (header_len > col->current_width) {
            col->current_width = header_len;
        }
    }
    
    table->widths_calculated = 1;
}

/**
 * Set reasonable column widths based on expected data patterns
 * @param table Pointer to table structure
 */
static void set_reasonable_widths(table_t *table) {
    if (table == &route_table) {
        // Route table specific widths
        table->columns[0].current_width = 25;  // Destination - accommodate IPv6
        table->columns[1].current_width = 18;  // Gateway - accommodate IPv4
        table->columns[2].current_width = 6;   // Flags - "UGS" + padding
        table->columns[3].current_width = 10;  // Interface - "em0" + padding
        table->columns[4].current_width = 8;   // Description - reasonable default
    } else if (table == &interface_table) {
        // Interface table specific widths
        table->columns[0].current_width = 12;  // Interface
        table->columns[1].current_width = 18;  // IPv4 Address
        table->columns[2].current_width = 18;  // IPv6 Address
        table->columns[3].current_width = 8;   // VRF
        table->columns[4].current_width = 8;   // TunnelVRF
        table->columns[5].current_width = 6;   // MTU
    }
}

/**
 * Update column widths based on a data row
 * @param table Pointer to table structure
 * @param values Array of string values for each column
 */
void update_column_widths(table_t *table, const char **values) {
    // Only update widths if header hasn't been printed yet
    if (table->header_printed) {
        return;
    }
    
    // Ensure widths are initialized
    if (!table->widths_calculated) {
        calculate_column_widths(table);
    }
    
    // Update column widths based on current row data
    for (int i = 0; i < table->num_columns; i++) {
        table_column_t *col = &table->columns[i];
        const char *value = (i < table->num_columns && values[i]) ? values[i] : "-";
        int value_len = strlen(value);
        
        if (value_len > col->current_width) {
            col->current_width = value_len;
        }
    }
}

/**
 * Pre-calculate optimal column widths from all data rows
 * @param table Pointer to table structure
 * @param data_rows Array of data row arrays
 * @param num_rows Number of data rows
 */
void precalculate_column_widths(table_t *table, const char ***data_rows, int num_rows) {
    // Initialize widths to minimum values
    calculate_column_widths(table);
    
    // Process all data rows to find maximum widths
    for (int row = 0; row < num_rows; row++) {
        const char **values = data_rows[row];
        update_column_widths(table, values);
    }
}

/**
 * Print table header
 * @param table Pointer to table structure
 */
void print_table_header(table_t *table) {
    if (table->header_printed) {
        return;
    }
    
    // Set reasonable column widths based on table type
    set_reasonable_widths(table);
    
    // Lock the widths - no more changes after header is printed
    table->widths_calculated = 1;
    
    // Print title if provided
    if (table->title) {
        printf("%s\n", table->title);
    }
    
    // Print column headers
    for (int i = 0; i < table->num_columns; i++) {
        table_column_t *col = &table->columns[i];
        printf("%-*s", col->current_width, col->name);
        if (i < table->num_columns - 1) {
            printf(" ");
        }
    }
    printf("\n");
    
    // Print separator line
    for (int i = 0; i < table->num_columns; i++) {
        table_column_t *col = &table->columns[i];
        for (int j = 0; j < col->current_width; j++) {
            printf("-");
        }
        if (i < table->num_columns - 1) {
            printf(" ");
        }
    }
    printf("\n");
    
    table->header_printed = 1;
}

/**
 * Print a formatted table row
 * @param table Pointer to table structure
 * @param values Array of string values for each column
 */
void print_table_row(table_t *table, const char **values) {
    // Print header first if not already printed
    if (!table->header_printed) {
        print_table_header(table);
    }
    
    // Print each column value using the fixed widths from header
    for (int i = 0; i < table->num_columns; i++) {
        table_column_t *col = &table->columns[i];
        const char *value = (i < table->num_columns && values[i]) ? values[i] : "-";
        
        if (col->align_left) {
            printf("%-*s", col->current_width, value);
        } else {
            printf("%*s", col->current_width, value);
        }
        
        if (i < table->num_columns - 1) {
            printf(" ");
        }
    }
    printf("\n");
}

/**
 * Print interface table row
 * @param ifname Interface name
 * @param ipv4 IPv4 address (can be NULL)
 * @param ipv6 IPv6 address (can be NULL)
 * @param vrf VRF name (can be NULL)
 * @param tunnel_vrf Tunnel VRF name (can be NULL)
 * @param mtu MTU value (can be NULL)
 */
void print_interface_row(const char *ifname, const char *ipv4, const char *ipv6, 
                        const char *vrf, const char *tunnel_vrf, const char *mtu) {
    const char *values[6];
    values[0] = ifname;
    values[1] = ipv4;
    values[2] = ipv6;
    values[3] = vrf;
    values[4] = tunnel_vrf;
    values[5] = mtu;
    
    print_table_row(&interface_table, values);
}

/**
 * Print route table row
 * @param dest Destination prefix
 * @param gateway Gateway address
 * @param flags Route flags
 * @param interface Interface name
 * @param description Route description (can be NULL)
 */
void print_route_row(const char *dest, const char *gateway, const char *flags,
                    const char *interface, const char *description) {
    const char *values[5];
    values[0] = dest;
    values[1] = gateway;
    values[2] = flags;
    values[3] = interface;
    values[4] = description;
    
    print_table_row(&route_table, values);
}

/**
 * Reset table state (useful for multiple table displays)
 * @param table_type Type of table to reset (0 for interface, 1 for route)
 */
void reset_table(int table_type) {
    if (table_type == 0) {
        interface_table.header_printed = 0;
        interface_table.widths_calculated = 0;
        // Reset column widths to minimum
        for (int i = 0; i < interface_table.num_columns; i++) {
            interface_table.columns[i].current_width = interface_table.columns[i].min_width;
        }
    } else if (table_type == 1) {
        route_table.header_printed = 0;
        route_table.widths_calculated = 0;
        // Reset column widths to minimum
        for (int i = 0; i < route_table.num_columns; i++) {
            route_table.columns[i].current_width = route_table.columns[i].min_width;
        }
    }
}

/**
 * Print a simple status message
 * @param message Status message to print
 */
void print_status(const char *message) {
    printf("%s\n", message);
}

/**
 * Print an error message
 * @param message Error message to print
 */
void print_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}

/**
 * Print a warning message
 * @param message Warning message to print
 */
void print_warning(const char *message) {
    fprintf(stderr, "Warning: %s\n", message);
}

/**
 * Print a section header
 * @param title Section title
 */
void print_section_header(const char *title) {
    printf("\n%s\n", title);
    for (int i = 0; i < strlen(title); i++) {
        printf("=");
    }
    printf("\n");
}

/**
 * Print a subsection header
 * @param title Subsection title
 */
void print_subsection_header(const char *title) {
    printf("\n%s\n", title);
    for (int i = 0; i < strlen(title); i++) {
        printf("-");
    }
    printf("\n");
} 