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

#ifndef FLX_XRS_PROC_H
#define FLX_XRS_PROC_H

#include "flx_xrs_types.h"

int flx_xrs_proc_init_driver(void);
void flx_xrs_proc_cleanup_driver(void);
int flx_xrs_proc_init_device(struct flx_xrs_dev_priv *dp);
void flx_xrs_proc_cleanup_device(struct flx_xrs_dev_priv *dp);

#endif
