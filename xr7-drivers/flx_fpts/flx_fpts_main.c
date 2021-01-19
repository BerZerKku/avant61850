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
#include <linux/version.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif

#include "flx_fpts_types.h"
#include "flx_fpts_hw_type.h"
#include "flx_fpts_char.h"
#include "flx_fpts_proc.h"
#include "flx_fpts_if.h"

#ifdef CONFIG_FLX_BUS
# include <flx_bus/flx_bus.h>
#endif

// Module description information
MODULE_DESCRIPTION("Flexibilis PPx Time Stamper (FPTS/TS) driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

// Driver private data
static struct flx_fpts_drv_priv flx_fpts_drv_priv = {
    .class = {
        .name = DRV_NAME,
        .owner = THIS_MODULE,
    },
};

/**
 * Get access to driver privates.
 */
static struct flx_fpts_drv_priv *flx_fpts_get_drv_priv(void)
{
    return &flx_fpts_drv_priv;
}

/**
 * Get pointer to given register for use with memory mapped I/O.
 * @param dp FPTS device privates.
 * @param reg Register address.
 */
static inline void __iomem *flx_fpts_reg_addr(struct flx_fpts_reg_access *regs,
                                             int reg)
{
    return regs->ioaddr + reg;
}

/**
 * Read register via MMIO.
 * @param dp Device privates.
 * @param reg Register address.
 * @return Read value.
 */
static int flx_fpts_read_reg_mmio(struct flx_fpts_dev_priv *dp,
                                 int reg)
{
    return ioread16(flx_fpts_reg_addr(&dp->regs, reg));
}

/**
 * Write register via MMIO.
 * @param dp Device privates.
 * @param reg Register address.
 * @return Read value.
 */
static int flx_fpts_write_reg_mmio(struct flx_fpts_dev_priv *dp,
                                   int reg, uint16_t value)
{
    iowrite16(value, flx_fpts_reg_addr(&dp->regs, reg));
    return 0;
}

/**
 * Init MMIO register access.
 * @param dp Device privates
 * @param fpts_cfg FPTS config
 * @return 0 on success.
 */
static int __devinit flx_fpts_mmio_init_device(struct flx_fpts_dev_priv *dp,
                                              struct flx_fpts_cfg *fpts_cfg)
{
    struct resource *res = platform_get_resource(dp->pdev, IORESOURCE_MEM, 0);
    int ret = 0;

    dev_printk(KERN_DEBUG, &dp->pdev->dev,
               "Setup device %u IRQ %u for memory mapped access\n",
               dp->dev_num, dp->irq);

    if (!res) {
        dev_err(&dp->pdev->dev, "No I/O memory defined\n");
        return -ENXIO;
    }

    dp->regs.ioaddr = ioremap_nocache(res->start, resource_size(res));
    if (!dp->regs.ioaddr) {
        dev_printk(KERN_WARNING, &dp->pdev->dev,
                   ": ioremap failed for device address 0x%llx/0x%llx\n",
                   (unsigned long long int) res->start,
                   (unsigned long long int) resource_size(res));
        ret = -ENXIO;
        goto out;
    }

    dev_printk(KERN_DEBUG, &dp->pdev->dev,
               "Device uses memory mapped access: 0x%llx/0x%llx -> %p\n",
               (unsigned long long int) res->start,
               (unsigned long long int) resource_size(res),
               dp->regs.ioaddr);

    dp->regs.ops = (struct flx_fpts_ops) {
        .read_reg = &flx_fpts_read_reg_mmio,
        .write_reg = &flx_fpts_write_reg_mmio,
    };

out:
    return ret;
}

/**
 * Cleanup MMIO register access.
 * @param dp Device privates.
 */
static void flx_fpts_mmio_cleanup_device(struct flx_fpts_dev_priv *dp)
{
    dev_dbg(&dp->pdev->dev, "Cleanup device memory mapped access\n");

    dp->regs.ops = (struct flx_fpts_ops) {
        .read_reg = NULL,
    };

    iounmap(dp->regs.ioaddr);
    dp->regs.ioaddr = NULL;

    return;
}

#ifdef CONFIG_FLX_BUS
/**
 * Determine bus address for given FPTS register.
 * @param base Bus base address.
 * @param reg FPTS register address.
 * @return Bus address for FPTS register.
 */
static inline uint32_t flx_fpts_bus_addr(struct flx_fpts_reg_access *regs,
                                         int reg)
{
    return regs->addr + reg;
}

/**
 * Read register via indirect register access.
 * @param dp Device privates.
 * @param regnum Register number.
 * @return Read value or negative error code.
 */
static int flx_fpts_read_reg_indirect(struct flx_fpts_dev_priv *dp,
                                      int reg)
{
    uint16_t value = 0xffff;
    int ret;

    ret = flx_bus_read16(dp->regs.flx_bus,
                         flx_fpts_bus_addr(&dp->regs, reg),
                         &value);
    if (ret < 0)
        return ret;

    return value;
}

/**
 * Write register via indirect register access.
 * @param dp Device privates.
 * @param regnum Register number.
 * @param value Value to write.
 * @return Zero on success or negative error code.
 */
static int flx_fpts_write_reg_indirect(struct flx_fpts_dev_priv *dp,
                                       int reg, uint16_t value)
{
    return flx_bus_write16(dp->regs.flx_bus,
                           flx_fpts_bus_addr(&dp->regs, reg),
                           value);
}

/**
 * Initialize indirect register access.
 * @param dp Device privates.
 * @param io_res IO resource handle from platform config.
 * @param frs_cfg FRS config.
 * @return 0 on success..
 */
static int __devinit flx_fpts_indirect_init_device(
        struct flx_fpts_dev_priv *dp,
        struct flx_fpts_cfg *fpts_cfg)
{
    struct resource *res = platform_get_resource(dp->pdev, IORESOURCE_REG, 0);

    if (dp->irq) {
        dev_printk(KERN_DEBUG, &dp->pdev->dev,
                   "Setup device %u IRQ %u for indirect register access\n",
                   dp->dev_num, dp->irq);
    }
    else {
        dev_printk(KERN_DEBUG, &dp->pdev->dev,
                   "Setup device %u for indirect register access\n",
                   dp->dev_num);
    }

    if (!res) {
        dev_err(&dp->pdev->dev, "No I/O registers defined\n");
        return -ENXIO;
    }

    dp->regs.addr = res->start;

    dp->regs.ops = (struct flx_fpts_ops) {
        .read_reg = &flx_fpts_read_reg_indirect,
        .write_reg = &flx_fpts_write_reg_indirect,
    };

    return 0;
}

/**
 * Cleanup indirect register access.
 * @param dp Device private
 */
void flx_fpts_indirect_cleanup_device(struct flx_fpts_dev_priv *dp)
{
    dev_dbg(&dp->pdev->dev, "Cleanup device indirect register access\n");

    dp->regs.ops = (struct flx_fpts_ops) {
        .read_reg = NULL,
    };

    flx_bus_put(dp->regs.flx_bus);
    dp->regs.flx_bus = NULL;

    return;
}
#endif

/**
 * Configure FPTS device.
 * @param dp FPTS device privates.
 * @param pdev FPTS platform_device.
 * @param frs_cfg Temporary storage for building FPTS config.
 * @return Pointer for acquired FPTS config, or NULL.
 */
static struct flx_fpts_cfg *flx_fpts_device_config(
        struct flx_fpts_dev_priv *dp,
        struct platform_device *pdev,
        struct flx_fpts_cfg *fpts_cfg)
{
    struct flx_fpts_cfg *fpts_cfg_pdata = dev_get_platdata(&pdev->dev);

    if (!fpts_cfg_pdata) {
#ifdef CONFIG_OF
        struct resource *irq_res =
            platform_get_resource(pdev, IORESOURCE_IRQ, 0);

        // IRQ is not required.
        if (irq_res) {
            dp->irq = irq_res->start;
        }
        else {
            dp->irq = 0;
        }

        // Register access
#ifdef CONFIG_FLX_BUS
        dp->regs.flx_bus = of_flx_bus_get_by_device(pdev->dev.of_node);
#endif
#else
        dev_warn(&pdev->dev, "No platform_data\n");
        return NULL;
#endif
    }
    else {
        dev_printk(KERN_DEBUG, &pdev->dev, "Config via platform_data\n");
        // Config provided via platform_data.
        fpts_cfg = fpts_cfg_pdata;
        dp->irq = fpts_cfg->irq;
    }

    return fpts_cfg;
}

/**
 * Setup FPTS register access.
 * FPTS switch registers and FPTS port registers can be accessed either
 * through MDIO or memory mapped I/O.
 */
static int __devinit flx_fpts_reg_access_init_device(
        struct flx_fpts_dev_priv *dp,
        struct platform_device *pdev,
        struct flx_fpts_cfg *fpts_cfg)
{
    bool indirect = false;
    int ret = 0;

    // Setup FPTS register access.

#ifdef CONFIG_FLX_BUS
    // Check for indirect register access.
#ifdef CONFIG_OF
    if (dp->regs.flx_bus) {
        indirect = true;
        ret = flx_fpts_indirect_init_device(dp, fpts_cfg);
    }
#else
    if (fpts_cfg->flx_bus_name) {
        // TODO: Currently only possible via device tree
        dev_err(&pdev->dev,
                "Currently indirect register access requires device tree\n");
        return -EINVAL;
    }
#endif
#endif

    if (!indirect) {
        ret = flx_fpts_mmio_init_device(dp, fpts_cfg);
    }

    return ret;
}

/**
 * Cleanup FPTS register access.
 */
static void flx_fpts_reg_access_cleanup_device(struct flx_fpts_dev_priv *dp)
{
    bool indirect = false;

#ifdef CONFIG_FLX_BUS
    if (dp->regs.flx_bus) {
        indirect = true;
        flx_fpts_indirect_cleanup_device(dp);
    }
#endif

    if (!indirect) {
        flx_fpts_mmio_cleanup_device(dp);
    }

    return;
}

/**
 * Function to initialise FPTS platform devices.
 * @param pdev Platform device
 * @return 0 on success or negative error code.
 */
static int __devinit flx_fpts_device_init(struct platform_device *pdev)
{
    struct flx_fpts_drv_priv *drv = &flx_fpts_drv_priv;
    struct flx_fpts_dev_priv *dp;
    struct flx_fpts_cfg fpts_cfg_dev_tree = { .baseaddr = 0 };
    struct flx_fpts_cfg *fpts_cfg = NULL;
    unsigned long int dev_num = 0;
    int ret = -ENXIO;

    // use pdev->id if provided, if only one, pdev->id == -1
    if (pdev->id >= 0) {
        dev_num = pdev->id;
    } else {
        dev_num = find_first_zero_bit(drv->used_devices, FLX_FPTS_MAX_DEVICES);
    }
    if (dev_num >= FLX_FPTS_MAX_DEVICES) {
        dev_err(&pdev->dev, "Too many FPTS devices\n");
        return -ENODEV;
    }
    if (test_bit(dev_num, drv->used_devices)) {
        dev_err(&pdev->dev, "Device already initialized\n");
        return -ENODEV;
    }

    dev_dbg(&pdev->dev, "Init device %lu\n", dev_num);

    /// Allocate device private
    dp = kmalloc(sizeof(*dp), GFP_KERNEL);
    if (!dp) {
        dev_err(&pdev->dev, "kmalloc failed\n");
        ret = -ENOMEM;
        goto err_alloc;
    }

    *dp = (struct flx_fpts_dev_priv) {
        .drv = drv,
        .pdev = pdev,
        .dev_num = dev_num,
        .mode = FLX_FPTS_MODE_INTERRUPT,
        .poll_interval = HZ/2,
    };

    spin_lock_init(&dp->buf_lock);
    mutex_init(&dp->read_lock);
    init_waitqueue_head(&dp->read_waitq);

    set_bit(dp->dev_num, drv->used_devices);
    INIT_LIST_HEAD(&dp->list);
    list_add(&dp->list, &drv->devices);

    fpts_cfg = flx_fpts_device_config(dp, pdev, &fpts_cfg_dev_tree);
    if (!fpts_cfg) {
        dev_err(&dp->pdev->dev, "Failed to configure device\n");
        goto err_config;
    }

    // Use polling mode if interrupt is not available.
    if (!dp->irq)
        dp->mode = FLX_FPTS_MODE_POLL;

    ret = flx_fpts_reg_access_init_device(dp, pdev, fpts_cfg);
    if (ret) {
        goto err_init;
    }

    flx_fpts_proc_init_device(dp);

    dp->class_dev = device_create(&drv->class, &dp->pdev->dev,
                                  MKDEV(MAJOR(drv->first_devno), dp->dev_num),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
                                  NULL,
#endif
                                  "%s%u",
                                  DRV_NAME, dp->dev_num);
    if (!dp->class_dev) {
        dev_err(&dp->pdev->dev, "Failed to add device to class\n");
        goto err_dev_create;
    }

    return 0;

err_dev_create:
    flx_fpts_proc_cleanup_device(dp);
    flx_fpts_reg_access_cleanup_device(dp);

err_init:
#ifdef CONFIG_FLX_BUS
    if (dp->regs.flx_bus) {
        flx_bus_put(dp->regs.flx_bus);
        dp->regs.flx_bus = NULL;
    }
#endif

err_config:
    list_del(&dp->list);
    clear_bit(dp->dev_num, drv->used_devices);
    dp->pdev = NULL;
    kfree(dp);

err_alloc:
    return ret;
}

/**
 * Function to clean device data
 */
static void __devexit flx_fpts_device_cleanup(struct flx_fpts_dev_priv *dp)
{
    struct flx_fpts_drv_priv *drv = flx_fpts_get_drv_priv();

    device_destroy(&drv->class, MKDEV(MAJOR(drv->first_devno), dp->dev_num));
    dp->class_dev = NULL;

    flx_fpts_proc_cleanup_device(dp);

    flx_fpts_reg_access_cleanup_device(dp);

    list_del(&dp->list);
    clear_bit(dp->dev_num, drv->used_devices);
    dp->pdev = NULL;

    kfree(dp);
}

/*
 * Platform device driver match table.
 */
#ifdef CONFIG_OF
static const struct of_device_id flx_fpts_match[] = {
    { .compatible = "flx,fpts" },
    { .compatible = "flx,ts" },
    { },
};
#endif

/**
 * Platform Driver definition for Linux core.
 */
static struct platform_driver flx_fpts_dev_driver = {
    .driver = {
        .name = DRV_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_OF
        .of_match_table = flx_fpts_match,
#endif
    },
    .probe = &flx_fpts_device_init,
};

/**
 * Module init.
 * @return 0 if success.
 */
static int __init flx_fpts_init(void)
{
    struct flx_fpts_drv_priv *drv = flx_fpts_get_drv_priv();
    int ret = 0;

    printk(KERN_DEBUG DRV_NAME ": Init driver\n");

    INIT_LIST_HEAD(&drv->devices);

    drv->wq = create_singlethread_workqueue(DRV_NAME);
    if (!drv->wq)
        return -ENOMEM;

    // Init proc file system
    ret = flx_fpts_proc_init_driver();
    if (ret)
        goto err_proc_init;

    ret = flx_fpts_register_char_device(drv);
    if (ret)
        goto err_reg_char;

    // Register platform driver
    ret = platform_driver_register(&flx_fpts_dev_driver);
    if (ret)
        goto err_reg_driver;

    return 0;

err_reg_driver:
    flx_fpts_unregister_char_device(drv);

err_reg_char:
    flx_fpts_proc_cleanup_driver();

err_proc_init:
    destroy_workqueue(drv->wq);
    drv->wq = NULL;

    return ret;
}

/**
 * Module exit.
 * Cleanup everything.
 */
static void __exit flx_fpts_cleanup(void)
{
    struct flx_fpts_drv_priv *drv = flx_fpts_get_drv_priv();
    struct flx_fpts_dev_priv *dp = NULL;
    struct flx_fpts_dev_priv *tmp = NULL;

    printk(KERN_DEBUG DRV_NAME ": Cleanup driver\n");

    list_for_each_entry_safe(dp, tmp, &drv->devices, list) {
        flx_fpts_device_cleanup(dp);
    }

    flx_fpts_proc_cleanup_driver();

    flx_fpts_unregister_char_device(drv);

    platform_driver_unregister(&flx_fpts_dev_driver);

    destroy_workqueue(drv->wq);
    drv->wq = NULL;

    return;
}

// Module init and exit function
module_init(flx_fpts_init);
module_exit(flx_fpts_cleanup);

