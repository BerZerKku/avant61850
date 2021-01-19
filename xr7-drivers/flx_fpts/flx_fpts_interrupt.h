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

#ifndef FLX_FPTS_INTERRUPT_H
#define FLX_FPTS_INTERRUPT_H

#include "flx_fpts_types.h"

int flx_fpts_init_interrupt(struct flx_fpts_dev_priv *dp);
void flx_fpts_cleanup_interrupt(struct flx_fpts_dev_priv *dp);

// Select spinlock implementation
#ifdef CONFIG_FLX_BUS

#define flx_fpts_spin_lock(lock, flags) spin_lock(lock)
#define flx_fpts_spin_unlock(lock, flags) spin_unlock(lock)

#else

#define flx_fpts_spin_lock(lock, flags) spin_lock_irqsave(lock, flags)
#define flx_fpts_spin_unlock(lock, flags) spin_unlock_irqrestore(lock, flags)

#endif

#endif
