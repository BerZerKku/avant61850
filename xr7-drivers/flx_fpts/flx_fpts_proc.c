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

#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "flx_fpts_types.h"
#include "flx_fpts_proc.h"
#include "flx_fpts_if.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#define PDE_DATA(inode) PDE(inode)->data
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
static inline struct proc_dir_entry *proc_create_data(
        const char *name,
        umode_t mode,
        struct proc_dir_entry *parent,
        const struct file_operations *proc_fops,
        void *data)
{
    struct proc_dir_entry *entry =
        proc_create(name, mode, parent, proc_fops);

    if (entry)
        entry->data = data;

    return entry;
}
#endif

static struct proc_dir_entry *proc_root_entry;

/**
 * Convert mode of operation to string.
 */
static const char *flx_fpts_mode_str(enum flx_fpts_mode mode)
{
    switch (mode) {
    case FLX_FPTS_MODE_INTERRUPT: return "interrupt driven";
    case FLX_FPTS_MODE_POLL: return "poll";
    case FLX_FPTS_MODE_DIRECT: return "direct";
    }

    return "(invalid)";
}

/**
 * Print switch registers.
 * @param to Buffer
 * @param dp Device data
 * @return Size of added data.
 */
static int flx_fpts_proc_show_regs(struct seq_file *m, void *v)
{
    struct flx_fpts_dev_priv *dp = m->private;
    struct timespec interval = { .tv_sec = 0 };
    struct flx_fpts_event last_event = dp->last_event;
    int data;
    uint64_t sec = 0;
    uint32_t nsec = 0;
    uint32_t pulses = 0;

    jiffies_to_timespec(dp->poll_interval, &interval);

    seq_printf(m, "Registers of device %i:\n\n", dp->dev_num);

    data = flx_fpts_read_reg(dp, FPTS_REG_TS_CTRL);
    seq_printf(m, "TS CTRL\t\t\t(0x%04x): 0x%04x\n", FPTS_REG_TS_CTRL, data);

    data = flx_fpts_read_reg(dp, FPTS_REG_INT_MASK);
    seq_printf(m, "INT MASK\t\t(0x%04x): 0x%04x\n", FPTS_REG_INT_MASK, data);

    data = flx_fpts_read_reg(dp, FPTS_REG_INT_STAT);
    seq_printf(m, "INT STATUS\t\t(0x%04x): 0x%04x\n", FPTS_REG_INT_STAT, data);

    data = flx_fpts_read_reg(dp, FPTS_REG_TS_SEC0);
    if (data >= 0)
        sec |= data;
    data = flx_fpts_read_reg(dp, FPTS_REG_TS_SEC1);
    if (data >= 0)
        sec |= (uint64_t)data << 16;
    data = flx_fpts_read_reg(dp, FPTS_REG_TS_SEC2);
    if (data >= 0)
        sec |= (uint64_t)data << 32;
    sec &= FPTS_TS_SEC_MASK;
    seq_printf(m, "Seconds\t\t\t(0x%04x): %llu\n", FPTS_REG_TS_SEC0, sec);

    data = flx_fpts_read_reg(dp, FPTS_REG_TS_NSEC0);
    if (data >= 0)
        nsec |= data;
    data = flx_fpts_read_reg(dp, FPTS_REG_TS_NSEC1);
    if (data >= 0)
        nsec |= data << 16;
    nsec &= FPTS_TS_NSEC_MASK;
    seq_printf(m, "Nanoseconds\t\t(0x%04x): %u\n", FPTS_REG_TS_NSEC0, nsec);

    data = flx_fpts_read_reg(dp, FPTS_REG_PCNT0);
    if (data >= 0)
        pulses |= data;
    data = flx_fpts_read_reg(dp, FPTS_REG_PCNT1);
    if (data >= 0)
        pulses |= data << 16;
    seq_printf(m, "Pulse count\t\t(0x%04x): %u\n", FPTS_REG_PCNT0, pulses);

    seq_printf(m, "\n");

    seq_printf(m, "Interrupt count:\t%lu\n", dp->irq_count);
#ifdef CONFIG_FLX_BUS
    seq_printf(m, "Interrupt work count:\t%lu\n", dp->irq_work_count);
#endif
    seq_printf(m, "Poll work count:\t%lu\n", dp->poll_work_count);
    seq_printf(m, "\n");

    seq_printf(m, "Mode:\t\t\t%s\n", flx_fpts_mode_str(dp->mode));
    seq_printf(m, "Polling interval:\t%lu s %lu ns\n",
               interval.tv_sec, interval.tv_nsec);
    seq_printf(m, "Polling interval:\t%u ms\n",
               jiffies_to_msecs(dp->poll_interval));
    seq_printf(m, "\n");

    seq_printf(m, "Last event:\n");

    seq_printf(m, "    Seconds:\t\t%llu\n", last_event.sec);
    seq_printf(m, "    Nanoseconds:\t%u\n", last_event.nsec);
    seq_printf(m, "    Pulse count:\t%u\n", last_event.counter);
    seq_printf(m, "\n");

    return 0;
}

/**
 * Init proc for driver
 * @return 0 on success
 */
int flx_fpts_proc_init_driver(void)
{
    proc_root_entry = proc_mkdir("driver/flx_fpts", NULL);
    if (!proc_root_entry) {
        printk(KERN_WARNING DRV_NAME
               ": creating proc root dir entry failed\n");
        return -EFAULT;
    }

    return 0;
}

/**
 * Cleanup driver
 */
void flx_fpts_proc_cleanup_driver(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    proc_remove(proc_root_entry);
#else
    remove_proc_entry("driver/flx_fpts", NULL);
#endif
}

static int flx_fpts_proc_regs_open(struct inode *inode,
                                         struct file *file)
{
    return single_open(file, &flx_fpts_proc_show_regs,
                       PDE_DATA(inode));
}

static const struct file_operations flx_fpts_proc_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_fpts_proc_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

/**
 * Init proc device under driver
 * @param dp Device data
 * @return 0 on success
 */
int flx_fpts_proc_init_device(struct flx_fpts_dev_priv *dp)
{
    char buf[50];

    sprintf(buf, "device%02u_status", dp->dev_num);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_fpts_proc_regs_fops, dp))) {
        dev_dbg(&dp->pdev->dev, "creating proc entry %s failed.\n", buf);
    }

    return 0;
}

/**
 * Cleanup device proc under driver dir.
 */
void flx_fpts_proc_cleanup_device(struct flx_fpts_dev_priv *dp)
{
    char buf[50];

    sprintf(buf, "device%02u_status", dp->dev_num);
    remove_proc_entry(buf, proc_root_entry);
}
