/** @file
 * @brief Flexibilis general purpose I/O
 */

/*

   Flexibilis general purpose I/O Linux driver

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

#define DRV_NAME                "flx_gpio"
#define DRV_VERSION             "1.11.1"

// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#ifdef CONFIG_FLX_BUS
#include <flx_bus/flx_bus.h>
#endif

#include "flx_gpio_if.h"

// __devinit and friends disappeared in Linux 3.8.
#ifndef __devinit
#define __devinit
#define __devexit
#endif

// Module description information
MODULE_DESCRIPTION("Flexibilis General Purpose I/O driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

#define MAX_DEVICES 16

/**
 * GPIO driver context
 */
struct flx_gpio_drv_priv {
    /// Device privates
    struct flx_gpio_dev_priv *devices[MAX_DEVICES];
    /// Number of devices found
    unsigned int num_devices;
};

/**
 * PIO device context
 */
struct flx_gpio_dev_priv {
    unsigned int dev_num;               ///< Device number
    struct platform_device *pdev;       ///< Associated platform_device
    unsigned int width;                 ///< Number of I/O on device
    struct gpio_chip gpio_chip;         ///< GPIO chip
    uint16_t *config;                   ///< GPIO config bits
    spinlock_t lock_direct;             ///< Memory mapped register access lock
#ifdef CONFIG_FLX_BUS
    struct mutex lock_indirect;         ///< Indirect register access lock
    struct flx_bus *flx_bus;            ///< Indirect register access bus
    uint32_t bus_addr;                  ///< Indirect register bus address
#endif
    void __iomem *ioaddr;               ///< Memory-mapped I/O address
};

/// Get index to shadow config index.
#define FLX_GPIO_SHADOW_INDEX(offset) ((offset) / 8u)

/// Number of config bytes for given GPIO count.
#define FLX_GPIO_CONFIG_SIZE(width) \
    (FLX_GPIO_SHADOW_INDEX(width) * sizeof(uint16_t))

/**
 * Acquire register access lock.
 */
static inline void flx_gpio_lock(struct flx_gpio_dev_priv *dp)
{
#ifdef CONFIG_FLX_BUS
    if (dp->flx_bus) {
        mutex_lock(&dp->lock_indirect);
        return;
    }
#endif
    spin_lock(&dp->lock_direct);
}

/**
 * Release register access lock.
 */
static inline void flx_gpio_unlock(struct flx_gpio_dev_priv *dp)
{
#ifdef CONFIG_FLX_BUS
    if (dp->flx_bus) {
        mutex_unlock(&dp->lock_indirect);
        return;
    }
#endif
    spin_unlock(&dp->lock_direct);
}

/**
 * Read GPIO register value.
 */
static inline int flx_gpio_read16(struct flx_gpio_dev_priv *dp,
                                  uint32_t addr, uint16_t *value)
{
#ifdef CONFIG_FLX_BUS
    if (dp->flx_bus) {
        int ret = flx_bus_read16(dp->flx_bus, dp->bus_addr + addr,
                                 value);
        dev_dbg(&dp->pdev->dev, "Read from 0x%x value 0x%x\n",
                dp->bus_addr + addr, *value);
        return ret;
    }
#endif
    *value = ioread16(dp->ioaddr + addr);
    return 0;
}

/**
 * Write GPIO register value.
 */
static inline int flx_gpio_write16(struct flx_gpio_dev_priv *dp,
                                   uint32_t addr, uint16_t value)
{
#ifdef CONFIG_FLX_BUS
    if (dp->flx_bus) {
        int ret = flx_bus_write16(dp->flx_bus, dp->bus_addr + addr,
                                  value);
        dev_dbg(&dp->pdev->dev, "Write to 0x%x value 0x%x\n",
                dp->bus_addr + addr, value);
        return ret;
    }
#endif
    iowrite16(value, dp->ioaddr + addr);
    return 0;
}

/// Our driver privates
static struct flx_gpio_drv_priv drv_priv;

static int flx_bus_gpio_cleanup(struct platform_device *pdev);

/**
 * Get access to driver privates
 */
static inline struct flx_gpio_drv_priv *get_drv_priv(void)
{
    return &drv_priv;
}

/**
 * GPIO chip interface: set GPIO direction as input
 */
static int flx_bus_gpio_direction_input(struct gpio_chip *chip,
                                        unsigned int offset)
{
    struct flx_gpio_dev_priv *dp =
        container_of(chip, struct flx_gpio_dev_priv, gpio_chip);

    int ret = 0;
    uint16_t *config = &dp->config[FLX_GPIO_SHADOW_INDEX(offset)];
    const unsigned int shift = FLX_GPIO_SHIFT(offset);

    flx_gpio_lock(dp);

    if (*config & (FLX_GPIO_OUT_DIR << shift)) {
        *config &= ~(FLX_GPIO_MASK << shift);
        ret = flx_gpio_write16(dp, FLX_GPIO_CONFIG_REG(offset), *config);
    }

    flx_gpio_unlock(dp);

    if (ret < 0)
        return ret;

    dev_dbg(&dp->pdev->dev, "DIR input %u\n", offset);

    return ret;
}

/**
 * GPIO chip interface: set GPIO direction as output
 */
static int flx_bus_gpio_direction_output(struct gpio_chip *chip,
                                         unsigned int offset, int value)
{
    struct flx_gpio_dev_priv *dp =
        container_of(chip, struct flx_gpio_dev_priv, gpio_chip);

    int ret = 0;
    uint16_t *config = &dp->config[FLX_GPIO_SHADOW_INDEX(offset)];
    const unsigned int shift = FLX_GPIO_SHIFT(offset);
    const uint16_t config_mask = FLX_GPIO_MASK << shift;
    const uint16_t config_bits = FLX_GPIO_OUT_BITS(value) << shift;

    flx_gpio_lock(dp);

    if ((*config & config_mask) != config_bits) {
        *config &= ~config_mask;
        *config |= config_bits;
        ret = flx_gpio_write16(dp, FLX_GPIO_CONFIG_REG(offset), *config);
    }

    flx_gpio_unlock(dp);

    if (ret < 0)
        return ret;

    dev_dbg(&dp->pdev->dev, "DIR output %u\n", offset);

    return ret;
}

/**
 * GPIO chip interface: Get GPIO value
 */
static int flx_bus_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
    struct flx_gpio_dev_priv *dp =
        container_of(chip, struct flx_gpio_dev_priv, gpio_chip);

    int ret;
    uint16_t config_bits;
    uint16_t status;
    const unsigned int shift = FLX_GPIO_SHIFT(offset);

    flx_gpio_lock(dp);

    config_bits = dp->config[FLX_GPIO_SHADOW_INDEX(offset)];
    config_bits = (config_bits >> shift) & FLX_GPIO_MASK;
    if (config_bits & FLX_GPIO_OUT_DIR) {
        ret = !!(config_bits & FLX_GPIO_VALUE);
    }
    else {
        // GPIO is an input, read value from status register
        ret = flx_gpio_read16(dp, FLX_GPIO_INPUT_STATUS_REG(offset), &status);
        if (ret < 0)
            goto err_read;
        ret = !!((status >> shift) & FLX_GPIO_VALUE);
    }

err_read:
    flx_gpio_unlock(dp);

    if (ret < 0)
        return ret;

    dev_dbg(&dp->pdev->dev, "GET %u value %u DIR %s\n",
            offset, ret,
            config_bits & FLX_GPIO_OUT_LOW ? "OUT" : "IN");

    return ret;
}

/**
 * GPIO chip interface: set GPIO value
 */
static void flx_bus_gpio_set(struct gpio_chip *chip,
                                unsigned int offset, int value)
{
    struct flx_gpio_dev_priv *dp =
        container_of(chip, struct flx_gpio_dev_priv, gpio_chip);

    int ret = -EINVAL;
    uint16_t *config = &dp->config[FLX_GPIO_SHADOW_INDEX(offset)];
    const unsigned int shift = FLX_GPIO_SHIFT(offset);
    const uint16_t config_dir_mask = FLX_GPIO_OUT_DIR << shift;
    const uint16_t config_mask = FLX_GPIO_MASK << shift;
    const uint16_t config_bits = FLX_GPIO_OUT_BITS(value) << shift;

    flx_gpio_lock(dp);

    // Write register only if given GPIO is an output and the given value
    // does not match what config already has.
    if (!(*config & config_dir_mask)) {
        // Should never happen.
        flx_gpio_unlock(dp);
        dev_warn(&dp->pdev->dev, "Offset %u configured as input, cannot set\n",
                 offset);
        return;
    }

    if ((*config & config_mask) != config_bits) {
        *config &= ~config_mask;
        *config |= config_bits;
        ret = flx_gpio_write16(dp, FLX_GPIO_CONFIG_REG(offset), *config);
    }

    flx_gpio_unlock(dp);

    if (ret < 0)
        return;

    dev_dbg(&dp->pdev->dev, "SET %u to %i DIR %s\n",
            offset, value,
            *config & config_dir_mask ? "OUT" : "IN");

    return;
}

/**
 * Register Linux GPIO for GPIO device.
 * @param pdev Platform_device for GPIO device
 */
static int __devinit flx_bus_gpio_init(struct platform_device *pdev)
{
    int ret = -ENXIO;
    struct flx_gpio_drv_priv *drv = get_drv_priv();
    struct flx_gpio_dev_priv *dp = NULL;
    struct resource *res = NULL;
    bool indirect = false;
    struct gpio_chip *chip = NULL;
    uint32_t width = 0;
    unsigned int i;

    dev_dbg(&pdev->dev, "Init GPIO device\n");

    if (drv->num_devices >= MAX_DEVICES) {
        return -ENOMEM;
    }

    /// Allocate device private 
    dp = kmalloc(sizeof(*dp), GFP_KERNEL);
    if (!dp) {
        dev_warn(&pdev->dev, "kmalloc failed\n");
        ret = -ENOMEM;
        goto err_alloc_priv;
    }

    *dp = (struct flx_gpio_dev_priv) {
        .dev_num = drv->num_devices,
        .pdev = pdev,
    };
    spin_lock_init(&dp->lock_direct);

#ifdef CONFIG_OF

    ret = of_property_read_u32(pdev->dev.of_node, "width", &width);
    if (ret) {
        dev_err(&pdev->dev, "Missing width\n");
        ret = -EINVAL;
        goto err_width;
    }

#ifdef CONFIG_FLX_BUS
    dp->flx_bus = of_flx_bus_get_by_device(pdev->dev.of_node);
    if (dp->flx_bus) {
        // Indirect register access.
        indirect = true;

        dev_dbg(&pdev->dev, "Indirect register access\n");

        res = platform_get_resource(pdev, IORESOURCE_REG, 0);
        if (!res) {
            dev_err(&pdev->dev, "Register address not defined\n");
            goto err_access;
        }

        dp->bus_addr = res->start;

        mutex_init(&dp->lock_indirect);
    }
#endif

#else
    // Currently width can only be set from device tree.
#endif

    if (width == 0) {
        dev_err(&pdev->dev, "Width cannot be zero\n");
        ret = -EINVAL;
        goto err_width;
    }
    dp->width = width;
    dev_dbg(&pdev->dev, "GPIO width %u\n", dp->width);

    if (!indirect) {
        // Memory mapped register access.

        dev_dbg(&pdev->dev, "Memory mapped register access\n");

        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res) {
            dev_err(&pdev->dev, "I/O memory not defined\n");
            ret = -EINVAL;
            goto err_access;
        }

        dp->ioaddr = ioremap_nocache(res->start, resource_size(res));
        if (!dp->ioaddr) {
            dev_warn(&pdev->dev, "ioremap failed\n");
            ret = -ENOMEM;
            goto err_access;
        }
    }

    // Setup shadow config bits.
    dp->config = kzalloc(FLX_GPIO_CONFIG_SIZE(dp->width), GFP_KERNEL);
    if (!dp->config) {
        dev_err(&pdev->dev, "kmalloc failed\n");
        goto err_config;
    }

    for (i = 0; i < dp->width; i += 8) {
        ret = flx_gpio_read16(dp, FLX_GPIO_CONFIG_REG(i),
                              &dp->config[FLX_GPIO_SHADOW_INDEX(i)]);
        if (ret < 0)
            goto err_config;
    }

    chip = &dp->gpio_chip;

    *chip = (struct gpio_chip) {
        .label = DRV_NAME,
        .dev = &dp->pdev->dev,
        .direction_input = &flx_bus_gpio_direction_input,
        .direction_output = &flx_bus_gpio_direction_output,
        .get = &flx_bus_gpio_get,
        .set = &flx_bus_gpio_set,
        // Allocate GPIO numbers dynamically
        .base = -1,
        .ngpio = dp->width,
        .can_sleep = indirect,
    };

    dev_dbg(&dp->pdev->dev, "Adding GPIO chip\n");

    ret = gpiochip_add(chip);
    if (ret) {
        dev_err(&dp->pdev->dev, "Failed to add GPIO chip\n");
        goto err_gpiochip_add;
    }

    drv->devices[drv->num_devices++] = dp;

    dev_info(&dp->pdev->dev, "Added GPIO %u .. %u\n",
             chip->base, chip->base + chip->ngpio - 1);

    return 0;

err_gpiochip_add:
    drv->devices[dp->dev_num] = NULL;

err_config:
    kfree(dp->config);
    dp->config = NULL;

err_width:
err_access:
#ifdef CONFIG_FLX_BUS
    if (dp->flx_bus) {
        flx_bus_put(dp->flx_bus);
        dp->flx_bus = NULL;
    }
#endif
    if (dp->ioaddr) {
        iounmap(dp->ioaddr);
        dp->ioaddr = NULL;
    }
    kfree(dp);

err_alloc_priv:
    return ret;
}

/**
 * Unregister Linux GPIO on GPIO device.
 * @param pdev Platform device of GPIO device.
 */
static int flx_bus_gpio_cleanup(struct platform_device *pdev)
{
    struct flx_gpio_drv_priv *drv = get_drv_priv();
    struct flx_gpio_dev_priv *dp = NULL;
    unsigned int i;
    int ret = 0;

    dev_dbg(&pdev->dev, "Release\n");

    for (i = 0; i < drv->num_devices; i++) {
        if (drv->devices[i]->pdev == pdev) {
            dp = drv->devices[i];
            break;
        }
    }

    if (!dp)
        return -ENODEV;

    dev_dbg(&pdev->dev, "Removing GPIO chip for GPIO %u .. %u\n",
            dp->gpio_chip.base,
            dp->gpio_chip.base + dp->gpio_chip.ngpio - 1);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0)
    ret = gpiochip_remove(&dp->gpio_chip);
    if (ret) {
        dev_err(&pdev->dev, "Failed to remove GPIO chip\n");
        return ret;
    }
#else
    gpiochip_remove(&dp->gpio_chip);
#endif

    drv->devices[i] = NULL;

#ifdef CONFIG_FLX_BUS
    if (dp->flx_bus) {
        flx_bus_put(dp->flx_bus);
        dp->flx_bus = NULL;
    }
#endif

    if (dp->ioaddr) {
        iounmap(dp->ioaddr);
        dp->ioaddr = NULL;
    }

    kfree(dp->config);
    dp->config = NULL;

    kfree(dp);

    return ret;
}

#ifdef CONFIG_OF
/**
 * Device tree match for PIO devices.
 */
static const struct of_device_id flx_bus_gpio_match[] = {
    { .compatible = "flx,gpio" },
    { },
};
#endif

/**
 * GPIO driver definition.
 */
static struct platform_driver flx_bus_gpio_driver = {
    .driver = {
               .name = "flx-gpio",
               .owner = THIS_MODULE,
#ifdef CONFIG_OF
               .of_match_table = flx_bus_gpio_match,
#endif
               },
    .probe = &flx_bus_gpio_init,
    .remove = &flx_bus_gpio_cleanup,
};

/**
 * Initialize driver.
 */
static int __init flx_gpio_init(void)
{
    int ret = 0;

    printk(KERN_DEBUG DRV_NAME ": Init driver\n");

    ret = platform_driver_register(&flx_bus_gpio_driver);
    if (ret)
        goto err_gpio_driver;

  err_gpio_driver:
    return ret;
}

/**
 * Cleanup driver.
 */
static void __exit flx_gpio_cleanup(void)
{
    printk(KERN_DEBUG DRV_NAME ": module cleanup\n");

    platform_driver_unregister(&flx_bus_gpio_driver);

    printk(KERN_DEBUG DRV_NAME ": module cleanup done\n");
}

// Module init and exit function
module_init(flx_gpio_init);
module_exit(flx_gpio_cleanup);
