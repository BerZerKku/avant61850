/** @file flx_time_proc.h
 * @brief FLX_TIME Linux Driver PROC header file
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

#ifndef FLX_TIME_PROC_H
#define FLX_TIME_PROC_H

#include "flx_time_types.h"

extern void flx_time_proc_init_driver(void);
extern void flx_time_proc_cleanup_driver(void);
extern void flx_time_proc_create_comp(struct flx_time_comp_priv_common *cp);
extern void flx_time_proc_cleanup_comp(struct flx_time_comp_priv_common *cp);

#endif
