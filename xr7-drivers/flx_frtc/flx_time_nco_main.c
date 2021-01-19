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

#define DRV_NAME           "flx_frtc"
#define DRV_VERSION        "1.11.1"

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sched.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif

// FLX general defines
#include <flx_pci/flx_pci.h>
#include <flx_pci_config/flx_modules.h>

#include "flx_time_nco_types.h"
#include "flx_time_nco_proc.h"

// sub components
#include "flx_time_nco.h"

// __devinit and friends disappeared in Linux 3.8.
#ifndef __devinit
#define __devinit
#define __devexit
#endif

/*
 * Module description information
 */

MODULE_DESCRIPTION("Flexibilis Real-Time Clock (FRTC/RTC) driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");

/** Module params */
unsigned int nco_step_nsec = 0;
module_param(nco_step_nsec, uint, S_IRUGO);
MODULE_PARM_DESC(nco_step_nsec, "Nanosecond stepsize");

unsigned int nco_step_subnsec = 0;
module_param(nco_step_subnsec, uint, S_IRUGO);
MODULE_PARM_DESC(nco_step_subnsec, "SubNanosecond stepsize");

static struct flx_time_comp_priv *comp_list_head = NULL;

static int flx_time_comp_init(struct platform_device *pdev, uint32_t id);
static void flx_time_comp_exit(struct flx_time_comp_priv *cp);

/**
 * Get data from device.
 * @param dp private data
 * @param time_data Return with time data filled in.
 * @return 0 if ok - or negative error code.
 */
int get_time_data_nco(struct flx_time_comp_priv_common *cp,
                      struct flx_time_get_data *time_data)
{
    struct flx_time_comp_priv *nco =
        container_of(cp, struct flx_time_comp_priv, common);
    struct flx_if_property prop;
    int ret;

    prop.index = time_data->index;
    ret = get_interface_properties(cp, &prop);
    if (ret < 0)
        return ret;

    switch (prop.type) {
    case FLX_TIME_LOCAL_NCO:
        ret = read_nco_time(nco, time_data);
        break;
    default:
        printk(KERN_DEBUG
               "%s: Unknown FLX_TIME interface type: %d (%s).\n",
               flx_time_NAME, prop.type, prop.name);
        return -ENODEV;
    }

    return ret;

}

int clk_adj_nco(struct flx_time_comp_priv_common *cp,
                struct flx_time_clock_adjust_data *clk_adj_data)
{
    struct flx_time_comp_priv *nco =
        container_of(cp, struct flx_time_comp_priv, common);
    struct flx_if_property prop;
    int ret;

    prop.index = clk_adj_data->index;
    ret = get_interface_properties(cp, &prop);
    if (ret < 0)
        return ret;

    switch (prop.type) {
    case FLX_TIME_LOCAL_NCO:
        ret = nco_adj_time(nco, clk_adj_data);
        break;
    default:
        printk(KERN_DEBUG
               "%s: clk_adj is not supported for FLX_TIME interface %s.\n",
               flx_time_NAME, prop.name);
        return -ENODEV;
    }

    if (ret < 0)
        return ret;

    return 0;
}

int freq_adj_nco(struct flx_time_comp_priv_common *cp,
                 struct flx_time_freq_adjust_data *freq_adj_data)
{
    struct flx_time_comp_priv *nco =
        container_of(cp, struct flx_time_comp_priv, common);
    int ret = -ENODEV;

    switch (cp->prop.type) {
    case FLX_TIME_LOCAL_NCO:
        ret = nco_adj_freq(nco, freq_adj_data);
        break;
    default:
        printk(KERN_DEBUG
               "%s: Frequency adjustment is supported only on FLX_TIME_LOCAL_NCO.\n",
               flx_time_NAME);
    }

    return ret;
}

/**
 * Platform devices and probe functions.
 */
#ifdef CONFIG_OF
static const struct of_device_id flx_frtc_match[] = {
    { .compatible = "flx,frtc" },
    { .compatible = "flx,rtc" },
    { },
};
#endif

/**
 * FRTC platform device initialization function.
 */
static int __devinit flx_time_dev_frtc_init(struct platform_device *pdev)
{
    return flx_time_comp_init(pdev, FRTC_DEV_ID);
}

static struct platform_driver frtc_dev_driver = {
    .driver = {
        .name = FRTC_DEV_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_OF
        .of_match_table = flx_frtc_match,
#endif
    },
    .probe = &flx_time_dev_frtc_init,
};

/**
 * Driver initialization.
 * @return 0 if success.
 */
static int __init flx_time_common_init(void)
{
    printk(KERN_DEBUG "%s: Register NCO component(s)\n", flx_time_NAME);
    platform_driver_register(&frtc_dev_driver);

    return 0;
}

/**
 * Driver cleanup.
 */
static void __exit flx_time_common_cleanup(void)
{
    while (comp_list_head) {
        flx_time_comp_exit(comp_list_head);
        comp_list_head = comp_list_head->next_comp;
    }

    platform_driver_unregister(&frtc_dev_driver);

    printk(KERN_DEBUG "%s: module cleanup done.\n", flx_time_NAME);
}

#ifdef CONFIG_FLX_BUS
/**
 * Acquire NCO indirect register access lock.
 */
static inline void nco_lock_indirect(struct flx_time_comp_priv *nco)
{
    mutex_lock(&nco->lock_indirect);
}

/**
 * Release NCO indirect register access lock.
 */
static inline void nco_unlock_indirect(struct flx_time_comp_priv *nco)
{
    mutex_unlock(&nco->lock_indirect);
}

/**
 * Relax CPU when holding indirect register access lock.
 */
static inline void nco_relax_indirect(struct flx_time_comp_priv *nco)
{
    schedule();
}
#endif

/**
 * Acquire NCO register access lock.
 */
static inline void nco_lock(struct flx_time_comp_priv *nco)
{
    spin_lock_bh(&nco->lock_direct);
}

/**
 * Release NCO register access lock.
 */
static inline void nco_unlock(struct flx_time_comp_priv *nco)
{
    spin_unlock_bh(&nco->lock_direct);
}

/**
 * 
 */
static inline void nco_relax(struct flx_time_comp_priv *nco)
{
    cpu_relax();
}

/**
 * Function to initialise time devices.
 * @param pdev Platform device to initialize.
 * @param id Component id
 */
static int flx_time_comp_init(struct platform_device *pdev, uint32_t id)
{
    int ret = -ENODEV;
    struct flx_time_comp_priv *cp;
    struct resource *res = NULL;
    bool indirect = false;

    dev_printk(KERN_DEBUG, &pdev->dev, "probe device\n");

    /* Allocate component private */
    cp = kmalloc(sizeof(struct flx_time_comp_priv), GFP_KERNEL);
    if (!cp) {
        dev_warn(&pdev->dev, "kmalloc failed\n");
        ret = -ENOMEM;
        goto err_1;
    }
    memset(cp, 0, sizeof(struct flx_time_comp_priv));
    cp->id = id;
    // Step size: 1) module parameter 2) device tree 3) built-in default.
    if (nco_step_nsec == 0 && nco_step_subnsec == 0) {
        cp->step_nsec = NCO_DEFAULT_STEP_NSEC;
        cp->step_subnsec = NCO_DEFAULT_STEP_SUBNSEC;
    } else {
        cp->step_nsec = nco_step_nsec;
        cp->step_subnsec = nco_step_subnsec;
    }

#ifdef CONFIG_OF
    if (nco_step_nsec == 0 && nco_step_subnsec == 0) {
        const __be32 *step_size;
        int length = 0;

        step_size =
            of_get_property(pdev->dev.of_node, "step-size", &length);
        if (step_size && length >= sizeof(uint32_t)) {
            cp->step_nsec = be32_to_cpu(step_size[0]);
            if (length >= 2 * sizeof(uint32_t))
                cp->step_subnsec = be32_to_cpu(step_size[1]);
            else
                cp->step_subnsec = 0;
        } else {
            dev_dbg(&pdev->dev, "Unabled to get step_size\n");
        }
    }

#ifdef CONFIG_FLX_BUS
    // Memory mapped or indirect register access.
    cp->flx_bus = of_flx_bus_get_by_device(pdev->dev.of_node);
    if (cp->flx_bus)
        indirect = true;
#endif
#endif

    cp->common.get_time_data = get_time_data_nco;
    cp->common.clk_adj = clk_adj_nco;
    cp->common.freq_adj = freq_adj_nco;
    cp->common.print_status = flx_time_print_nco_status;

    cp->common.pdev = pdev;     ///< Platform device associated with this

    /** Calculate scaling factor.
     * subnsec is 32 bits, adjust data is in ppb (1e-9), 32bit is 4.29e9. 
     * nco_adjust_scale_factor = 10^-9 * nominal_value / (2^-32 ns) == 
     * 4.29 * nominal_value, calculate using U32.8 * U32.8 */
    cp->adjust_scale_factor = 1100 * ((cp->step_nsec << 8) |
                                      (cp->step_subnsec >> 24));
    cp->adjust_scale_factor >>= 16;

    // Register component
    ret = register_component(pdev, &cp->common);

    if (ret < 0) {
        goto err_2;
    }

    // Set properties according the type
    switch (id) {
    case FRTC_DEV_ID:
        cp->common.prop.properties = (TIME_PROP_FREQ_ADJ |
                                      TIME_PROP_CLOCK_ADJ |
                                      TIME_PROP_COUNTER |
                                      TIME_PROP_ACTUAL_TIME |
                                      TIME_PROP_TIMESTAMP);
        break;
    default:
        dev_err(&pdev->dev, "Unknown DEV_ID 0x%x\n", id);
        ret = -ENODEV;
        goto err_2;
    }

    // Set common data
    cp->common.prop.index = ret;
    strcpy(cp->common.prop.name, "Local NCO");
    cp->common.prop.type = FLX_TIME_LOCAL_NCO;

#ifdef CONFIG_FLX_BUS
    if (cp->flx_bus) {
        res = platform_get_resource(pdev, IORESOURCE_REG, 0);
        if (!res) {
            dev_err(&pdev->dev, "I/O registers not defined\n");
            ret = -ENXIO;
            goto err_2;
        }
        cp->bus_addr = res->start;

        mutex_init(&cp->lock_indirect);
        cp->lock = &nco_lock_indirect;
        cp->unlock = &nco_unlock_indirect;
        cp->relax = &nco_relax_indirect;
    }
#endif

    if (!indirect) {
        dev_dbg(&pdev->dev, "Memory mapped register access\n");

        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res) {
            dev_err(&pdev->dev, "I/O registers not defined\n");
            goto err_2;
        }

        // remap io address to virtual
        cp->common.ioaddr =
            ioremap_nocache(res->start, resource_size(res));

        if (cp->common.ioaddr == NULL) {
            dev_warn(&pdev->dev, "Component ioremap failed\n");
            ret = -ENOMEM;
            goto err_2;
        }

        dev_info(&pdev->dev, "Component %s IO remapped to: %p\n",
                 cp->common.prop.name, cp->common.ioaddr);

        spin_lock_init(&cp->lock_direct);
        cp->lock = &nco_lock;
        cp->unlock = &nco_unlock;
        cp->relax = &nco_relax;
    }

    // Register init
    init_nco_registers(cp);

    // Add to local list of components
    cp->next_comp = comp_list_head;
    comp_list_head = cp;

    return 0;

err_2:
#ifdef CONFIG_FLX_BUS
    if (cp->flx_bus) {
        flx_bus_put(cp->flx_bus);
        cp->flx_bus = NULL;
    }
#endif
    kfree(cp);

err_1:
    return ret;
}

/**
 * Function to clean device data.
 */
static void flx_time_comp_exit(struct flx_time_comp_priv *cp)
{
    bool indirect = false;

    printk(KERN_DEBUG "%s: Component %s exit called.\n",
           flx_time_NAME, cp->common.prop.name);

    unregister_component(cp->common.pdev, &cp->common);

#ifdef CONFIG_FLX_BUS
    if (cp->flx_bus) {
        flx_bus_put(cp->flx_bus);
        cp->flx_bus = NULL;
        indirect = true;
    }
#endif
    if (!indirect) {
        // io unmap
        iounmap(cp->common.ioaddr);
        cp->common.ioaddr = NULL;
    }

    kfree(cp);
}

module_init(flx_time_common_init);
module_exit(flx_time_common_cleanup);
