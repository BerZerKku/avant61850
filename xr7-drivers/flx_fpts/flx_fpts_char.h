/** @file
 */

/*

   Flexibilis PPx Time Stamper Linux driver

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

#ifndef FLX_FPTS_CHAR_H
#define FLX_FPTS_CHAR_H

#include "flx_fpts_types.h"

int flx_fpts_register_char_device(struct flx_fpts_drv_priv *drv);
void flx_fpts_unregister_char_device(struct flx_fpts_drv_priv *drv);

#endif
