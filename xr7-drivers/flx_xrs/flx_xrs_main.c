/** @file
 */

/*

   XRS700x Linux driver

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

#define DRV_VERSION        "1.11.1"

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#include "flx_xrs_types.h"
#include "flx_xrs_guard.h"
#include "flx_xrs_hw_type.h"
#include "flx_xrs_proc.h"
#include "flx_xrs_if.h"

#ifdef CONFIG_FLX_BUS
# include <flx_bus/flx_bus.h>
#endif

// Module description information
MODULE_DESCRIPTION("Flexibilis XRS7000 ID driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

// Driver private data
static struct flx_xrs_drv_priv flx_xrs_drv_priv = {
};

/**
 * Get access to driver privates.
 */
static struct flx_xrs_drv_priv *flx_xrs_get_drv_priv(void)
{
    return &flx_xrs_drv_priv;
}

/**
 * Find XRS device privates.
 * @param dev XRS device.
 * @return XRS device privates or NULL.
 */
static struct flx_xrs_dev_priv *flx_xrs_find_dev(struct device *dev)
{
    struct flx_xrs_drv_priv *drv = flx_xrs_get_drv_priv();
    struct flx_xrs_dev_priv *dp;

    list_for_each_entry(dp, &drv->devices, list) {
        if (dp->this_dev == dev)
            return dp;
    }

    return NULL;
}

#ifdef CONFIG_FLX_BUS
/**
 * Determine bus address for given XRS register.
 * @param base Bus base address.
 * @param reg XRS register address.
 * @return Bus address for register.
 */
static inline uint32_t flx_xrs_bus_addr(struct flx_xrs_reg_access *regs,
                                        int reg)
{
    return regs->addr + reg;
}

/**
 * Read register via indirect access.
 * @param dp Device privates.
 * @param regnum Register number.
 * @return Read value.
 */
static int flx_xrs_read_reg_indirect(struct flx_xrs_dev_priv *dp, int reg)
{
    struct flx_xrs_reg_access *regs = &dp->regs;
    uint16_t value = 0xffff;
    int ret;

    ret = flx_bus_read16(regs->flx_bus, flx_xrs_bus_addr(regs, reg), &value);
    if (ret < 0)
        return ret;
    return value;
}

/**
 * Init indirect register access.
 * @param dp Device privates.
 * @param io_res IO resource handle from platform config.
 * @param frs_cfg FRS config.
 * @return 0 on success..
 */
static int __devinit flx_xrs_indirect_init_device(struct flx_xrs_dev_priv *dp,
                                                  struct flx_xrs_cfg *xrs_cfg)
{
    struct resource *res = platform_get_resource(dp->pdev, IORESOURCE_REG, 0);

    dev_dbg(dp->this_dev, "Setup device for indirect register access\n");

    if (!res) {
        dev_err(dp->this_dev, "No I/O registers defined\n");
        return -ENXIO;
    }

    dp->regs.addr = res->start;

    dp->ops = (struct flx_xrs_ops) {
        .read_reg = &flx_xrs_read_reg_indirect,
    };

    return 0;
}

/**
 * Cleanup MDIO register access.
 * @param dp Device private
 */
void flx_xrs_indirect_cleanup_device(struct flx_xrs_dev_priv *dp)
{
    dev_dbg(dp->this_dev, "Cleanup device indirect register access\n");

    dp->ops = (struct flx_xrs_ops) {
        .read_reg = NULL,
    };

    flx_bus_put(dp->regs.flx_bus);
    dp->regs.flx_bus = NULL;

    return;
}
#endif

/**
 * Configure XRS device.
 * @param dp XRS device privates.
 * @param pdev XRS platform_device.
 * @param frs_cfg Temporary storage for building XRS config.
 * @return Pointer for acquired XRS config, or NULL.
 */
static struct flx_xrs_cfg *flx_xrs_device_config(struct flx_xrs_dev_priv *dp,
                                                 struct flx_xrs_cfg *xrs_cfg)
{
    struct flx_xrs_cfg *xrs_cfg_pdata = dev_get_platdata(&dp->pdev->dev);

    if (!xrs_cfg_pdata) {
#ifdef CONFIG_OF
        // These are optional.
        dp->power_ok = of_get_named_gpio(dp->pdev->dev.of_node, "power-ok", 0);
        dp->reset = of_get_named_gpio(dp->pdev->dev.of_node, "reset", 0);
#else
        dev_warn(dp->this_dev, "No platform_data\n");
        return NULL;
#endif
    }
    else {
        dev_printk(KERN_DEBUG, dp->this_dev, "Config via platform_data\n");
        // Config provided via platform_data.
        xrs_cfg = xrs_cfg_pdata;
    }

    return xrs_cfg;
}

/**
 * Setup XRS register access.
 * XRS switch registers and XRS port registers can be accessed either
 * through MDIO or memory mapped I/O.
 */
static int __devinit flx_xrs_reg_access_init_device(
        struct flx_xrs_dev_priv *dp,
        struct flx_xrs_cfg *xrs_cfg)
{
    bool indirect = false;
    int ret = 0;

    dev_dbg(dp->this_dev, "%s()\n", __func__);

#ifdef CONFIG_FLX_BUS
    // Check for indirect register access.
#ifdef CONFIG_OF
    dp->regs.flx_bus = of_flx_bus_get_by_device(dp->pdev->dev.of_node);

    if (dp->regs.flx_bus) {
        indirect = true;
        ret = flx_xrs_indirect_init_device(dp, xrs_cfg);
        if (ret) {
            flx_bus_put(dp->regs.flx_bus);
            dp->regs.flx_bus = NULL;
        }
    }
#else
    if (xrs_cfg->flx_bus_name) {
        // TODO: Currently only possible via device tree
        dev_err(dp->this_dev,
                "Currently indirect register access requires device tree\n");
        return -EINVAL;
    }
#endif
#endif

    if (!indirect)
        return -EIO;

    return ret;
}

/**
 * Cleanup XRS register access through PHY abstraction layer.
 */
static int flx_xrs_reg_access_cleanup_device(struct flx_xrs_dev_priv *dp)
{
    int ret = 0;
    bool indirect = false;

#ifdef CONFIG_FLX_BUS
    if (dp->regs.flx_bus) {
        indirect = true;
        flx_xrs_indirect_cleanup_device(dp);
    }
#endif
    if (!indirect)
        return -EIO;

    return ret;
}

/**
 * Convert XRS DEV_ID0 value to string representation.
 * @param dev_id0 Device ID from DEV_ID0 register.
 * @return String representation of device type or NULL if unknown.
 */
const char *flx_xrs_type_str(int dev_id0)
{
    switch (dev_id0 & XRS_DEV_ID0_MASK) {
    case XRS_DEV_ID0_XRS7003E: return "XRS7003E";
    case XRS_DEV_ID0_XRS7003F: return "XRS7003F";
    case XRS_DEV_ID0_XRS7004E: return "XRS7004E";
    case XRS_DEV_ID0_XRS7004F: return "XRS7004F";
    case XRS_DEV_ID0_XRS3003E: return "XRS3003E";
    case XRS_DEV_ID0_XRS3003F: return "XRS3003F";
    case XRS_DEV_ID0_XRS5003E: return "XRS5003E";
    case XRS_DEV_ID0_XRS5003F: return "XRS5003F";
    case XRS_DEV_ID0_XRS7103E: return "XRS7103E";
    case XRS_DEV_ID0_XRS7103F: return "XRS7103F";
    case XRS_DEV_ID0_XRS7104E: return "XRS7104E";
    case XRS_DEV_ID0_XRS7104F: return "XRS7104F";
    }

    return "XRS(unknown)";
}

/**
 * Print device type to sysfs.
 * @param dev XRS device.
 * @param attr sysfs attribute.
 * @param buf Where to print the revision.
 * @return Number of bytes added to buf.
 */
static ssize_t flx_xrs_show_type(struct device *dev,
                                 struct device_attribute *attr,
                                 char *buf)
{
    struct flx_xrs_dev_priv *dp = flx_xrs_find_dev(dev);
    ssize_t len = 0;
    int ret;

    if (!dp)
        return -ENODEV;

    ret = flx_xrs_read_reg(dp, XRS_REG_DEV_ID0);
    if (ret < 0)
        return -EIO;

    len += sprintf(buf + len, "%s\n", flx_xrs_type_str(ret));

    return len;
}

static DEVICE_ATTR(type, S_IRUGO, &flx_xrs_show_type, NULL);

/**
 * Print device revision to sysfs.
 * @param dev XRS device.
 * @param attr sysfs attribute.
 * @param buf Where to print the revision.
 * @return Number of bytes added to buf.
 */
static ssize_t flx_xrs_show_revision(struct device *dev,
                                     struct device_attribute *attr,
                                     char *buf)
{
    struct flx_xrs_dev_priv *dp = flx_xrs_find_dev(dev);
    ssize_t len = 0;
    int ret;

    if (!dp)
        return -ENODEV;

    ret = flx_xrs_read_reg(dp, XRS_REG_REV_ID);
    if (ret < 0)
        return -EIO;

    len += sprintf(buf + len, "%u.%u\n",
                   (ret >> XRS_REV_ID_MAJOR_OFFSET) & XRS_REV_ID_MAJOR_MASK,
                   (ret >> XRS_REV_ID_MINOR_OFFSET) & XRS_REV_ID_MINOR_MASK);

    return len;
}

static DEVICE_ATTR(revision, S_IRUGO, &flx_xrs_show_revision, NULL);

/**
 * Print internal device revision to sysfs.
 * @param dev XRS device.
 * @param attr sysfs attribute.
 * @param buf Where to print the revision.
 * @return Number of bytes added to buf.
 */
static ssize_t flx_xrs_show_internal_revision(struct device *dev,
                                              struct device_attribute *attr,
                                              char *buf)
{
    struct flx_xrs_dev_priv *dp = flx_xrs_find_dev(dev);
    ssize_t len = 0;
    uint32_t rev = 0;
    int ret;

    if (!dp)
        return -ENODEV;

    ret = flx_xrs_read_reg(dp, XRS_REG_INTERNAL_REV_ID0);
    if (ret < 0)
        return -EIO;
    rev |= ret;

    ret = flx_xrs_read_reg(dp, XRS_REG_INTERNAL_REV_ID1);
    if (ret < 0)
        return -EIO;
    rev |= ret << 16;

    len += sprintf(buf + len, "%u\n", rev);

    return len;
}

static DEVICE_ATTR(internal_revision, S_IRUGO,
                   &flx_xrs_show_internal_revision, NULL);

/**
 * Print device ready status to sysfs.
 * @param dev XRS device.
 * @param attr sysfs attribute.
 * @param buf Where to print the ready status.
 * @return Number of bytes added to buf.
 */
static ssize_t flx_xrs_show_ready(struct device *dev,
                                  struct device_attribute *attr,
                                  char *buf)
{
    struct flx_xrs_dev_priv *dp = flx_xrs_find_dev(dev);
    ssize_t len = 0;

    if (!dp)
        return -ENODEV;

    len += sprintf(buf + len, "%u\n", dp->ready ? 1 : 0);

    return len;
}

/**
 * Store device ready status from sysfs.
 * @param dev XRS device.
 * @param attr sysfs attribute.
 * @param buf Where to read the ready status.
 * @return Number of bytes to write.
 */
static ssize_t flx_xrs_set_ready(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf,
                                 size_t count)
{
    struct flx_xrs_dev_priv *dp = flx_xrs_find_dev(dev);
    ssize_t ret = -EINVAL;

    if (!dp)
        return -ENODEV;

    mutex_lock(&dp->lock);

    if (sysfs_streq(buf, "1")) {
        // Always ready if IRQ has not been defined.
        if (!dp->ready) {
            dp->ready = true;
            if (dp->irq >= 0) {
                enable_irq(dp->irq);
                dev_dbg(dp->this_dev, "Interrupt %i enabled\n", dp->irq);
            }
        }
        ret = count;
    }
    else if (sysfs_streq(buf, "0")) {
        if (dp->ready) {
            // Allow disable only if IRQ has been defined.
            if (dp->irq >= 0) {
                dp->ready = false;
                disable_irq(dp->irq);
                ret = count;
                dev_dbg(dp->this_dev, "Interrupt %i disabled\n", dp->irq);
            }
        }
    }

    mutex_unlock(&dp->lock);

    return ret;
}

static DEVICE_ATTR(ready, S_IRUGO | S_IWUSR,
                   &flx_xrs_show_ready, &flx_xrs_set_ready);

static struct attribute *flx_xrs_attr[] = {
    &dev_attr_type.attr,
    &dev_attr_revision.attr,
    &dev_attr_internal_revision.attr,
    &dev_attr_ready.attr,
    NULL,
};

static const struct attribute_group flx_xrs_attr_group = {
    .name = "xrs",
    .attrs = flx_xrs_attr,
};

static const struct attribute_group *flx_xrs_attr_groups[] = {
    &flx_xrs_attr_group,
    NULL,
};

/**
 * Function to initialise XRS platform devices.
 * @param pdev Platform device
 * @return 0 on success or negative error code.
 */
static int __devinit flx_xrs_device_init(struct platform_device *pdev)
{
    struct flx_xrs_drv_priv *drv = flx_xrs_get_drv_priv();
    struct flx_xrs_dev_priv *dp;
    struct flx_xrs_cfg xrs_cfg_dev_tree = { .baseaddr = 0 };
    struct flx_xrs_cfg *xrs_cfg = NULL;
    unsigned long int dev_num = 0;
    const char *dev_type = NULL;
    struct resource *irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
    int revision = 0;
    int ret = -ENXIO;

    dev_dbg(&pdev->dev, "Init device\n");

    // use pdev->id if provided, if only one, pdev->id == -1
    if (pdev->id >= 0) {
        dev_num = pdev->id;
    } else {
        dev_num = find_first_zero_bit(drv->used_devices, FLX_XRS_MAX_DEVICES);
    }
    if (dev_num >= FLX_XRS_MAX_DEVICES) {
        dev_err(&pdev->dev, "Too many XRS devices\n");
        return -ENODEV;
    }
    if (test_bit(dev_num, drv->used_devices)) {
        dev_err(&pdev->dev, "Device already initialized\n");
        return -ENODEV;
    }

    /// Allocate device private
    dp = kmalloc(sizeof(*dp), GFP_KERNEL);
    if (!dp) {
        dev_err(&pdev->dev, "kmalloc failed\n");
        ret = -ENOMEM;
        goto err_alloc;
    }

    *dp = (struct flx_xrs_dev_priv) {
        .pdev = pdev,
        .this_dev = &pdev->dev,
        .dev_num = dev_num,
        .ready = false,
        .reset = -ENOENT,
        .power_ok = -ENOENT,
        .irq = irq_res ? irq_res->start : -ENOENT,
    };

    /*
     * Disable interrupt immediately to avoid killing CPU before
     * all drivers have set up HW correctly.
     */
    if (dp->irq >= 0) {
        disable_irq(dp->irq);
        irq_set_status_flags(dp->irq, IRQ_NOAUTOEN);
        dev_dbg(&pdev->dev, "Interrupt %i disabled\n", dp->irq);
    }
    else {
        dp->ready = true;
    }

    mutex_init(&dp->lock);
    set_bit(dp->dev_num, drv->used_devices);
    INIT_LIST_HEAD(&dp->list);
    list_add(&dp->list, &drv->devices);

    xrs_cfg = flx_xrs_device_config(dp, &xrs_cfg_dev_tree);
    if (!xrs_cfg) {
        dev_err(dp->this_dev, "Failed to configure device\n");
        goto err_config;
    }

    dev_dbg(&pdev->dev, "Using: power OK %i reset %i IRQ %i\n",
            dp->power_ok, dp->reset, dp->irq);

    // Verify that power is okay.
    if (dp->power_ok >= 0) {
        ret = devm_gpio_request(&pdev->dev, dp->power_ok, "power_ok");
        if (ret) {
            dev_err(&pdev->dev, "Failed to get power OK GPIO %i\n",
                    dp->power_ok);
            goto err_get_power_ok;
        }
        gpio_direction_input(dp->power_ok);
        if (!gpio_get_value(dp->power_ok)) {
            dev_err(&pdev->dev, "Power is not OK\n");
            ret = -EIO;
            goto err_power_not_ok;
        }
        dev_dbg(&pdev->dev, "Power is OK\n");
    }

    // Release from reset.
    if (dp->reset >= 0) {
        ret = devm_gpio_request(&pdev->dev, dp->reset, "reset");
        if (ret) {
            dev_err(&pdev->dev, "Failed to get reset GPIO %i\n", dp->reset);
            goto err_get_reset;
        }
        dev_dbg(&pdev->dev, "Release from reset\n");
        gpio_direction_output(dp->reset, 0);
        msleep(FLX_XRS_RESET_DELAY);
        gpio_set_value(dp->reset, 1);
        msleep(FLX_XRS_RESET_DELAY);
    }

    ret = flx_xrs_reg_access_init_device(dp, xrs_cfg);
    if (ret) {
        goto err_access_init;
    }

    // Verify it is an XRS.
    ret = flx_xrs_read_reg(dp, XRS_REG_DEV_ID1);
    if (ret < 0) {
        dev_err(dp->this_dev, "Failed to read device ID\n");
        ret = -EIO;
        goto err_access;
    }
    if (ret != XRS_DEV_ID1_XRS) {
        dev_warn(dp->this_dev, "Not an XRS device\n");
        ret = -ENODEV;
        goto err_access;
    }

    dev_type = flx_xrs_type_str(flx_xrs_read_reg(dp, XRS_REG_DEV_ID0));

    revision = flx_xrs_read_reg(dp, XRS_REG_REV_ID);
    if (revision < 0) {
        dev_err(dp->this_dev, "Failed to read revision\n");
        ret = -EIO;
        goto err_access;
    }

    ret = sysfs_create_groups(&dp->this_dev->kobj, flx_xrs_attr_groups);
    if (ret) {
        goto err_groups;
    }

    flx_xrs_proc_init_device(dp);

    dev_info(dp->this_dev, "%s revision %u.%u\n",
             dev_type,
             (revision >> XRS_REV_ID_MAJOR_OFFSET) & XRS_REV_ID_MAJOR_MASK,
             (revision >> XRS_REV_ID_MINOR_OFFSET) & XRS_REV_ID_MINOR_MASK);

    return 0;

err_groups:
err_access:
    flx_xrs_reg_access_cleanup_device(dp);

err_access_init:
    if (dp->reset >= 0)
        gpio_direction_input(dp->reset);

err_get_reset:
err_power_not_ok:
err_get_power_ok:
err_config:
    list_del(&dp->list);
    clear_bit(dp->dev_num, drv->used_devices);
    if (dp->irq >= 0)
        enable_irq(dp->irq);
    dp->pdev = NULL;
    kfree(dp);

err_alloc:
    return ret;
}

/**
 * Function to clean device data
 */
static void __devexit flx_xrs_device_cleanup(struct flx_xrs_dev_priv *dp)
{
    struct flx_xrs_drv_priv *drv = flx_xrs_get_drv_priv();

    dev_dbg(dp->this_dev, "%s()\n", __func__);

    flx_xrs_proc_cleanup_device(dp);

    sysfs_remove_groups(&dp->this_dev->kobj, flx_xrs_attr_groups);

    flx_xrs_reg_access_cleanup_device(dp);

    if (!dp->ready && dp->irq >= 0)
        enable_irq(dp->irq);

    list_del(&dp->list);
    clear_bit(dp->dev_num, drv->used_devices);
    dp->pdev = NULL;

    kfree(dp);
}

/*
 * Platform device driver match table.
 */
#ifdef CONFIG_OF
static const struct of_device_id flx_xrs_match[] = {
    { .compatible = "flx,xrs" },
    { },
};
#endif

/**
 * Platform Driver definition for Linux core.
 */
static struct platform_driver flx_xrs_dev_driver = {
    .driver = {
        .name = DRV_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_OF
        .of_match_table = flx_xrs_match,
#endif
    },
    .probe = &flx_xrs_device_init,
};

/**
 * Module init.
 * @return 0 if success.
 */
static int __init flx_xrs_init(void)
{
    struct flx_xrs_drv_priv *drv = flx_xrs_get_drv_priv();
    int ret = 0;

    printk(KERN_DEBUG DRV_NAME ": Init driver\n");

    ret = flx_xrs_guard_init();
    if (ret)
        goto err_guard_init;

    INIT_LIST_HEAD(&drv->devices);

    // Init proc file system
    ret = flx_xrs_proc_init_driver();
    if (ret)
        goto err_proc_init;

    // Register platform driver
    ret = platform_driver_register(&flx_xrs_dev_driver);
    if (ret)
        goto err_reg_driver;

    return 0;

err_reg_driver:
    flx_xrs_proc_cleanup_driver();

err_proc_init:
    flx_xrs_guard_cleanup();

err_guard_init:
    return ret;
}

/**
 * Module exit.
 * Cleanup everything.
 */
static void __exit flx_xrs_cleanup(void)
{
    struct flx_xrs_drv_priv *drv = flx_xrs_get_drv_priv();
    struct flx_xrs_dev_priv *dp = NULL;
    struct flx_xrs_dev_priv *tmp = NULL;

    printk(KERN_DEBUG DRV_NAME ": Cleanup driver\n");

    list_for_each_entry_safe(dp, tmp, &drv->devices, list) {
        flx_xrs_device_cleanup(dp);
    }

    flx_xrs_proc_cleanup_driver();

    platform_driver_unregister(&flx_xrs_dev_driver);

    flx_xrs_guard_cleanup();
}

// Module init and exit function
module_init(flx_xrs_init);
module_exit(flx_xrs_cleanup);

