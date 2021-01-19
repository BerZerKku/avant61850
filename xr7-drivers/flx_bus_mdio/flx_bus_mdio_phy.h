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

#ifndef FLX_BUS_MDIO_PHY_H
#define FLX_BUS_MDIO_PHY_H

#include "flx_bus_mdio_types.h"

int flx_bus_mdio_phy_init(struct flx_bus_mdio_dev_priv *dp);
void flx_bus_mdio_phy_cleanup(struct flx_bus_mdio_dev_priv *dp);

#endif
