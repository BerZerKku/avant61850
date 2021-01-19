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

#ifndef FLX_BUS_H
#define FLX_BUS_H

#include <linux/device.h>
#include <linux/module.h>

/**
 * Bus type.
 */
extern struct bus_type flx_bus_type;

/**
 * Indirect register access bus context.
 * Bus drivers need to embed this in their device privates,
 * set the required fields and call #flx_bus_register.
 */
struct flx_bus {
    /// Owner for module reference count updates
    struct module *owner;
    /// Bus name
    const char *name;
    /// Bus number
    unsigned int num;

    /// 16-bit bus read operation
    int (*read16)(struct flx_bus *bus, uint32_t addr, uint16_t *value);
    /// 16-bit bus write operation
    int (*write16)(struct flx_bus *bus, uint32_t addr, uint16_t value);
    /// 32-bit bus read operation (optional)
    int (*read32)(struct flx_bus *bus, uint32_t addr, uint32_t *value);
    /// 32-bit bus write operation (optional)
    int (*write32)(struct flx_bus *bus, uint32_t addr, uint32_t value);
    /// Bus reset
    int (*reset)(struct flx_bus *bus);

    /// Bus device (managed automatically)
    struct device dev;
};

/// Get access to bus context by its bus device
#define to_flx_bus(d) container_of(d, struct flx_bus, dev)

/**
 * Register new indirect register access bus.
 * This is used by bus implementation drivers.
 * Function #flx_bus_unregister must be called to unregister the bus.
 * @param bus New bus to register.
 * @param parent Parent device of the new bus device.
 */
int flx_bus_register(struct flx_bus *bus, struct device *parent);

/**
 * Unregister indirect register access bus.
 * @param bus Bus to unregister.
 */
void flx_bus_unregister(struct flx_bus *bus);

#ifdef CONFIG_OF
/**
 * Get indirect register access bus by its device tree node.
 * This can be used by device drivers which want to support indirect accesses,
 * to detect whether the device is attached to indirect register access bus
 * or not. Function #flx_bus_put must be called when done with the bus.
 * @param bus_node Device tree node of the flx_bus.
 * @return Corresponding bus instance or NULL if bus_node
 * is not an indirect register access bus.
 */
struct flx_bus *of_flx_bus_get(struct device_node *bus_node);

/**
 * Get indirect register access bus for device node on that bus.
 * See #of_flx_bus_find_bus for details.
 * @param node Child node whose bus to get.
 */
struct flx_bus *of_flx_bus_get_by_device(struct device_node *node);
#endif

/**
 * Release access to indirect register access bus.
 * This must be called after #of_flx_bus_find_bus.
 */
void flx_bus_put(struct flx_bus *bus);

/*
 * Register access functions for drivers which want to support
 * indirect register accesses.
 */

static inline int flx_bus_read16(struct flx_bus *bus,
                                 uint32_t addr, uint16_t *value)
{
    return bus->read16(bus, addr, value);
}

static inline int flx_bus_write16(struct flx_bus *bus,
                                  uint32_t addr, uint16_t value)
{
    return bus->write16(bus, addr, value);
}

static inline int flx_bus_read32(struct flx_bus *bus,
                                 uint32_t addr, uint32_t *value)
{
    return bus->read32(bus, addr, value);
}

static inline int flx_bus_write32(struct flx_bus *bus,
                                  uint32_t addr, uint32_t value)
{
    return bus->write32(bus, addr, value);
}

#endif
