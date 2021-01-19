/** @file flx_pci.h
 * @brief List of FLX_pci driver API functions
 */

/*

   Flexibilis PCI device driver for Linux

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

#ifndef FLX_PCI_H
#define FLX_PCI_H

#include <asm/io.h>
#include <linux/version.h>

/**
 * FLX PCI device config.
 */
struct flx_dev_cfg {
    uint32_t sub_id;            ///< component subid
    void *cfg;                  ///< component config
};

/// @todo resource_size is already defined in newer kernels.
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)

#include <linux/ioport.h>

static inline resource_size_t resource_size(const struct resource *res)
{
    return res->end - res->start + 1;
}
#endif

/**
 * 32-bit data I/O read access
 * @param ioaddr Address to read.
 * @return Data being read
 */
static inline uint32_t flx_read32(void __iomem * ioaddr)
{
    return readl(ioaddr);
}

/**
 * 64-bit data I/O read access.
 * @param ioaddr Address to read.
 * @return Data being read
 */
static inline uint64_t flx_read64(void __iomem * ioaddr)
{
    uint64_t data = 0;

    data = readl(ioaddr);
    data |= (uint64_t) readl(ioaddr + 4) << 32;

    return data;
}

/**
 * 32-bit data I/O write access.
 * @param ioaddr Byte address to 32-bit write data access
 */
static inline void flx_write32(uint32_t data, void __iomem * ioaddr)
{
    writel(data, ioaddr);
}

/**
 * 64-bit data I/O write access.
 * @param ioaddr Byte address to 64-bit write data access
 */
static inline void flx_write64(uint64_t data, void __iomem * ioaddr)
{
    writel((uint32_t) data, ioaddr);
    writel((uint32_t) (data >> 32), ioaddr + 4);
}

#endif
