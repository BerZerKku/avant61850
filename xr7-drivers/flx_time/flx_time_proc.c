/** @file flx_time_proc.c
 * @brief FLX_TIME Linux Driver PROC file
 *
 * @author Petri Anttila
 */

/*

   Flexibilis time driver for Linux

   Copyright (C) 2008 Flexibilis Oy

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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "flx_time_types.h"
#include "flx_time_proc.h"

static struct proc_dir_entry *proc_root_entry;

static int flx_time_proc_show_comp_status(struct seq_file *m, void *data)
{
    struct flx_time_comp_priv_common *cp = m->private;

    if (cp->print_status) {
        return cp->print_status(m, cp);
    }

    return seq_printf(m, "Not supported\n");
}

void flx_time_proc_init_driver(void)
{
    proc_root_entry = proc_mkdir("driver/flx_time", NULL);
    if (!proc_root_entry)
        printk(KERN_WARNING "%s: creating proc root dir entry failed.\n",
               flx_time_NAME);
}

void flx_time_proc_cleanup_driver(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    proc_remove(proc_root_entry);
#else
    remove_proc_entry("driver/flx_time", NULL);
#endif
}

static int flx_time_proc_comp_open(struct inode *inode, struct file *file)
{
    void *data;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    data = PDE_DATA(inode);
#else
    data = PDE(inode)->data;
#endif
    return single_open(file, &flx_time_proc_show_comp_status, data);
}

static const struct file_operations flx_time_comp_fops = {
    .owner = THIS_MODULE,
    .open = &flx_time_proc_comp_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

void flx_time_proc_create_comp(struct flx_time_comp_priv_common *cp)
{
    char buf[50];
    int device = cp->prop.index;
    struct proc_dir_entry *entry = NULL;

    sprintf(buf, "component_%02i_registers", device);
    // proc_create_data appeared in 2.6.26
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
    entry = proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                             &flx_time_comp_fops, cp);
#else
    entry = proc_create(buf, S_IFREG | S_IRUGO, proc_root_entry,
                        &flx_time_proc_read_comp_status);
    if (entry)
        entry->data = cp;
#endif
    if (!entry) {
        printk(KERN_WARNING "%s: creating component proc entry failed.\n",
               flx_time_NAME);
    }

}

void flx_time_proc_cleanup_comp(struct flx_time_comp_priv_common *cp)
{

    char buf[50];
    int device = cp->prop.index;

    sprintf(buf, "component_%02i_registers", device);
    remove_proc_entry(buf, proc_root_entry);
}
