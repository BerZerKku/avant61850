/** @file
 */

/*

   XRS Linux driver

   Copyright (C) 2015 Flexibilis Oy

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

#ifndef FLX_XRS_HW_TYPE_H
#define FLX_XRS_HW_TYPE_H

#include <linux/phy.h>

/// Size of XRS register address region
#define FLX_XRS_IOSIZE 0x0020

/**
 * XRS component initialisation data structure.
 * This information needs to be provided through platform_device
 * platform_data.
 */
struct flx_xrs_cfg {
    uint32_t baseaddr;          ///< register base address
#ifdef CONFIG_FLX_BUS
#ifndef CONFIG_OF
    const char *flx_bus_name;   ///< Indirect register access bus name
#endif
#endif
};

#endif
