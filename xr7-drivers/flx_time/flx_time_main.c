/** @file flx_time_main.c
 * @brief FLX_TIME Linux Driver
 */

/*

   Flexibilis time driver for Linux

   Copyright (C) 2009-2012 Flexibilis Oy

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

#define DRV_NAME           "flx_time"
#define DRV_VERSION        "1.11.1"

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/version.h>

/* strcpy() */
#include <linux/string.h>

/* schedule() */
#include <linux/sched.h>

#include "flx_time_types.h"
#include "flx_time_ioctl.h"
#include "flx_time_proc.h"

/*
 * Module description information
 */

MODULE_DESCRIPTION("Flexibilis time interface driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

// Only one common time device in the system - components have separate devices
#define FLX_TIME_MAX_DEVICES 1

/***************************************************************
 * Global variables (common to whole driver, all the devices)
 ***************************************************************/

/**
 * Char device major number
 */
static int major = 0;

/** Char device registration */
static struct cdev flx_time_cdev;

/**
 * Driver private data
 */
static struct flx_time_dev_priv *device_private = NULL;

/*
 * Char Device function declaration.
 * These are described before function body
 */
static void flx_time_unregister_char_device(void);
static int flx_time_register_char_device(void);
static int flx_time_char_open(struct inode *inode, struct file *filp);
static int flx_time_char_release(struct inode *inode, struct file *filp);

#ifdef HAVE_UNLOCKED_IOCTL
static long flx_time_char_unlocked_ioctl(struct file *filp,
                                         unsigned int cmd,
                                         unsigned long arg);
#endif

static int flx_time_char_ioctl(struct inode *inode, struct file *filp,
                               unsigned int cmd, unsigned long arg);

static void flx_time_setup_cdev(void);

/*
 * Other local functions
 */


/**
 * Return number of time devices registered.
 * @param dp private data
 * @return Number of devices.
 */
uint32_t count_interfaces(struct flx_time_dev_priv *dp)
{
    /* Return just sum of all sub components */
    return dp->comp_count;
}

/**
 * Get device data.
 * @param dp private data
 * @param Device index
 * @param cp_pp Return ptr to Device data. 
 * @return 0 if found - or negative error code.
 */
int get_component_privates(struct flx_time_dev_priv *dp,
                           uint32_t index,
                           struct flx_time_comp_priv_common **cp_pp)
{
    struct flx_time_comp_priv_common *cp;
    unsigned int i;

    if (index >= count_interfaces(dp)) {
        goto err_nodev;
    }

    cp = dp->first_comp;
    for (i = 0; i < index && cp; i++) {
        cp = cp->next;
    }

    if (!cp) {
        goto err_nodev;
    }

    *cp_pp = cp;
    return 0;

  err_nodev:
    printk(KERN_DEBUG "%s: Component (index %u) is not available.\n",
           flx_time_name, index);
    return -ENXIO;
}

/**
 * Get device properties.
 * @param dp private data
 * @param prop Return with properties filled in.
 * @return 0 if ok - or negative error code.
 */
int get_interface_properties(struct flx_time_comp_priv_common *cp,
                             struct flx_if_property *prop)
{

    if (prop->index == cp->prop.index) {
        strcpy(prop->name, cp->prop.name);
        prop->type = cp->prop.type;
        prop->properties = cp->prop.properties;
        return 0;
    }

    /* Index did not match */
    printk(KERN_DEBUG
           "%s: Property (index %d) requested did not match with the stored (index %d)\n",
           flx_time_name, prop->index, cp->prop.index);
    return -EFAULT;

}

EXPORT_SYMBOL(get_interface_properties);

/**
 * Function to initialise time devices.
 * @param pdev platform_device to initialize.
 */
int flx_time_dev_init(struct platform_device *pdev)
{
    int ret = -EFAULT;
    struct flx_time_dev_priv *dp = NULL;

    if (device_private != NULL) {
        // No need to initialize
        return 0;
    }
    /// Allocate device private 
    dp = kmalloc(sizeof(struct flx_time_dev_priv), GFP_KERNEL);
    if (!dp) {
        printk(KERN_WARNING "%s: kmalloc failed.\n", flx_time_NAME);
        ret = -ENOMEM;
        goto err_1;
    }
    memset(dp, 0, sizeof(struct flx_time_dev_priv));

    dp->class = class_create(THIS_MODULE, flx_time_NAME);

    if (dp->class == NULL) {
        printk(KERN_WARNING "%s: class_create failed.\n", flx_time_NAME);
        ret = -ENOMEM;
        goto err_2;
    }
    /// Add device to class
    dp->this_dev = device_create(dp->class, &pdev->dev, MKDEV(major, 0),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
                                 NULL,
#endif
                                 "%s%d", flx_time_name, 0);
    if (IS_ERR(dp->this_dev)) {
        printk(KERN_WARNING "%s: device class registration failed.\n",
               flx_time_name);
        ret = PTR_ERR(dp->this_dev);
        goto err_3;
    }
    // Store private data ptr
    device_private = dp;

    return 0;

  err_3:
    class_destroy(dp->class);
  err_2:
    kfree(dp);
  err_1:
    return ret;
}

/**
 * Function to clean device data.
 */
void flx_time_dev_exit(struct flx_time_dev_priv *dp)
{
    printk(KERN_DEBUG "%s: Destroy time_dev.\n", flx_time_NAME);

    // remove device from class
    device_destroy(dp->this_dev->class, MKDEV(major, 0));
    dp->this_dev = NULL;

    class_destroy(dp->class);
    dp->class = NULL;

    kfree(dp);
}

/**
 * FLX_TIME driver initialization.
 * Char device is also registered for the driver.
 * @return 0 if success.
 */
static int __init flx_time_init(void)
{
    // Allocate Major Number
    flx_time_register_char_device();

    // Register Char device
    flx_time_setup_cdev();

    flx_time_proc_init_driver();

    return 0;
}

/**
 * Device module exit.
 * Unregister PCI driver and char device. FLX_TIME proc data is also cleaned up.
 */
static void __exit flx_time_cleanup(void)
{
    struct flx_time_dev_priv *dp = device_private;
    if (dp) {
        flx_time_dev_exit(dp);
        device_private = NULL;
    }

    flx_time_proc_cleanup_driver();

    // unregister char device
    cdev_del(&flx_time_cdev);

    flx_time_unregister_char_device();

    printk(KERN_DEBUG "%s: module cleanup done.\n", flx_time_NAME);
}


/**
 * File operations
 */
struct file_operations flx_time_char_fops = {
    .owner = THIS_MODULE,
#if defined(HAVE_UNLOCKED_IOCTL)
    .unlocked_ioctl = flx_time_char_unlocked_ioctl,
#else
    .ioctl = flx_time_char_ioctl,
#endif
#if defined(HAVE_COMPAT_IOCTL)
    .compat_ioctl = NULL,
#endif
    .open = flx_time_char_open,
    .release = flx_time_char_release,
};

/**
 * Char device setup
 */
static void flx_time_setup_cdev(void)
{
    int err;
    dev_t devno = MKDEV(major, 0);

    cdev_init(&flx_time_cdev, &flx_time_char_fops);

    flx_time_cdev.owner = THIS_MODULE;
    flx_time_cdev.ops = &flx_time_char_fops;

    /* Fail gracefully if need be */
    err = cdev_add(&flx_time_cdev, devno, FLX_TIME_MAX_DEVICES);
    if (err)
        printk(KERN_WARNING "Error %d adding %s0", err, DRV_NAME);

    return;
}

/**
 * FLX_TIME Char device registration.
 */
static int flx_time_register_char_device(void)
{
    dev_t dev;
    int result;

    if (major) {
        dev = MKDEV(major, 0);  // (major,minor)
        result =
            register_chrdev_region(dev, FLX_TIME_MAX_DEVICES, DRV_NAME);
    } else {
        result = alloc_chrdev_region(&dev, 0, FLX_TIME_MAX_DEVICES,
                                     DRV_NAME);
        major = MAJOR(dev);

    }
    if (result < 0) {
        printk(KERN_WARNING "%s: can't get major %d\n", DRV_NAME, major);
        return result;
    }

    printk(KERN_DEBUG "%s: char dev major %d\n", DRV_NAME, major);

    return result;
}

/**
 * FLX_TIME Char device unregistration.
 */
static void flx_time_unregister_char_device(void)
{
    dev_t devno = MKDEV(major, 0);

    unregister_chrdev_region(devno, FLX_TIME_MAX_DEVICES);

    major = 0;
}

/**
 * FLX_TIME Char device open
 */
static int flx_time_char_open(struct inode *inode, struct file *filp)
{
    unsigned int minor = iminor(inode);
    struct flx_time_dev_priv *dp = device_private;

    if (minor >= FLX_TIME_MAX_DEVICES)
        return -ENXIO;

    filp->private_data = dp;

    filp->f_op = &flx_time_char_fops;

    return 0;
}

/**
 * FLX_TIME Char device release.
 */
static int flx_time_char_release(struct inode *inode, struct file *filp)
{

    return 0;
}


#if defined(HAVE_UNLOCKED_IOCTL)
static long flx_time_char_unlocked_ioctl(struct file *filp,
                                         unsigned int cmd,
                                         unsigned long arg)
{
    /* XXX Is there need for a mutex? */

    /* XXX Let's hope that int -> long conversion works
     * In reality, it would be better(?) to have a completely separate
     * function for unlocked_ioctl.
     */
    return (long) flx_time_char_ioctl(filp->f_dentry->d_inode, filp, cmd,
                                      arg);
}
#endif

/**
 *
 */
static int flx_time_char_ioctl(struct inode *inode, struct file *filp,
                               unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    void __iomem *ioaddr;
    struct flx_time_dev_priv *dp = device_private;
    struct flx_time_comp_priv_common *cp = NULL;

    if (!dp) {
        printk(KERN_DEBUG "%s: dp not initialized.\n", flx_time_NAME);
        return -EACCES;
    }

    ioaddr = 0;

    if (_IOC_DIR(cmd) & _IOC_READ)
        if (!access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd)))
            return -EACCES;

    if (_IOC_DIR(cmd) & _IOC_WRITE)
        if (!access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd)))
            return -EACCES;

    switch (cmd) {
    case FLX_TIME_IOCTL_GET_IF_COUNT:
    {
        unsigned int i = count_interfaces(dp);
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_GET_IF_COUNT\n",flx_time_NAME);
        if (copy_to_user((void *) arg, &i, _IOC_SIZE(cmd)))     //copy value read to user
            return -EFAULT;

    }
        break;
    case FLX_TIME_IOCTL_GET_IF:
    {
        struct flx_if_property prop;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_GET_IF\n",flx_time_NAME);

        retval = copy_from_user(&prop, (void *) arg, _IOC_SIZE(cmd));
        if (retval)
            return -EFAULT;

        if (get_component_privates(dp, prop.index, &cp))
            return -EFAULT;

        if (get_interface_properties(cp, &prop) < 0)
            return -EFAULT;

        if (copy_to_user((void *) arg, &prop, _IOC_SIZE(cmd)))  //copy value read to user
            return -EFAULT;

    }
        break;

    case FLX_TIME_IOCTL_GET_DATA:
    {
        struct flx_time_get_data time_data;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_GET_DATA\n",flx_time_NAME);

        retval = copy_from_user(&time_data, (void *) arg, _IOC_SIZE(cmd));
        if (retval) {
            return -EFAULT;
        }
        if (get_component_privates(dp, time_data.index, &cp))
            return -EFAULT;

        if (cp->get_time_data == NULL) {
            return -EFAULT;
        }
        retval = cp->get_time_data(cp, &time_data);
        if (copy_to_user((void *) arg, &time_data, _IOC_SIZE(cmd))) {   //copy value read to user
            return -EFAULT;
        }

    }
        break;
    case FLX_TIME_IOCTL_CLOCK_ADJUST:
    {
        struct flx_time_clock_adjust_data clk_adj_data;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_CLOCK_ADJUST\n",flx_time_NAME);

        if (copy_from_user(&clk_adj_data, (void *) arg, _IOC_SIZE(cmd)))
            return -EFAULT;

        if (clk_adj_data.sign == 0) {
            printk(KERN_DEBUG
                   "%s: Direction (sign) missing from the data.\n",
                   flx_time_NAME);
            return -ENXIO;
        }
        if (get_component_privates(dp, clk_adj_data.index, &cp))
            return -EFAULT;

        if (cp->clk_adj == NULL) {
            return -EFAULT;
        }
        retval = cp->clk_adj(cp, &clk_adj_data);

        if (retval < 0)
            return -EFAULT;

    }
        break;
    case FLX_TIME_IOCTL_FREQ_ADJUST:
    {
        struct flx_time_freq_adjust_data freq_adj_data;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_FERQ_ADJUST\n",flx_time_NAME);

        if (copy_from_user(&freq_adj_data, (void *) arg, _IOC_SIZE(cmd)))
            return -EFAULT;

        if (get_component_privates(dp, freq_adj_data.index, &cp))
            return -EFAULT;

        if (cp->freq_adj == NULL) {
            return -EFAULT;
        }
        retval = cp->freq_adj(cp, &freq_adj_data);

        if (retval < 0)
            return -EFAULT;

    }
        break;
    case FLX_TIME_IOCTL_SET_PPS_GEN:
    {
        printk(KERN_DEBUG "%s: PPS adjustment is not supported.\n",
               flx_time_NAME);
        return -EFAULT;
    }
        break;
#if 0                           // Not supported currently
    case FLX_TIME_IOCTL_GET_HISTOGRAM:
    {
        struct flx_time_get_histogram_data data;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_GET_HISTOGRAM\n",flx_time_NAME);

        if (copy_from_user(&data, (void *) arg, _IOC_SIZE(cmd)))
            return -EFAULT;

        if (get_component_privates(dp, data.index, &cp))
            return -EFAULT;

        if (get_histogram(cp, &data) < 0)
            return -EFAULT;

        if (copy_to_user((void *) arg, &data, _IOC_SIZE(cmd)))  //copy value read to user
            return -EFAULT;

    }
        break;
    case FLX_TIME_IOCTL_HISTOGRAM_ADJUST:
    {
        struct flx_time_histogram_adjust_data data;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_HISTOGRAM_ADJUST\n",flx_time_NAME);

        if (copy_from_user(&data, (void *) arg, _IOC_SIZE(cmd)))
            return -EFAULT;

        if (get_component_privates(dp, data.index, &cp))
            return -EFAULT;

        if (move_histogram_window(cp, &data) < 0)
            return -EFAULT;

    }
        break;
#endif
    case FLX_TIME_IOCTL_SET_IRIG_DATA:
    case FLX_TIME_IOCTL_SEND_NMEA_DATA:
    {
        struct flx_time_get_data data;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_SEND_NMEA_DATA\n", flx_time_NAME);

        if (copy_from_user(&data, (void *) arg, _IOC_SIZE(cmd)))
            return -EFAULT;

        if (get_component_privates(dp, data.index, &cp))
            return -EFAULT;

        if (cp->set_time_data == NULL) {
            return -EFAULT;
        }
        retval = cp->set_time_data(cp, &data);

        if (retval < 0)
            return -EFAULT;
    }
        break;
    case FLX_TIME_IOCTL_SET_BAUD_RATE:
    {
        struct flx_time_baud_rate_ctrl ctrl;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_PPS_GEN\n",flx_time_NAME);

        if (copy_from_user(&ctrl, (void *) arg, _IOC_SIZE(cmd)))
            return -EFAULT;

        if (get_component_privates(dp, ctrl.index, &cp))
            return -EFAULT;

        if (cp->set_baud_rate == NULL) {
            return -EFAULT;
        }
        retval = cp->set_baud_rate(cp, &ctrl);

        if (retval < 0)
            return -EFAULT;
    }
        break;
    case FLX_TIME_IOCTL_SET_IO:
    {
        struct flx_time_io_ctrl ctrl;
        //printk (KERN_DEBUG "%s: FLX_TIME_IOCTL_PPS_GEN\n",flx_time_NAME);

        if (copy_from_user(&ctrl, (void *) arg, _IOC_SIZE(cmd)))
            return -EFAULT;

        if (get_component_privates(dp, ctrl.index, &cp))
            return -EFAULT;

        if (cp->set_io_features == NULL) {
            return -EFAULT;
        }
        retval = cp->set_io_features(cp, &ctrl);

        if (retval < 0)
            return -EFAULT;
    }
        break;
    default:
        printk(KERN_DEBUG "%s: Unknown ioctl command 0x%x.\n",
               flx_time_NAME, cmd);
        return -ENOTTY;
    }

    return retval;

}

/**
 * Register time component.
 * @param pdev Platform device handle
 * @param cp Component data
 * @return Component number or negative error code.
 */
int register_component(struct platform_device *pdev,
                       struct flx_time_comp_priv_common *cp)
{
    struct flx_time_dev_priv *dp = NULL;
    uint32_t component_num = 0;
    int ret = 0;

    ret = flx_time_dev_init(pdev);
    if (ret != 0) {
        return ret;
    }

    dp = device_private;
    if (!dp) {
        printk(KERN_DEBUG "%s: dp not initialized.\n", flx_time_NAME);
        return -ENOMEM;
    }

    // Keep count 
    dp->comp_count++;

    // Add to the end of list.
    if (dp->last_comp) {
        dp->last_comp->next = cp;
        cp->prev = dp->last_comp;
        component_num = dp->last_comp->prop.index + 1;
    } else {
        cp->prev = NULL;
    }
    dp->last_comp = cp;
    // If first, update first_comp
    if (dp->first_comp == NULL) {
        dp->first_comp = cp;
    }

    printk(KERN_DEBUG
           "%s: Component %s (index %d/%d) registered\n",
           flx_time_NAME, dp->last_comp->prop.name,
           dp->last_comp->prop.index, dp->comp_count);

    // Create component proc
    flx_time_proc_create_comp(cp);

    return component_num;
}

EXPORT_SYMBOL(register_component);

void unregister_component(struct platform_device *pdev,
                          struct flx_time_comp_priv_common *cp)
{
    struct flx_time_comp_priv_common *tmp = NULL;
    struct flx_time_dev_priv *dp = NULL;

    // get private
    dp = device_private;
    if (!dp) {
        printk(KERN_DEBUG "%s: dp not initialized.\n", flx_time_NAME);
        return;
    }

    printk(KERN_WARNING
           "%s: Component %s (index %d) unregister\n",
           flx_time_NAME, cp->prop.name, cp->prop.index);

    // Remove proc for component
    flx_time_proc_cleanup_comp(cp);

    // Remove from the list
    for (tmp = dp->first_comp; tmp; tmp = tmp->next) {
        if (tmp == cp) {
            // Found
            break;
        }
    }
    if (!tmp) {
        printk(KERN_WARNING
               "%s: Component %s (index %d) not found\n",
               flx_time_NAME, cp->prop.name, cp->prop.index);
        return;
    }
    // Remove from double linked list
    if (cp->prev) {
        cp->prev->next = cp->next;
    } else {                    //First, update head
        dp->first_comp = cp->next;
    }

    if (cp->next) {
        cp->next->prev = cp->prev;
    } else {
        dp->last_comp = cp->prev;
    }
}

EXPORT_SYMBOL(unregister_component);


// Module init and exit functions.
module_init(flx_time_init);
module_exit(flx_time_cleanup);
