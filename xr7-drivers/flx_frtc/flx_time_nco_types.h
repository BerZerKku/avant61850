/** @file
 */

/*

   Flexibilis Real-Time Clock Linux driver

   Copyright (C) 2009 Flexibilis Oy

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

#ifndef FLX_TIME_COMP_TYPES_H
#define FLX_TIME_COMP_TYPES_H

#ifdef CONFIG_FLX_BUS
# include <flx_bus/flx_bus.h>
#endif

// flx_read32(), flx_write32()
#include <flx_pci/flx_pci.h>

#include <flx_time/flx_time_types.h>

/* IOCTL defines struct flx_time_type - that's used everywhere, so we
   include it through this file. */
#include <flx_time/flx_time_ioctl.h>

// FRTC registers

/**
 * GENERAL register
 */
#define GENERAL_REG             0x00000000
#define REVID_MASK              0xff
#define REVID_SHIFT             0
#define DEVID_MASK              0xffff
#define DEVID_SHIFT             8
#define RESET_SHIFT             31
#define RESET_BIT               (1u << RESET_SHIFT)

/**
 * CUR_NSEC register
 */
#define NCO_SUBNSEC_REG         0x00001000
#define NCO_SUBNSEC_MASK        0x0000ffff

#define NCO_NSEC_REG            0x00001004
#define NCO_NSEC_MASK           0x3fffffff

/**
 * CUR_SEC register
 */
#define NCO_SEC_REG             0x00001008
#define NCO_SEC_HI_REG          0x0000100C
#define NCO_SEC_MASK            0xffffffffffffULL
#define NCO_SEC_HI_MASK         ((uint32_t)(NCO_SEC_MASK >> 32))

/**
 * TIME_CC register
 */
#define NCO_CCCNT_REG           0x00001010
#define NCO_CCCNT_HI_REG        0x00001014
#define NCO_CC_MASK             0xffffffffffffULL

/**
 * STEP_SIZE register
 */
#define NCO_STEP_SUBNSEC_REG    0x00001020
#define NCO_STEP_NSEC_REG       0x00001024
#define NCO_STEP_NSEC_MASK      0x3f

/**
 * ADJUST_NSEC register
 */
#define NCO_ADJ_NSEC_REG        0x00001034
#define NCO_ADJ_NSEC_MASK       0x3fffffff

/**
 * ADJUST_SEC register
 */
#define NCO_ADJ_SEC_REG         0x00001038
#define NCO_ADJ_SEC_HI_REG      0x0000103c
#define NCO_ADJ_SEC_MASK        0xffffffffffffULL
#define NCO_ADJ_SEC_HI_MASK     ((uint32_t)(NCO_ADJ_SEC_MASK >> 32))

/**
 * TIME_CMD register
 */
#define NCO_CMD_REG             0x00001040
#define NCO_CMD_ADJUST_CLOCK    0x1
#define NCO_CMD_ADJUST_STEP     0x2
#define NCO_CMD_READ            0x4

/**
 * Default step size
 */
#if 0
#define NCO_DEFAULT_STEP_NSEC    7
#define NCO_DEFAULT_STEP_SUBNSEC 0xF5CF9A1B
#else
#define NCO_DEFAULT_STEP_NSEC    8
#define NCO_DEFAULT_STEP_SUBNSEC 0
#endif

/**
 * Struct of device private information.
 */
struct flx_time_comp_priv {
    struct flx_time_comp_priv_common common;

    struct flx_time_comp_priv *next_comp;

    uint32_t id;                ///< Device type
    uint32_t step_nsec;         ///< Nominal step size nanoseconds part
    uint32_t step_subnsec;      ///< Nominal step size subnanoseconds part
    uint32_t adjust_scale_factor;       ///< Scaling factor for freq adjust
    uint32_t cur_step_nsec;     ///< Current step size nanoseconds part

#ifdef CONFIG_FLX_BUS
    struct flx_bus *flx_bus;    ///< Indirect register access
    uint32_t bus_addr;          ///< Indirect access bus address
    struct mutex lock_indirect;         ///< Lock for indirect register access
#endif
    spinlock_t lock_direct;             ///< Lock for MMIO register access

    void (*lock)(struct flx_time_comp_priv *nco);
    void (*unlock)(struct flx_time_comp_priv *nco);
    void (*relax)(struct flx_time_comp_priv *nco);
};

/**
 * Read NCO register value using configured access method.
 */
static inline uint32_t flx_nco_read32(struct flx_time_comp_priv *cp,
                                      uint32_t addr)
{
#ifdef CONFIG_FLX_BUS
    if (cp->flx_bus) {
        uint32_t value = 0;
        int ret = flx_bus_read32(cp->flx_bus, cp->bus_addr + addr, &value);
        if (ret < 0)
            return 0xffffffffu;
        return value;
    }
#endif

    return flx_read32(cp->common.ioaddr + addr);
}

/**
 * Write NCO register using configured access method.
 */
static inline void flx_nco_write32(struct flx_time_comp_priv *cp,
                                   uint32_t addr, uint32_t value)
{
#ifdef CONFIG_FLX_BUS
    if (cp->flx_bus) {
        flx_bus_write32(cp->flx_bus, cp->bus_addr + addr, value);
        return;
    }
#endif

    flx_write32(value, cp->common.ioaddr + addr);
}

#endif
