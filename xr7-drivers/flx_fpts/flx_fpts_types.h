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

#ifndef FLX_FPTS_TYPES_H
#define FLX_FPTS_TYPES_H

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>

#include "flx_fpts_api.h"

#define DRV_NAME "flx_fpts"

#define FLX_FPTS_MAX_DEVICES  32

// __devinit and friends disappeared in Linux 3.8.
#ifndef __devinit
#define __devinit
#define __devexit
#endif

struct flx_fpts_drv_priv;
struct flx_fpts_dev_priv;

/**
 * FPTS operations for supporting different FPTS register access methods.
 */
struct flx_fpts_ops {
    /// Function to read FPTS register
    int (*read_reg) (struct flx_fpts_dev_priv *dp, int reg);
    /// Function to write FPTS register
    int (*write_reg) (struct flx_fpts_dev_priv *dp, int reg, uint16_t value);
};

/**
 * Register access context.
 */
struct flx_fpts_reg_access {
    struct flx_fpts_ops ops;            ///< register access operations
#ifdef CONFIG_FLX_BUS
    struct flx_bus *flx_bus;            ///< indirect register access bus
#endif
    union {
        void __iomem *ioaddr;           ///< memory mapped I/O address
        uint32_t addr;                  ///< indirect access bus address
    };
};

/**
 * FPTS device private information structure.
 */
struct flx_fpts_dev_priv {
    struct list_head list;              ///< linked list
    struct flx_fpts_drv_priv *drv;      ///< back reference
    struct platform_device *pdev;       ///< FPTS platform device
    struct device *class_dev;           ///< pointer to device with class
    unsigned int dev_num;               ///< number of this device
    unsigned int irq;                   ///< interrupt number
    enum flx_fpts_mode mode;            ///< current mode of operation
    unsigned long int poll_interval;    ///< poll interval in jiffies

    unsigned long int irq_count;        ///< interrupt counter
    struct delayed_work poll_work;      ///< polling mode event read work
    unsigned long int poll_work_count;  ///< interrupt work counter
#ifdef CONFIG_FLX_BUS
    struct work_struct irq_work;        ///< for indirect register access
    atomic_t irq_disable;               ///< refcount for disabling interrupt
    unsigned long int irq_work_count;   ///< interrupt work counter
#endif
    struct flx_fpts_reg_access regs;     ///< register access

    wait_queue_head_t read_waitq;       ///< wait queue to inform readability
    spinlock_t buf_lock;                ///< synchronize buffer index changes
    unsigned int use_count;             ///< number of users of this device
    struct mutex read_lock;             ///< synchronize char device reads
    unsigned int buf_count;             ///< number of events buffered
    unsigned int read_count;            ///< number of events read from buffer
    unsigned int buf_size;              ///< number of events room in buf
    struct flx_fpts_event *buf;         ///< buffered events

    struct flx_fpts_event last_event;   ///< last read event information
};

/**
 * FPTS driver private information structure.
 */
struct flx_fpts_drv_priv {
    struct list_head devices;           ///< our devices
    /// used device numbers
    DECLARE_BITMAP(used_devices, FLX_FPTS_MAX_DEVICES);
    dev_t first_devno;                  ///< first character device
    struct class class;                 ///< device class
    struct cdev cdev;                   ///< our character device
    struct workqueue_struct *wq;        ///< work queue for delayed work
};

static inline int flx_fpts_read_reg(struct flx_fpts_dev_priv *dp,
                                    int reg)
{
    return dp->regs.ops.read_reg(dp, reg);
}

static inline int flx_fpts_write_reg(struct flx_fpts_dev_priv *dp,
                                     int reg, uint16_t value)
{
    return dp->regs.ops.write_reg(dp, reg, value);
}

#endif
