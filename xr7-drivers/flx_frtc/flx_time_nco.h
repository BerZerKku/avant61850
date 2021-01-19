/** @file
 */

/*

   Flexibilis Real-Time Clock Linux driver

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

#ifndef FLX_TIME_NCO_H
#define FLX_TIME_NCO_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/io.h>

#include <flx_time/flx_time_types.h>

/*
 * Other local functions
 */

int read_nco_time(struct flx_time_comp_priv *nco,
                  struct flx_time_get_data *time);
int nco_adj_time(struct flx_time_comp_priv *nco,
                 struct flx_time_clock_adjust_data *clk_adj_data);
int nco_adj_freq(struct flx_time_comp_priv *nco,
                 struct flx_time_freq_adjust_data *freq_adj_data);

int init_nco_registers(struct flx_time_comp_priv *cp);

#endif
