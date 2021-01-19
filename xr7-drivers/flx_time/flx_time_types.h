/** @file
 * @brief FLX_TIME driver Internal typedefinitions
 */

/*

   Flexibilis time driver for Linux

   Copyright (C) 2009 Flexibilis Oy

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License version 2
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef FLX_TIME_TYPES_H
#define FLX_TIME_TYPES_H

/* IOCTL defines struct flx_time_type - that's used everywhere, so we
   include it through this file. */
#include "flx_time_ioctl.h"

#define flx_time_NAME "FLX_TIME"
#define flx_time_name "flx_time"

/// Forward declarations
struct flx_time_dev_priv;
struct flx_time_comp_priv_common;

/// Function prototypes.
typedef int (*get_time_data_func) (struct flx_time_comp_priv_common * cp,
                                   struct flx_time_get_data * time_data);
typedef int (*set_time_data_func) (struct flx_time_comp_priv_common * cp,
                                   struct flx_time_get_data * time_data);
typedef int (*clk_adj_func) (struct flx_time_comp_priv_common * cp,
                             struct flx_time_clock_adjust_data *
                             clk_adj_data);
typedef int (*freq_adj_func) (struct flx_time_comp_priv_common * cp,
                              struct flx_time_freq_adjust_data *
                              freq_adj_data);
typedef int (*set_baud_rate_func) (struct flx_time_comp_priv_common * cp,
                                   struct flx_time_baud_rate_ctrl * ctrl);
typedef int (*set_io_features_func) (struct flx_time_comp_priv_common * cp,
                                     struct flx_time_io_ctrl * io);
typedef int (*print_status_func) (struct seq_file * m,
                                  struct flx_time_comp_priv_common * cp);

/**
 * Struct of device private information.
 */
struct flx_time_comp_priv_common {
    struct flx_time_comp_priv_common *next;     ///< Linked list
    struct flx_time_comp_priv_common *prev;     ///< Linked list
    struct flx_if_property prop;        ///< properties as presented to user mode

    /*
     * Device interface functions.
     */
    get_time_data_func get_time_data;
    set_time_data_func set_time_data;
    clk_adj_func clk_adj;
    freq_adj_func freq_adj;
    set_baud_rate_func set_baud_rate;
    set_io_features_func set_io_features;
    print_status_func print_status;

    struct platform_device *pdev;       ///< Platform device associated with this
    void __iomem *ioaddr;       ///< Module ioaddr
};

/**
 * Struct of device private information.
 * One instance per each card.
 */
struct flx_time_dev_priv {
    struct class *class;
    struct device *this_dev;    /**<  Pointer to device class */

    /* Component privates pointers in a linked list */
    uint32_t comp_count;
    struct flx_time_comp_priv_common *first_comp;
    struct flx_time_comp_priv_common *last_comp;

};

int register_component(struct platform_device *pdev,
                       struct flx_time_comp_priv_common *cp);
void unregister_component(struct platform_device *pdev,
                          struct flx_time_comp_priv_common *cp);
int get_interface_properties(struct flx_time_comp_priv_common *cp,
                             struct flx_if_property *prop);

#endif
