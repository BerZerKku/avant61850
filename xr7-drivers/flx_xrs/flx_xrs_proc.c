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
#include <asm/uaccess.h>
#include <asm/io.h>

#include "flx_xrs_types.h"
#include "flx_xrs_proc.h"
#include "flx_xrs_if.h"

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
 * Print XRS identification registers.
 * @param to Buffer
 * @param dp Device data
 * @return Size of added data.
 */
static int flx_xrs_proc_show_regs(struct seq_file *m, void *v)
{
    struct flx_xrs_dev_priv *dp = m->private;
    int data;

    seq_printf(m, "Registers of device %i:\n", dp->dev_num);

    data = flx_xrs_read_reg(dp, XRS_REG_DEV_ID0);
    seq_printf(m, "XRS ID0\t\t\t(0x%04x): 0x%04x\n", XRS_REG_DEV_ID0, data);

    data = flx_xrs_read_reg(dp, XRS_REG_DEV_ID1);
    seq_printf(m, "XRS ID1\t\t\t(0x%04x): 0x%04x\n", XRS_REG_DEV_ID1, data);

    data = flx_xrs_read_reg(dp, XRS_REG_REV_ID);
    seq_printf(m, "XRS revision\t\t(0x%04x): %u.%u\n",
               XRS_REG_REV_ID,
               (data >> XRS_REV_ID_MAJOR_OFFSET) & XRS_REV_ID_MAJOR_MASK,
               (data >> XRS_REV_ID_MINOR_OFFSET) & XRS_REV_ID_MINOR_MASK);

    data = flx_xrs_read_reg(dp, XRS_REG_INTERNAL_REV_ID0);
    if (data >= 0) {
        uint32_t rev = data;

        data = flx_xrs_read_reg(dp, XRS_REG_INTERNAL_REV_ID1);
        if (data >= 0) {
            rev |= data << 16;
            seq_printf(m, "XRS internal revision\t(0x%04x): %u\n",
                       XRS_REG_INTERNAL_REV_ID0, rev);
        }
    }

    seq_printf(m, "\n");

    return 0;
}

static int flx_xrs_proc_regs_open(struct inode *inode,
                                  struct file *file)
{
    return single_open(file, &flx_xrs_proc_show_regs,
                       PDE_DATA(inode));
}

static const struct file_operations flx_xrs_proc_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_xrs_proc_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

/**
 * Print XRS type.
 * @param to Buffer
 * @param dp Device data
 * @return Size of added data.
 */
static int flx_xrs_proc_show_type(struct seq_file *m, void *v)
{
    struct flx_xrs_dev_priv *dp = m->private;
    int data;

    data = flx_xrs_read_reg(dp, XRS_REG_DEV_ID0);
    if (data < 0)
        return -EIO;

    seq_printf(m, "%s\n", flx_xrs_type_str(data));

    return 0;
}

static int flx_xrs_proc_type_open(struct inode *inode,
                                  struct file *file)
{
    return single_open(file, &flx_xrs_proc_show_type,
                       PDE_DATA(inode));
}

static const struct file_operations flx_xrs_proc_type_fops = {
    .owner = THIS_MODULE,
    .open = &flx_xrs_proc_type_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

/**
 * Init proc device under driver
 * @param dp Device data
 * @return 0 on success
 */
int flx_xrs_proc_init_device(struct flx_xrs_dev_priv *dp)
{
    char buf[50];
    int device = dp->dev_num;

    sprintf(buf, "device%02i_registers", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_xrs_proc_regs_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    sprintf(buf, "device%02i_type", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_xrs_proc_type_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    return 0;
}

/**
 * Cleanup device proc under driver dir.
 */
void flx_xrs_proc_cleanup_device(struct flx_xrs_dev_priv *dp)
{
    char buf[50];
    int device = dp->dev_num;

    sprintf(buf, "device%02i_registers", device);
    remove_proc_entry(buf, proc_root_entry);

    sprintf(buf, "device%02i_type", device);
    remove_proc_entry(buf, proc_root_entry);
}

/**
 * Init proc for driver
 * @return 0 on success
 */
int flx_xrs_proc_init_driver(void)
{
    proc_root_entry = proc_mkdir("driver/flx_xrs", NULL);
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
void flx_xrs_proc_cleanup_driver(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    proc_remove(proc_root_entry);
#else
    remove_proc_entry("driver/flx_xrs", NULL);
#endif
}
