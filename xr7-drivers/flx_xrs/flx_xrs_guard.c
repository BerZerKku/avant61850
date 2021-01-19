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

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#endif

#include "flx_xrs_guard.h"

// Driver private data
static struct flx_xrs_guard_drv_priv flx_xrs_guard_drv_priv = {
};

/**
 * Get access to driver privates.
 */
static struct flx_xrs_guard_drv_priv *flx_xrs_guard_get_drv_priv(void)
{
    return &flx_xrs_guard_drv_priv;
}

/**
 * Find XRS device privates.
 * @param dev XRS device.
 * @return XRS device privates or NULL.
 */
static struct flx_xrs_guard_dev_priv *flx_xrs_guard_find_dev(
        struct device *dev)
{
    struct flx_xrs_guard_drv_priv *drv = flx_xrs_guard_get_drv_priv();
    struct flx_xrs_guard_dev_priv *dp;

    list_for_each_entry(dp, &drv->devices, list) {
        if (dp->this_dev == dev)
            return dp;
    }

    return NULL;
}

/**
 * Configure XRS device.
 * @param dp XRS device privates.
 * @param pdev XRS platform_device.
 */
static int flx_xrs_guard_device_config(struct flx_xrs_guard_dev_priv *dp)
{
#ifdef CONFIG_OF
    // These are optional.
    dp->power_ok = of_get_named_gpio(dp->pdev->dev.of_node, "power-ok", 0);
    dp->reset = of_get_named_gpio(dp->pdev->dev.of_node, "reset", 0);
#else
    return -ENODEV;
#endif

    return 0;
}

/**
 * Print device ready status to sysfs.
 * @param dev XRS device.
 * @param attr sysfs attribute.
 * @param buf Where to print the ready status.
 * @return Number of bytes added to buf.
 */
static ssize_t flx_xrs_guard_show_ready(struct device *dev,
                                        struct device_attribute *attr,
                                        char *buf)
{
    struct flx_xrs_guard_dev_priv *dp = flx_xrs_guard_find_dev(dev);
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
static ssize_t flx_xrs_guard_set_ready(struct device *dev,
                                       struct device_attribute *attr,
                                       const char *buf,
                                       size_t count)
{
    struct flx_xrs_guard_dev_priv *dp = flx_xrs_guard_find_dev(dev);
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
                   &flx_xrs_guard_show_ready, &flx_xrs_guard_set_ready);

static struct attribute *flx_xrs_guard_attr[] = {
    &dev_attr_ready.attr,
    NULL,
};

static const struct attribute_group flx_xrs_guard_attr_group = {
    .name = "xrs-guard",
    .attrs = flx_xrs_guard_attr,
};

static const struct attribute_group *flx_xrs_guard_attr_groups[] = {
    &flx_xrs_guard_attr_group,
    NULL,
};

/**
 * Function to initialise XRS platform devices.
 * @param pdev Platform device
 * @return 0 on success or negative error code.
 */
static int __devinit flx_xrs_guard_device_init(struct platform_device *pdev)
{
    struct flx_xrs_guard_drv_priv *drv = flx_xrs_guard_get_drv_priv();
    struct flx_xrs_guard_dev_priv *dp;
    unsigned long int dev_num = 0;
    struct resource *irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
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

    *dp = (struct flx_xrs_guard_dev_priv) {
        .pdev = pdev,
        .this_dev = &pdev->dev,
        .dev_num = dev_num,
        .ready = false,
        .reset = -ENOENT,
        .power_ok = -ENOENT,
        .irq = irq_res ? irq_res->start : -ENOENT,
    };

    // For some strange reason resources are not set for this device.
    // Parse interrupt directly from device tree.
#ifdef CONFIG_OF
    if (!irq_res) {
        struct resource tmp_irq_res = { .start = 0 };

        ret = of_irq_to_resource(pdev->dev.of_node, 0, &tmp_irq_res);
        if (ret > 0)
            dp->irq = ret;
    }
#endif

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

    ret = flx_xrs_guard_device_config(dp);
    if (ret) {
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

    ret = sysfs_create_groups(&dp->this_dev->kobj, flx_xrs_guard_attr_groups);
    if (ret) {
        goto err_groups;
    }

    return 0;

err_groups:
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
static void __devexit flx_xrs_guard_device_cleanup(
        struct flx_xrs_guard_dev_priv *dp)
{
    struct flx_xrs_guard_drv_priv *drv = flx_xrs_guard_get_drv_priv();

    dev_dbg(dp->this_dev, "%s()\n", __func__);

    sysfs_remove_groups(&dp->this_dev->kobj, flx_xrs_guard_attr_groups);

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
static const struct of_device_id flx_xrs_guard_match[] = {
    { .compatible = "flx,xrs-guard" },
    { },
};
#endif

/**
 * Platform Driver definition for Linux core.
 */
static struct platform_driver flx_xrs_guard_dev_driver = {
    .driver = {
        .name = "flx_xrs_guard",
        .owner = THIS_MODULE,
#ifdef CONFIG_OF
        .of_match_table = flx_xrs_guard_match,
#endif
    },
    .probe = &flx_xrs_guard_device_init,
};

/**
 * Module init.
 * @return 0 if success.
 */
int __init flx_xrs_guard_init(void)
{
    struct flx_xrs_guard_drv_priv *drv = flx_xrs_guard_get_drv_priv();
    int ret = 0;

    INIT_LIST_HEAD(&drv->devices);

    // Register platform driver
    ret = platform_driver_register(&flx_xrs_guard_dev_driver);
    if (ret)
        goto err_reg_driver;

    return 0;

err_reg_driver:
    return ret;
}

/**
 * Module exit.
 * Cleanup everything.
 */
void flx_xrs_guard_cleanup(void)
{
    struct flx_xrs_guard_drv_priv *drv = flx_xrs_guard_get_drv_priv();
    struct flx_xrs_guard_dev_priv *dp = NULL;
    struct flx_xrs_guard_dev_priv *tmp = NULL;

    list_for_each_entry_safe(dp, tmp, &drv->devices, list) {
        flx_xrs_guard_device_cleanup(dp);
    }

    platform_driver_unregister(&flx_xrs_guard_dev_driver);
}

