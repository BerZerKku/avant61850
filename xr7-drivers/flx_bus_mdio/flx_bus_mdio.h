/** @file
 */

/*

   Indirect register access via MDIO Linux driver

   Copyright (C) 2014 Flexibilis Oy

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

#ifndef FLX_BUS_MDIO_H
#define FLX_BUS_MDIO_H

// MDIO slave

/// MDIO slave high address bits register
#define FLX_BUS_MDIO_SLAVE_REG_AA1      0x11

/// MDIO slave low address bits register
#define FLX_BUS_MDIO_SLAVE_REG_AA0      0x10
#define FLX_BUS_MDIO_SLAVE_AA0_ADDR     0xfffe  ///< Address bitmask
#define FLX_BUS_MDIO_SLAVE_AA0_READ     0x00    ///< Read access
#define FLX_BUS_MDIO_SLAVE_AA0_WRITE    0x01    ///< Write access

/// MDIO slave data register
#define FLX_BUS_MDIO_SLAVE_REG_AD       0x14

// MDIO bridge

#ifndef FLX_BUS_MDIO_PHY_ID
/// Default bridge PHY ID is "disabled"
#define FLX_BUS_MDIO_PHY_ID             UINT_MAX
#define FLX_BUS_MDIO_PHY_ID_MASK        UINT_MAX
#define FLX_BUS_MDIO_PHY_ADDR_MASK      0
#endif

/// Bridge indirect address register
#define FLX_BUS_MDIO_BRIDGE_REG_AA      0x1e
#define FLX_BUS_MDIO_BRIDGE_AA_READ     (0u << 15)      ///< Read access
#define FLX_BUS_MDIO_BRIDGE_AA_WRITE    (1u << 15)      ///< Write access
/// Bitmask for address
#define FLX_BUS_MDIO_BRIDGE_AA_ADDR_MASK \
    (~FLX_BUS_MDIO_BRIDGE_AA_WRITE)
/// Get address bits for AA register
#define FLX_BUS_MDIO_BRIDGE_AA_ADDR(addr) \
    (((addr) >> 1) & FLX_BUS_MDIO_BRIDGE_AA_ADDR_MASK)
/// Bridge indirect data register
#define FLX_BUS_MDIO_BRIDGE_REG_AD      0x1f

#endif
