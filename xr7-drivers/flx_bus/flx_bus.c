/** @file
 */

/*

   Indirect register access Linux bus driver

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

#define DRV_NAME    "flx_bus"
#define DRV_VERSION "1.11.1"

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/version.h>

#include "flx_bus.h"

// Module description information
MODULE_DESCRIPTION("Indirect register access bus driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

// of_platform_depopulate appeared in Linux 3.16
#ifdef CONFIG_OF
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
static void of_platform_depopulate(struct device *parent);

static int of_platform_device_destroy(struct device *dev, void *data)
{
    if (!dev->of_node) {
        return 0;
    }

    // Recurse
    of_platform_depopulate(dev);

    if (dev->bus == &platform_bus_type)
        platform_device_unregister(to_platform_device(dev));

    return 0;
}

static void of_platform_depopulate(struct device *parent)
{
    device_for_each_child(parent, NULL, of_platform_device_destroy);
}
#endif
#endif

/**
 * Bus type match function.
 */
static int flx_bus_match(struct device *dev, struct device_driver *drv)
{
#ifdef CONFIG_OF
    if (of_driver_match_device(dev, drv))
        return 1;
#endif

    return 0;
}

/**
 * Bus type probe function.
 */
static int flx_bus_probe(struct device *dev)
{
    get_device(dev);

    return 0;
}

/**
 * Bus type remove function.
 */
static int flx_bus_remove(struct device *dev)
{
    put_device(dev);

    return 0;
}

/**
 * Bus type definition.
 */
struct bus_type flx_bus_type = {
    .name = DRV_NAME,
    .match = &flx_bus_match,
    .probe = &flx_bus_probe,
    .remove = &flx_bus_remove,
};
EXPORT_SYMBOL(flx_bus_type);

/**
 * Device type release callback function.
 */
static void flx_bus_release(struct device *dev)
{
    // no-op
}

/**
 * Device type to identify bus devices from other devices.
 * Currently bus type is used only for bus devices.
 */
static struct device_type flx_bus_dev_type = {
    .release = &flx_bus_release,
};

#ifdef CONFIG_OF
/**
 * Construct a unique name for device on bus.
 * This would be unnecessary if we could use of_platform_populate.
 * Adapted from of_device_make_bus_id because it is not exported.
 */
static void of_flx_bus_set_name(struct device *dev)
{
    struct device_node *node = dev->of_node;
    int length = 0;
    const __be32 *reg;

    // Construct the name, using parent nodes if necessary to ensure uniqueness.
    while (node->parent) {
        /*
         * If the address can be translated, then that is as much
         * uniqueness as we need. Make it the first component and return.
         */
        reg = of_get_property(node, "reg", &length);
        if (reg && length >= sizeof(uint32_t)) {
            dev_set_name(dev, dev_name(dev) ? "%x.%s:%s" : "%x.%s",
                         be32_to_cpu(reg[0]), node->name, dev_name(dev));
            return;
        }

        // Format arguments only used if dev_name() resolves to NULL.
        dev_set_name(dev, dev_name(dev) ? "%s:%s" : "%s",
                     strrchr(node->full_name, '/') + 1, dev_name(dev));
        node = node->parent;
    }

    return;
}

/**
 * Create platform device for a device on bus.
 * This would be unnecessary if we could use of_platform_populate.
 * @param bus Host Bus of the device.
 * @param node Device tree node of the device to create.
 * @return New platform device or NULL on error.
 */
static struct platform_device *of_flx_bus_create_device(
        struct flx_bus *bus,
        struct device_node *node)
{
    struct platform_device *pdev;
    struct of_phandle_args irq;
    int num_reg = 0;
    int num_irq = 0;
    const __be32 *reg;
    int length = 0;

    dev_dbg(&bus->dev, "%s() %s\n", __func__, node->name);

    // Name is generated later.
    pdev = platform_device_alloc("", -1);
    if (!pdev) {
        dev_err(&bus->dev, "platform_device_alloc failed for %s\n",
                node->name);
        return NULL;
    }

    // Determine number of resources (registers and interrupts).

    // Bus uses #address-cells 1 and #size-cells 1. Check.
    reg = of_get_property(node, "reg", &length);
    if (!reg || length & (2*sizeof(uint32_t) - 1)) {
        dev_err(&bus->dev, "Node %s has invalid reg value\n", node->name);
        goto err_reg;
    }
    num_reg = length / (2*sizeof(uint32_t));

    // of_irq_count is not exported. of_irq_parse_one appeared in Linux 3.13.
    while (of_irq_parse_one(node, num_irq, &irq) == 0) {
        num_irq++;
    }

    pdev->num_resources = num_reg + num_irq;
    dev_dbg(&bus->dev, "%s() %s num_reg %u num_irq %u\n",
            __func__, node->name, num_reg, num_irq);

    // Allocate and set resources.
    if (pdev->num_resources) {
        struct resource *res;
        unsigned int i;

        res = kzalloc(sizeof(*res) * pdev->num_resources, GFP_KERNEL);
        if (!res) {
            pdev->num_resources = 0;
            dev_err(&bus->dev, "kmalloc failed\n");
            goto err_res;
        }

        pdev->resource = res;

        for (i = 0; i < num_reg; i++) {
            res[i].start = be32_to_cpu(reg[2*i]);
            res[i].end = res[i].start + be32_to_cpu(reg[2*i + 1]) - 1;
            // This is not memory mapped I/O so use a different resource type.
            res[i].flags = IORESOURCE_REG;
            dev_dbg(&bus->dev,
                    "%s() %s res %u start 0x%llx size 0x%llx flags 0x%lx\n",
                    __func__, node->name, i,
                     (unsigned long long int)res[i].start,
                     (unsigned long long int)resource_size(&res[i]),
                     res[i].flags);
        }

        if (of_irq_to_resource_table(node, &res[num_reg], num_irq) != num_irq) {
            dev_info(&bus->dev, "%s() Not all IRQ resources mapped for %s\n",
                     __func__, node->name);
        }
    }

    // Set rest of the platform device.
    pdev->dev.of_node = of_node_get(node);
    pdev->dev.parent = &bus->dev;
    // Must use platform bus here.
    pdev->dev.bus = &platform_bus_type;

    // Construct unique name.
    of_flx_bus_set_name(&pdev->dev);
    pdev->name = dev_name(&pdev->dev);

    // Add platform device to system.
    if (device_add(&pdev->dev)) {
        dev_err(&bus->dev, "%s() device_add failed for %s\n",
                __func__, node->name);
        goto err_add;
    }

    dev_dbg(&bus->dev, "%s() added %s with name %s\n",
            __func__, node->name, pdev->name);
    return pdev;

err_add:
err_res:
err_reg:
    platform_device_put(pdev);
    return NULL;
}

/**
 * Create platform devices from bus device tree children.
 * Unfortunately of_platform_populate assumes memory mapped I/O,
 * so we duplicate it here in a simplified form.
 * @param bus Bus whose device tree children to scan.
 * @return Zero on success or negative error code.
 */
static int of_flx_bus_populate(struct flx_bus *bus)
{
    struct device_node *root = bus->dev.of_node;
    struct device_node *node;

    if (!root)
        return -ENODEV;

    dev_dbg(&bus->dev, "%s() populate %s\n", __func__, root->name);

    for_each_available_child_of_node(root, node) {
        // Make sure it has a compatible property.
        if (!of_get_property(node, "compatible", NULL))
            continue;

#ifdef OF_POPULATED
        if (of_node_test_and_set_flag(node, OF_POPULATED)) {
            continue;
        }
#endif

        if (!of_flx_bus_create_device(bus, node)) {
#ifdef OF_POPULATED
            of_node_clear_flag(node, OF_POPULATED);
#endif
            dev_dbg(&bus->dev, "%s() Failed to create device for %s\n",
                    __func__, node->name);
        }
    }

#ifdef OF_POPULATED_BUS
    of_node_set_flag(root, OF_POPULATED_BUS);
#endif
    of_node_put(root);

    return 0;
}

/**
 * Bus iterator function to locate given device node.
 * @param dev Device to check.
 * @param bus_node Device tree node whose device to locate.
 * @return true if found.
 */
static int of_flx_bus_match_node(struct device *dev,
                                 void *bus_node)
{
    return dev->of_node == bus_node;
}

struct flx_bus *of_flx_bus_get(struct device_node *bus_node)
{
    struct device *dev;
    struct flx_bus *bus = NULL;

    if (!bus_node)
	return NULL;

    dev = bus_find_device(&flx_bus_type, NULL, bus_node,
                          &of_flx_bus_match_node);
    if (!dev)
        return NULL;

    // Bus device reference count has been increased.

    if (dev->type == &flx_bus_dev_type) {
        bus = to_flx_bus(dev);
        if (!try_module_get(bus->owner)) {
            put_device(dev);
            bus = NULL;
        }
    }

    return bus;
}
EXPORT_SYMBOL(of_flx_bus_get);

struct flx_bus *of_flx_bus_get_by_device(struct device_node *node)
{
    struct device_node *parent = of_get_parent(node);
    struct flx_bus *bus = NULL;

    if (parent) {
        bus = of_flx_bus_get(parent);
        of_node_put(parent);
    }

    return bus;
}
EXPORT_SYMBOL(of_flx_bus_get_by_device);
#endif

/**
 * Generic 32-bit read operation implemented using 16-bit reads.
 */
static int flx_bus_generic_read32(struct flx_bus *bus,
                                  uint32_t addr, uint32_t *value)
{
    int ret;
    uint16_t low = 0xffff;
    uint16_t high = 0xffff;

    ret = bus->read16(bus, addr, &low);
    if (ret < 0)
        return ret;

    ret = bus->read16(bus, addr + 2, &high);
    if (ret < 0)
        return ret;

    *value = ((uint32_t)high << 16) | low;

    return 0;
}

/**
 * Generic 32-bit write operation implemented using 16-bit writes.
 */
static int flx_bus_generic_write32(struct flx_bus *bus,
                                   uint32_t addr, uint32_t value)
{
    int ret;

    ret = bus->write16(bus, addr, value & 0xffff);
    if (ret < 0)
        return ret;

    ret = bus->write16(bus, addr + 2, value >> 16);
    if (ret < 0)
        return ret;

    return 0;
}

int flx_bus_register(struct flx_bus *bus, struct device *parent)
{
    int err;

    if (NULL == bus || NULL == bus->name ||
        NULL == bus->read16 || NULL == bus->write16) {
        return -EINVAL;
    }

    // Use generic read32 and write32 implementation if they are not set.
    if (!bus->read32)
        bus->read32 = &flx_bus_generic_read32;
    if (!bus->write32)
        bus->write32 = &flx_bus_generic_write32;

    bus->dev.of_node = parent ? parent->of_node : NULL;
    bus->dev.bus = &flx_bus_type;
    bus->dev.type = &flx_bus_dev_type;
    bus->dev.groups = NULL;
    dev_set_name(&bus->dev, "%s-%u", bus->name, bus->num);

    err = device_register(&bus->dev);
    if (err) {
	pr_err("flx_bus %s failed to register\n", bus->name);
	put_device(&bus->dev);
	return err;
    }

#ifdef CONFIG_OF
    // Unfortunately we can't use of_platform_populate.
    //err = of_platform_populate(bus->dev.of_node, NULL, NULL, &bus->dev);
    err = of_flx_bus_populate(bus);
    if (err) {
        dev_err(&bus->dev, "Failed to register child devices\n");

        // Note: must use device_del here, not device_unregister.
        device_del(&bus->dev);
	put_device(&bus->dev);
        return err;
    }
#endif

    if (bus->reset) {
	bus->reset(bus);
    }

    return 0;
}
EXPORT_SYMBOL(flx_bus_register);

void flx_bus_unregister(struct flx_bus *bus)
{
#ifdef CONFIG_OF
    // Our of_flx_bus_populate is meant to be compatible with this.
    of_platform_depopulate(&bus->dev);
#endif

    // Note: must use device_del here, not device_unregister.
    device_del(&bus->dev);
}
EXPORT_SYMBOL(flx_bus_unregister);


void flx_bus_put(struct flx_bus *bus)
{
    module_put(bus->owner);
    put_device(&bus->dev);
}
EXPORT_SYMBOL(flx_bus_put);

/**
 * Module initialization function.
 */
static int __init flx_bus_init(void)
{
    int ret = bus_register(&flx_bus_type);

    if (ret) {
        pr_err(DRV_NAME ": Failed to register bus type\n");
    }

    return ret;
}

/**
 * Module cleanup function.
 */
static void __exit flx_bus_cleanup(void)
{
    bus_unregister(&flx_bus_type);
}

module_init(flx_bus_init);
module_exit(flx_bus_cleanup);
