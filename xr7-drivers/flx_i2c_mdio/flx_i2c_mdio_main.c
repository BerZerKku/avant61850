/** @file
 * @brief Virtual MDIO bus for I2C PHY devices
 */

/*

   I2C to virtual MDIO bus Linux driver

   Copyright (C) 2013 Flexibilis Oy

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

#define DRV_NAME                "flx_i2c_mdio"
#define DRV_VERSION             "1.11.1"

// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/of_mdio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/version.h>
#include <linux/atomic.h>

// Module description information
MODULE_DESCRIPTION("I2C slave to MDIO bus driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

/// MDIO bus base name
#define FLX_I2C_MDIO_BUS_NAME "flx-i2c-mdio"

/// Maximum number of virtual MDIO buses on I2C slaves.
#define MAX_DEVICES 32

/// I2C change detection interval in jiffies, zero disables
#define MDIO_BUS_CHECK_INTERVAL (1*HZ)

// __devinit and friends disappeared in Linux 3.8.
#ifndef __devinit
#define __devinit
#define __devexit
#endif

// of_property_read_bool appeared in Linux 3.4
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
#ifdef CONFIG_OF
static inline bool of_property_read_bool(const struct device_node *np,
                                         const char *propname)
{
    struct property *prop = of_find_property(np, propname, NULL);

    return prop ? true : false;
}
#endif
#endif

// i2c_smbus_write_word_swapped and i2c_smbus_read_word_swapped
// appeared in Linux 3.2
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
static inline s32 i2c_smbus_write_word_swapped(const struct i2c_client *client,
                                               u8 command, u16 value)
{
    return i2c_smbus_write_word_data(client, command, swab16(value));
}

static inline s32 i2c_smbus_read_word_swapped(const struct i2c_client *client,
                                              u8 command)
{
    s32 value = i2c_smbus_read_word_data(client, command);

    return (value < 0) ? value : swab16(value);
}
#endif

static int __devinit flx_i2c_mdio_device_init(struct i2c_client *client,
                                              const struct i2c_device_id *id);
static int __devexit flx_i2c_mdio_device_cleanup(struct i2c_client *client);

/**
 * Driver context
 */
struct flx_i2c_mdio_drv_priv {
    struct list_head devices;                   ///< Our devices
    DECLARE_BITMAP(used_devices, MAX_DEVICES);  ///< Used device numbers
#if MDIO_BUS_CHECK_INTERVAL > 0
    struct workqueue_struct *wq;                ///< MDIO bus rescan
#endif
};

/**
 * Device privates
 */
struct flx_i2c_mdio_dev_priv {
    unsigned int dev_num;                       ///< Device number
    struct i2c_client *i2c_client;              ///< I2C client
    struct mii_bus *mdio_bus;                   ///< Virtual MDIO bus
    struct list_head list;                      ///< Linked list
#if MDIO_BUS_CHECK_INTERVAL > 0
    bool detect_changes;                        ///< Allow SFP changes
    atomic_t check;
    struct work_struct discard_bus;             ///< Virtual MDIO bus discard
    struct delayed_work check_bus;              ///< Virtual MDIO bus rescan
#endif
};

/// Driver privates
static struct flx_i2c_mdio_drv_priv drv_priv;

/**
 * Get access to driver privates
 */
static inline struct flx_i2c_mdio_drv_priv *get_drv_priv(void)
{
    return &drv_priv;
}

#if MDIO_BUS_CHECK_INTERVAL > 0

static inline void flx_i2c_mdio_set_dead(struct flx_i2c_mdio_dev_priv *dp,
                                         bool value)
{
    atomic_set(&dp->check, value);
}

static inline bool flx_i2c_mdio_is_dead(struct flx_i2c_mdio_dev_priv *dp)
{
    return atomic_read(&dp->check);
}

#else

static inline void flx_i2c_mdio_set_dead(struct flx_i2c_mdio_dev_priv *dp,
                                         bool value)
{ 
    return;
}

static inline bool flx_i2c_mdio_is_dead(struct flx_i2c_mdio_dev_priv *dp)
{
    return false;
}

#endif

/**
 * MDIO bus interface: Reset virtual MDIO bus.
 * @param mdio_bus Virtual MDIO bus intance
 */
static int flx_i2c_mdio_reset(struct mii_bus *mdio_bus)
{
    struct flx_i2c_mdio_dev_priv *dp = mdio_bus->priv;

    if (!dp) {
        printk(KERN_ERR DRV_NAME ": Unknown MDIO bus %s\n", mdio_bus->id);
        return -ENXIO;
    }

    dev_dbg(&dp->i2c_client->dev, "Reset MDIO bus (no-op)\n");

    return 0;
}

/**
 * MDIO bus interface: read PHY register value.
 * @param mdio_bus Virtual MDIO bus instance
 * @param phy_addr PHY address (irrelevant)
 * @param regnum PHY register number
 * @return PHY register value
 */
static int flx_i2c_mdio_read(struct mii_bus *mdio_bus,
        int phy_addr, int regnum)
{
    struct flx_i2c_mdio_dev_priv *dp = mdio_bus->priv;
    int ret = 0;

    if (!dp) {
        printk(KERN_DEBUG DRV_NAME ": Unknown MDIO bus %s\n",
               mdio_bus->id);
        return -ENXIO;
    }

    if (!dp->mdio_bus) {
        dev_warn(&dp->i2c_client->dev, "Read: device has no MDIO bus\n");
        return -ENXIO;
    }

    if (flx_i2c_mdio_is_dead(dp))
        return -ENXIO;

    ret = i2c_smbus_read_word_swapped(dp->i2c_client, regnum);

    if (ret < 0) {
        dev_dbg(&dp->i2c_client->dev,
                "Failed to read PHY 0x%x reg 0x%x got %i\n",
                phy_addr, regnum, ret);
    }
    else {
        dev_dbg(&dp->i2c_client->dev,
                "Read PHY 0x%x reg 0x%x value 0x%x\n",
                phy_addr, regnum, ret);
    }

    /*
     * Rely on PHY framework handling errors.
     * Eventually PHY should end up in PHY_HALTED and unbound
     * from its driver, which causes a notification to us.
     */

    return ret;
}

/**
 * MDIO bus interface: write PHY register value.
 * @param mdio_bus Virtual MDIO bus instance
 * @param phy_addr PHY address (irrelevant)
 * @param regnum PHY register number
 * @param value Value to write to PHY register
 */
static int flx_i2c_mdio_write(struct mii_bus *mdio_bus,
        int phy_addr, int regnum, uint16_t value)
{
    struct flx_i2c_mdio_dev_priv *dp = mdio_bus->priv;
    int ret = 0;

    if (!dp) {
        printk(KERN_DEBUG DRV_NAME ": Unknown MDIO bus %s\n",
               mdio_bus->id);
        return -ENXIO;
    }

    if (!dp->mdio_bus) {
        dev_warn(&dp->i2c_client->dev, "Write: device has no MDIO bus\n");
        return -ENXIO;
    }

    if (flx_i2c_mdio_is_dead(dp))
        return -ENXIO;

    dev_dbg(&dp->i2c_client->dev,
            "Write PHY 0x%x reg 0x%x value 0x%x\n",
            phy_addr, regnum, value);

    ret = i2c_smbus_write_word_swapped(dp->i2c_client, regnum, value);
    if (ret) {
        dev_warn(&dp->i2c_client->dev,
                 "Failed to write PHY 0x%x reg 0x%x value 0x%x\n",
                 phy_addr, regnum, value);
    }

    /*
     * Rely on PHY framework handling errors.
     * Eventually PHY should end up in PHY_HALTED and unbound
     * from its driver, which causes a notification to us.
     */

    return ret;
}

/**
 * Create MDIO bus for the I2C client.
 * @param dp Device privates.
 */
static int flx_i2c_mdio_get_mdiobus(struct flx_i2c_mdio_dev_priv *dp)
{
    int ret = 0;
    struct i2c_client *client = dp->i2c_client;
    struct mii_bus *mdio_bus = NULL;

    dev_dbg(&client->dev,
            "Create virtual MDIO bus for I2C slave 0x%hx\n",
            client->addr);

    mdio_bus = mdiobus_alloc();
    if (!mdio_bus) {
        dev_warn(&client->dev, "kmalloc\n");
        ret = -ENOMEM;
        goto err_mdiobus_alloc;
    }

    mdio_bus->name = FLX_I2C_MDIO_BUS_NAME;
    mdio_bus->reset = &flx_i2c_mdio_reset;
    mdio_bus->read = &flx_i2c_mdio_read;
    mdio_bus->write = &flx_i2c_mdio_write;
    mdio_bus->priv = dp;

    snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s-%x",
             mdio_bus->name, dp->dev_num);

    dp->mdio_bus = mdio_bus;
    flx_i2c_mdio_set_dead(dp, false);

    dev_printk(KERN_DEBUG, &client->dev,
               "Registering virtual MDIO bus %s for I2C slave 0x%hx\n",
               mdio_bus->id, client->addr);

#ifdef CONFIG_OF
    ret = of_mdiobus_register(dp->mdio_bus, client->dev.of_node);
#else
    ret = mdiobus_register(dp->mdio_bus);
#endif
    if (ret) {
        dev_err(&client->dev,
                "Failed to register virtual MDIO bus %s\n",
                mdio_bus->id);
        goto err_mdiobus_register;
    }

    return 0;

err_mdiobus_register:
    dp->mdio_bus = NULL;
    mdiobus_free(mdio_bus);

err_mdiobus_alloc:
    return ret;
}

/**
 * Remove MDIO bus from I2C client.
 * This causes MDIO bus notification chain to be called, so NIC drivers
 * can act accordingly.
 * @param dp Device privates.
 */
static void flx_i2c_mdio_put_mdiobus(struct flx_i2c_mdio_dev_priv *dp)
{
    struct i2c_client *client = dp->i2c_client;
    struct mii_bus *mdio_bus = dp->mdio_bus;

    if (mdio_bus) {
        mdio_bus->priv = NULL;
        dp->mdio_bus = NULL;

        mdiobus_unregister(mdio_bus);
        mdiobus_free(mdio_bus);

        flx_i2c_mdio_set_dead(dp, false);

        dev_printk(KERN_DEBUG, &client->dev,
                   "Removing virtual MDIO bus %s for I2C slave 0x%hx\n",
                   mdio_bus->id,
                   client->addr);
    }

    return;
}

#if MDIO_BUS_CHECK_INTERVAL > 0

/**
 * Work function to drop MDIO bus when PHY is no longer used.
 */
static void flx_i2c_mdio_discard(struct work_struct *work)
{
    struct flx_i2c_mdio_dev_priv *dp =
        container_of(work, struct flx_i2c_mdio_dev_priv, discard_bus);

    dev_dbg(&dp->i2c_client->dev, "%s()\n", __func__);

    if (!dp->mdio_bus)
        return;

    if (!flx_i2c_mdio_is_dead(dp))
        return;

    flx_i2c_mdio_put_mdiobus(dp);

    return;
}

/**
 * Notifier callback function to detect when our PHY is unbound from driver.
 * @param nb Registered notifier block.
 * @param action Bus action.
 * @param data Device on the bus (it is a PHY).
 * @return Notification result code.
 */
static int flx_i2c_mdio_event(struct notifier_block *nb,
                              unsigned long int action, void *data)
{
    struct device *dev = data;
    struct phy_device *phydev = to_phy_device(dev);
    struct mii_bus *mdio_bus = phydev->bus;
    struct flx_i2c_mdio_drv_priv *drv = get_drv_priv();
    struct flx_i2c_mdio_dev_priv *dp = NULL;
    struct flx_i2c_mdio_dev_priv *pos = NULL;

    pr_debug(DRV_NAME ": Bus notification %lu for device %p %s\n",
             action, dev, dev_name(dev));

    // We are only interesed on PHY device unbound events.
    if (action != BUS_NOTIFY_UNBOUND_DRIVER)
        return NOTIFY_DONE;

    // Locate the MDIO bus.
    list_for_each_entry(pos, &drv->devices, list) {
        if (pos->mdio_bus == mdio_bus) {
            dp = pos;
            break;
        }
    }

    if (!dp) {
        pr_debug(DRV_NAME ": Failed to find virtual MDIO bus for device %s\n",
                 dev_name(dev));
        return NOTIFY_DONE;
    }

    if (!dp->detect_changes)
        return NOTIFY_OK;

    dev_dbg(&dp->i2c_client->dev, "Queue %s for removal\n", dev_name(dev));
    flx_i2c_mdio_set_dead(dp, true);
    queue_work(drv->wq, &dp->discard_bus);

    return NOTIFY_OK;
}

/**
 * Notifier block for PHY bus events.
 */
static struct notifier_block flx_i2c_mdio_notifier = {
    .notifier_call = &flx_i2c_mdio_event,
};

/**
 * Work function to check PHY presence.
 * If there is no PHY, rescan the whole virtual MDIO bus
 * in order to detect possible changes.
 * @param work Work within I2C client device privates.
 */
static void flx_i2c_mdio_check_bus(struct work_struct *work)
{
    struct flx_i2c_mdio_dev_priv *dp =
        container_of(work, struct flx_i2c_mdio_dev_priv, check_bus.work);
    struct flx_i2c_mdio_drv_priv *drv = get_drv_priv();
    unsigned int num_phys = 0;
    unsigned int i;
    struct i2c_client *client = dp->i2c_client;
    struct mii_bus *mdio_bus = dp->mdio_bus;
    int ret = 0;

    // Check if PHY is already unbound from its driver.
    if (flx_i2c_mdio_is_dead(dp)) {
        dev_dbg(&client->dev, "Virtual MDIO bus marked for removal\n");
        flx_i2c_mdio_put_mdiobus(dp);
        goto out;
    }

    // Check if the virtual MDIO bus actually contains any PHY.
    if (mdio_bus) {
        mutex_lock(&mdio_bus->mdio_lock);

        for (i = 0; i < PHY_MAX_ADDR; i++) {
            struct phy_device *phydev = mdio_bus->phy_map[i];

            if (!phydev)
                continue;

            num_phys++;

            dev_dbg(&client->dev, "Bus PHY dev %p addr 0x%x attached %i drv %s\n",
                    &phydev->dev,
                    i,
                    phydev->attached_dev ? 1 : 0,
                    phydev->dev.driver ? phydev->dev.driver->name : "");
        }

        mutex_unlock(&mdio_bus->mdio_lock);

        dev_dbg(&client->dev, "Bus has %u PHY devices mask 0x%x\n",
                num_phys, mdio_bus->phy_mask);
    }

    // Drop the whole MDIO bus if there are no PHYs.
    // Recreate it when PHY appears.
    if (num_phys == 0) {
        if (mdio_bus) {
            flx_i2c_mdio_put_mdiobus(dp);
        }

        // Detect presence of PHY by reading PHY ID registers.
        dev_dbg(&client->dev, "Test read I2C slave 0x%hx\n",
                 dp->i2c_client->addr);
        ret = i2c_smbus_read_word_swapped(dp->i2c_client, MII_PHYSID1);
        if (ret < 0)
            goto out;
        ret = i2c_smbus_read_word_swapped(dp->i2c_client, MII_PHYSID2);
        if (ret < 0)
            goto out;

        // Looks like there could be a PHY. Setup MDIO bus.
        flx_i2c_mdio_get_mdiobus(dp);
    }

out:
    queue_delayed_work(drv->wq, &dp->check_bus, MDIO_BUS_CHECK_INTERVAL);

    return;
}

#endif

/**
 * I2C slave probe function.
 * Create virtual MDIO bus for the I2C slave.
 * @param client I2C client.
 * @param id I2C client identification information.
 */
static int __devinit flx_i2c_mdio_device_init(struct i2c_client *client,
                                              const struct i2c_device_id *id)
{
    struct flx_i2c_mdio_drv_priv *drv = get_drv_priv();
    struct flx_i2c_mdio_dev_priv *dp = NULL;
    unsigned long int dev_num = 0;
    int ret = -ENXIO;

    dev_dbg(&client->dev, "New device\n");

    dev_num = find_first_zero_bit(drv->used_devices, MAX_DEVICES);

    if (dev_num >= MAX_DEVICES) {
        dev_warn(&client->dev, "Too many devices");
        goto err_too_many;
    }

    dp = kzalloc(sizeof(*dp), GFP_KERNEL);
    if (!dp) {
        dev_err(&client->dev, "Failed to allocate privates\n");
        ret = -ENOMEM;
        goto err_alloc_priv;
    }

    *dp = (struct flx_i2c_mdio_dev_priv) {
        .dev_num = dev_num,
        .i2c_client = client,
    };

    set_bit(dp->dev_num, drv->used_devices);
    list_add(&dp->list, &drv->devices);

#if MDIO_BUS_CHECK_INTERVAL > 0
    INIT_DELAYED_WORK(&dp->check_bus, &flx_i2c_mdio_check_bus);
    INIT_WORK(&dp->discard_bus, &flx_i2c_mdio_discard);

    if (!drv->wq) {
        pr_debug(DRV_NAME ": Creating work queue\n");
        drv->wq = create_singlethread_workqueue(DRV_NAME);
        if (!drv->wq) {
            pr_err(DRV_NAME ": Failed to create work queue\n");
            goto err_create_wq;
        }

        ret = bus_register_notifier(&mdio_bus_type, &flx_i2c_mdio_notifier);
        if (ret) {
            printk(KERN_WARNING DRV_NAME ": Failed to register notifier\n");
        }
    }

#ifdef CONFIG_OF
    dp->detect_changes = !of_property_read_bool(client->dev.of_node,
                                                "disable-change-detection");
#else
    dp->detect_changes = true;
#endif
    if (dp->detect_changes) {
        queue_delayed_work(drv->wq, &dp->check_bus, MDIO_BUS_CHECK_INTERVAL);
    }
#endif

    flx_i2c_mdio_get_mdiobus(dp);

    return 0;

err_create_wq:
    list_del(&dp->list);
    clear_bit(dp->dev_num, drv->used_devices);

    dp->i2c_client = NULL;
    kfree(dp);

err_too_many:
err_alloc_priv:
    return ret;
}

/**
 * I2C slave cleanup function: unregister virtual MDIO bus for I2C slave.
 * @param client I2C client instance.
 */
static int __devexit flx_i2c_mdio_device_cleanup(struct i2c_client *client)
{
    struct flx_i2c_mdio_drv_priv *drv = get_drv_priv();
    struct flx_i2c_mdio_dev_priv *dp = NULL;
    struct flx_i2c_mdio_dev_priv *pos = NULL;
    int ret = 0;

    dev_dbg(&client->dev, "Cleanup device\n");

    list_for_each_entry(pos, &drv->devices, list) {
        if (pos->i2c_client == client) {
            dp = pos;
            break;
        }
    }

    if (!dp) {
        dev_err(&client->dev, "Failed to find virtual MDIO bus"
                " for I2C client 0x%hx\n", client->addr);
        return -ENXIO;
    }

#if MDIO_BUS_CHECK_INTERVAL > 0
    cancel_delayed_work_sync(&dp->check_bus);
    flush_workqueue(drv->wq);
#endif

    flx_i2c_mdio_put_mdiobus(dp);

    list_del(&dp->list);
    dp->i2c_client = NULL;
    clear_bit(dp->dev_num, drv->used_devices);

    kfree(dp);

    if (list_empty(&drv->devices)) {
#if MDIO_BUS_CHECK_INTERVAL > 0
        bus_unregister_notifier(&mdio_bus_type, &flx_i2c_mdio_notifier);

        destroy_workqueue(drv->wq);
        drv->wq = NULL;
#endif
    }

    return ret;
}

/**
 * I2C ID table of I2C slaves for PHY access.
 */
static const struct i2c_device_id flx_i2c_mdio_idtable[] = {
    /// Finisar SFP module (0xAC without read/write bit becomes 0x56)
    { FLX_I2C_MDIO_BUS_NAME, 0x56 },
    { },
};

MODULE_DEVICE_TABLE(i2c, flx_i2c_mdio_idtable);

#ifdef CONFIG_OF
/**
 * Device tree match table
 */
static const struct of_device_id flx_i2c_mdio_match[] = {
    {.compatible = "flx,i2c-mdio"},
    {},
};
#endif

/**
 * I2C driver definition
 */
static struct i2c_driver flx_i2c_mdio_driver = {
    .driver = {
               .name = FLX_I2C_MDIO_BUS_NAME,
               .owner = THIS_MODULE,
#ifdef CONFIG_OF
               .of_match_table = flx_i2c_mdio_match,
#endif
               },
    .id_table = flx_i2c_mdio_idtable,
    .probe = &flx_i2c_mdio_device_init,
    .remove = &flx_i2c_mdio_device_cleanup,
};

/**
 * Initialize driver.
 */
static int __init flx_i2c_mdio_init(void)
{
    int ret = 0;
    struct flx_i2c_mdio_drv_priv *drv = get_drv_priv();

    printk(KERN_DEBUG DRV_NAME ": Init driver\n");

    INIT_LIST_HEAD(&drv->devices);
    bitmap_zero(drv->used_devices, MAX_DEVICES);

    ret = i2c_add_driver(&flx_i2c_mdio_driver);
    if (ret) {
        printk(KERN_WARNING DRV_NAME ": Failed to register i2c driver\n");
        goto err_mdio_driver;
    }

err_mdio_driver:
    return ret;
}

/**
 * Cleanup driver.
 */
static void __exit flx_i2c_mdio_cleanup(void)
{
    printk(KERN_DEBUG DRV_NAME ": module cleanup\n");

    i2c_del_driver(&flx_i2c_mdio_driver);
}

// Module init and exit function
module_init(flx_i2c_mdio_init);
module_exit(flx_i2c_mdio_cleanup);
