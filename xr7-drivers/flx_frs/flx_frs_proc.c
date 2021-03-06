/** @file
 */

/*

   FRS Linux driver

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

#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include "flx_frs_types.h"
#include "flx_frs_main.h"
#include "flx_frs_proc.h"
#include "flx_frs_if.h"
#include "flx_frs_adapter.h"
#include "flx_frs_ethtool.h"
#include "flx_frs_netdev.h"
#include "flx_frs_sfp.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#define PDE_DATA(inode) PDE(inode)->data
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
static inline struct proc_dir_entry *proc_create_data(
        const char *name,
        umode_t mode,
        struct proc_dir_entry *parent,
        const struct file_operations *proc_fops,
        void *data)
{
    struct proc_dir_entry *entry =
        proc_create(name, mode, parent, proc_fops);

    if (entry)
        entry->data = data;

    return entry;
}
#endif

static struct proc_dir_entry *proc_root_entry;

/**
 * Helper to read 32-bit switch register value.
 */
static uint32_t flx_frs_read_switch_uint32(struct flx_frs_dev_priv *dp,
                                           int low_reg_num)
{
    uint32_t value = 0;
    int data;

    data = flx_frs_read_switch_reg(dp, low_reg_num);
    if (data < 0)
        goto error;

    value = data;

    data = flx_frs_read_switch_reg(dp, low_reg_num + 1);
    if (data < 0)
        goto error;

    value |= (uint32_t) data << 16;

    return value;

  error:
    return 0xffffffffu;
}

/**
 * Print switch common registers.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_common_regs(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    int data;
    unsigned int i;

    seq_printf(m, "Common Registers of device %i:\n", dp->dev_num);

    if (dp->type != FLX_FRS_TYPE_RS) {
        data = flx_frs_read_switch_reg(dp, FRS_REG_ID0);
        seq_printf(m, "FRS ID0\t\t\t\t(0x%04x): 0x%04x\n", FRS_REG_ID0, data);

        data = flx_frs_read_switch_reg(dp, FRS_REG_ID1);
        seq_printf(m, "FRS ID1\t\t\t\t(0x%04x): 0x%04x\n", FRS_REG_ID1, data);

        data = flx_frs_read_switch_reg(dp, FRS_REG_CONFIG_ID);
        seq_printf(m, "FRS configuration ID\t\t(0x%04x): %6d\n",
                   FRS_REG_CONFIG_ID, data);

        data = flx_frs_read_switch_reg(dp, FRS_REG_CONFIG_SVN_ID);
        seq_printf(m, "FRS configuration SVN ID\t(0x%04x): %6d\n",
                   FRS_REG_CONFIG_SVN_ID, data);

        data = flx_frs_read_switch_reg(dp, FRS_REG_BODY_SVN_ID);
        seq_printf(m, "FRS body SVN version\t\t(0x%04x): %6d\n",
                   FRS_REG_BODY_SVN_ID, data);
    }

    data = flx_frs_read_switch_reg(dp, FRS_REG_GEN);
    seq_printf(m, "FRS General\t\t\t(0x%04x): 0x%04x\n",
               FRS_REG_GEN, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_MAC_TABLE_CLEAR_MASK);
    seq_printf(m, "FRS MAC table clear mask\t(0x%04x): 0x%04x\n",
               FRS_REG_MAC_TABLE_CLEAR_MASK, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_CMEM_FILL_LEVEL);
    seq_printf(m, "FRS FRS_REG_CMEM_FILL_LEVEL\t(0x%04x): %6d\n",
               FRS_REG_CMEM_FILL_LEVEL, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_DMEM_FILL_LEVEL);
    seq_printf(m, "FRS FRS_REG_DMEM_FILL_LEVEL\t(0x%04x): %6d\n",
               FRS_REG_DMEM_FILL_LEVEL, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_SEQ_MEM_FILL_LEVEL);
    seq_printf(m, "FRS FRS_REG_SEQ_MEM_FILL_LEVEL\t(0x%04x): %6d\n",
               FRS_REG_SEQ_MEM_FILL_LEVEL, 0xff & data);
    seq_printf(m, "FRS FRS_REG_SEQ_MEM_DEALLOC_ERR\t(0x%04x): %6d\n",
               FRS_REG_SEQ_MEM_FILL_LEVEL, 0xff & (data >> 8));

    data = flx_frs_read_switch_reg(dp, FRS_REG_AGING);
    seq_printf(m, "FRS Aging\t\t\t(0x%04x): 0x%04x\n",
               FRS_REG_AGING, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_AGING_BASE_TIME_LO);
    seq_printf(m, "FRS AGING_BASE_TIME_LO\t\t(0x%04x): 0x%04x\n",
               FRS_REG_AGING_BASE_TIME_LO, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_AGING_BASE_TIME_HI);
    seq_printf(m, "FRS AGING_BASE_TIME_HI\t\t(0x%04x): 0x%04x\n",
               FRS_REG_AGING_BASE_TIME_HI, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_AUTH_STATUS);
    seq_printf(m, "FRS_REG_AUTH_STATUS\t\t(0x%04x): 0x%04x\n",
               FRS_REG_AUTH_STATUS, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_TS_CTRL_TX);
    seq_printf(m, "FRS_REG_TS_CTRL_TX\t\t(0x%04x): 0x%04x\n",
               FRS_REG_TS_CTRL_TX, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_TS_CTRL_RX);
    seq_printf(m, "FRS_REG_TS_CTRL_RX\t\t(0x%04x): 0x%04x\n",
               FRS_REG_TS_CTRL_RX, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_INTMASK);
    seq_printf(m, "FRS_REG_INTMASK\t\t\t(0x%04x): 0x%04x\n",
               FRS_REG_INTMASK, data);

    data = flx_frs_read_switch_reg(dp, FRS_REG_INTSTAT);
    seq_printf(m, "FRS_REG_INTSTAT\t\t\t(0x%04x): 0x%04x\n",
               FRS_REG_INTSTAT, data);

    seq_printf(m, "\n");

    for (i = 0; i < 4; i++) {
        uint32_t sec = flx_frs_read_switch_uint32(dp, FRS_TS_TX_S_LO(i));
        uint32_t nsec = flx_frs_read_switch_uint32(dp, FRS_TS_TX_NS_LO(i));
        seq_printf(m, "FRS_TX_TS_%u [s ns]\t (0x%04x 0x%04x):"
                   " 0x%08x 0x%08x\n",
                   i, FRS_TS_TX_S_LO(i), FRS_TS_TX_NS_LO(i), sec, nsec);
    }

    seq_printf(m, "\n");

    for (i = 0; i < 4; i++) {
        uint32_t sec = flx_frs_read_switch_uint32(dp, FRS_TS_RX_S_LO(i));
        uint32_t nsec = flx_frs_read_switch_uint32(dp, FRS_TS_RX_NS_LO(i));
        seq_printf(m, "FRS_RX_TS_%u [s ns]\t (0x%04x 0x%04x):"
                   " 0x%08x 0x%08x\n",
                   i, FRS_TS_RX_S_LO(i), FRS_TS_RX_NS_LO(i), sec, nsec);
    }

    seq_printf(m, "\n");

    return 0;
}

/**
 * Print VLAN config registers.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_vlan_regs(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    int data;
    unsigned int i = 0;

    seq_printf(m, "VLAN configuration registers of device %i:\n",
               dp->dev_num);

    seq_printf(m, "VLAN ID\tREG NUM\tVALUE\n");
    for (i = 0; i < 4096; i++) {
        data = flx_frs_read_switch_reg(dp, FRS_VLAN_CFG(i));
        seq_printf(m, "%u\t0x%02x\t0x%04x\n", i, FRS_VLAN_CFG(i), data);
    }

    seq_printf(m, "\n");

    return 0;
}

/**
 * Print statistics.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_stats(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    struct flx_frs_port_priv *port = NULL;
    unsigned int i;
    uint64_t *data = NULL;
    const size_t port_data_size = FRS_CNT_REG_COUNT * sizeof(*data);

    // Sample statistics, avoids holding port stats_pock undefined period.
    data = kmalloc(dp->num_of_ports*port_data_size, GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        mutex_lock(&port->stats_lock);
        flx_frs_update_port_stats(port);
        memcpy(&data[i*port_data_size], &port->stats[0], port_data_size);
        mutex_unlock(&port->stats_lock);
    }

    // Print sampled statistics.
    seq_printf(m, "\nStatistic of device %i:\n", dp->dev_num);
    seq_printf(m, "RX tstamp: \t0x%08x\n", dp->stats.rx_stamp);
    seq_printf(m, "TX tstamp: \t0x%08x\n", dp->stats.tx_stamp);
    seq_printf(m, "RX error:  \t0x%08x\n", dp->stats.rx_error);
    seq_printf(m, "Congested: \t0x%08x\n", dp->stats.congested);

    seq_printf(m, "\nPort statistics       \t\t   (REG):");
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t     PORT%i", i);
    }
    seq_printf(m, "\n\n");

    // RX good octets received
    seq_printf(m, "RX good octets\t\t\t(0x%04x):", PORT_REG_RX_GOOD_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_GOOD_OCTETS]);
    }
    seq_printf(m, "\n");

    // RX bad octets received
    seq_printf(m, "RX bad octets\t\t\t(0x%04x):", PORT_REG_RX_BAD_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_BAD_OCTETS]);
    }
    seq_printf(m, "\n");

    // RX unicast frames
    seq_printf(m, "RX unicast frames\t\t(0x%04x):", PORT_REG_RX_UNICAST_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_UNICAST]);
    }
    seq_printf(m, "\n");

    // RX broadcast frames
    seq_printf(m, "RX broadcast frames\t\t(0x%04x):",
               PORT_REG_RX_BROADCAST_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_BROADCAST]);
    }
    seq_printf(m, "\n");

    // RX multicast frames
    seq_printf(m, "RX multicast frames\t\t(0x%04x):",
               PORT_REG_RX_MULTICAST_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_MULTICAST]);
    }
    seq_printf(m, "\n");

    // RX undersize frames
    seq_printf(m, "RX undersize frames\t\t(0x%04x):",
               PORT_REG_RX_UNDERSIZE_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_UNDERSIZE]);
    }
    seq_printf(m, "\n");

    // RX fragment frames
    seq_printf(m, "RX fragment frames\t\t(0x%04x):",
               PORT_REG_RX_FRAGMENT_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_FRAGMENT]);
    }
    seq_printf(m, "\n");

    // RX oversize frames
    seq_printf(m, "RX oversize frames\t\t(0x%04x):",
               PORT_REG_RX_OVERSIZE_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_OVERSIZE]);
    }
    seq_printf(m, "\n");

    // RX error frames (any)
    seq_printf(m, "RX error frames\t\t\t(0x%04x):", PORT_REG_RX_ERR_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_ERR]);
    }
    seq_printf(m, "\n");

    // RX CRC error frames
    seq_printf(m, "RX CRC error frames\t\t(0x%04x):", PORT_REG_RX_CRC_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_CRC]);
    }
    seq_printf(m, "\n");

    seq_printf(m, "\n");

    // RX good HSR/PRP frames
    seq_printf(m, "RX HSR/PRP good frames\t\t(0x%04x):",
               PORT_REG_RX_HSRPRP_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_HSRPRP]);
    }
    seq_printf(m, "\n");

    // PRP wrong LAN error frames
    seq_printf(m, "RX PRP wrong LAN frames\t\t(0x%04x):",
               PORT_REG_RX_WRONGLAN_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_WRONGLAN]);
    }
    seq_printf(m, "\n");

    // HSR/PRP duplicate drops
    seq_printf(m, "RX HSR/PRP duplicate drop\t(0x%04x):",
               PORT_REG_RX_DUPLICATE_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_RX_DUPLICATE]);
    }
    seq_printf(m, "\n");

    seq_printf(m, "\n");

    // TX octets
    seq_printf(m, "TX octets \t\t\t(0x%04x):", PORT_REG_TX_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_TX_OCTETS]);
    }
    seq_printf(m, "\n");

    // TX unicast frames
    seq_printf(m, "TX unicast frames\t\t(0x%04x):", PORT_REG_TX_UNICAST_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_TX_UNICAST]);
    }
    seq_printf(m, "\n");

    // TX broadcast frames
    seq_printf(m, "TX broadcast frames\t\t(0x%04x):",
               PORT_REG_TX_BROADCAST_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_TX_BROADCAST]);
    }
    seq_printf(m, "\n");

    // TX multicast frames
    seq_printf(m, "TX multicast frames\t\t(0x%04x):",
               PORT_REG_TX_MULTICAST_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_TX_MULTICAST]);
    }
    seq_printf(m, "\n");

    seq_printf(m, "\n");

    // TX HSR/PRP frames
    seq_printf(m, "TX HSR/PRP frames\t\t(0x%04x):", PORT_REG_TX_HSRPRP_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_TX_HSRPRP]);
    }
    seq_printf(m, "\n");

    seq_printf(m, "\n");

    // Priority queue full drop
    seq_printf(m, "TX priority queue full drop\t(0x%04x):",
               PORT_REG_TX_PRIQ_DROP_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_TX_PRIQ_DROP]);
    }
    seq_printf(m, "\n");

    // TX early drop, i.e. free memory
    seq_printf(m, "TX early drop\t\t\t(0x%04x):",
               PORT_REG_TX_EARLY_DROP_L);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "\t0x%08llx",
                   data[i*port_data_size + FRS_CNT_TX_EARLY_DROP]);
    }
    seq_printf(m, "\n");

    seq_printf(m, "\n");

    if (dp->type == FLX_FRS_TYPE_DS) {
        // RX policed
        seq_printf(m, "RX policed \t\t\t(0x%04x):", PORT_REG_RX_POLICED_L);
        for (i = 0; i < dp->num_of_ports; i++) {
            port = dp->port[i];
            if (!port)
                continue;
            seq_printf(m, "\t0x%08llx",
                       data[i*port_data_size + FRS_CNT_RX_POLICED]);
        }
        seq_printf(m, "\n");
        // RX MACSEC untagged
        seq_printf(m, "RX MACSEC untagged\t\t(0x%04x):",
                   PORT_REG_RX_MACSEC_UNTAGGED_L);
        for (i = 0; i < dp->num_of_ports; i++) {
            port = dp->port[i];
            if (!port)
                continue;
            seq_printf(m, "\t0x%08llx",
                       data[i*port_data_size + FRS_CNT_RX_MACSEC_UNTAGGED]);
        }
        seq_printf(m, "\n");
        // RX MACSEC not supported
        seq_printf(m, "RX MACSEC not supported\t\t(0x%04x):",
                   PORT_REG_RX_MACSEC_NOTSUPP_L);
        for (i = 0; i < dp->num_of_ports; i++) {
            port = dp->port[i];
            if (!port)
                continue;
            seq_printf(m, "\t0x%08llx",
                       data[i*port_data_size + FRS_CNT_RX_MACSEC_NOTSUPP]);
        }
        seq_printf(m, "\n");
        // RX MACSEC unknown SCI
        seq_printf(m, "RX MACSEC unknown SCI\t\t(0x%04x):",
                   PORT_REG_RX_MACSEC_UNKNOWNSCI_L);
        for (i = 0; i < dp->num_of_ports; i++) {
            port = dp->port[i];
            if (!port)
                continue;
            seq_printf(m, "\t0x%08llx",
                       data[i*port_data_size + FRS_CNT_RX_MACSEC_UNKNOWNSCI]);
        }
        seq_printf(m, "\n");
        // RX MACSEC not valid
        seq_printf(m, "RX MACSEC not valid\t\t(0x%04x):",
                   PORT_REG_RX_MACSEC_NOTVALID_L);
        for (i = 0; i < dp->num_of_ports; i++) {
            port = dp->port[i];
            if (!port)
                continue;
            seq_printf(m, "\t0x%08llx",
                       data[i*port_data_size + FRS_CNT_RX_MACSEC_NOTVALID]);
        }
        seq_printf(m, "\n");
        // RX MACSEC late
        seq_printf(m, "RX MACSEC late\t\t\t(0x%04x):",
                   PORT_REG_RX_MACSEC_LATE_L);
        for (i = 0; i < dp->num_of_ports; i++) {
            port = dp->port[i];
            if (!port)
                continue;
            seq_printf(m, "\t0x%08llx",
                       data[i*port_data_size + FRS_CNT_RX_MACSEC_LATE]);
        }
        seq_printf(m, "\n");

        seq_printf(m, "\n");
    }

    kfree(data);

    return 0;
}

/**
 * Print port registers.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_port_regs(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    int i;
    int data;

    seq_printf(m, "\nPort registers of device %i\t   (REG):", dp->dev_num);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        seq_printf(m, "\tPORT%i", i);
    }
    seq_printf(m, "\n\n");

    seq_printf(m, "State\t\t\t\t(0x%04x):", PORT_REG_STATE);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_STATE);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "VLAN\t\t\t\t(0x%04x):", PORT_REG_VLAN);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_VLAN);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "VLAN0_MAP\t\t\t(0x%04x):", PORT_REG_VLAN0_MAP);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_VLAN0_MAP);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "FWD_PORT_MASK\t\t\t(0x%04x):", PORT_REG_FWD_PORT_MASK);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_FWD_PORT_MASK);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    if (dp->type == FLX_FRS_TYPE_DS) {
        seq_printf(m, "VLAN_PRIO_LO\t\t\t(0x%04x):", PORT_REG_VLAN_PRIO);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i], PORT_REG_VLAN_PRIO);
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");
        seq_printf(m, "VLAN_PRIO_HI\t\t\t(0x%04x):", PORT_REG_VLAN_PRIO_HI);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i], PORT_REG_VLAN_PRIO_HI);
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");
    } else {
        seq_printf(m, "VLAN_PRIO\t\t\t(0x%04x):", PORT_REG_VLAN_PRIO);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i], PORT_REG_VLAN_PRIO);
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");
    }

    seq_printf(m, "HSR_PORT_CFG\t\t\t(0x%04x):", PORT_REG_HSR_CFG);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_HSR_CFG);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "PORT_REG_PTP_DELAY_SN\t\t(0x%04x):",
               PORT_REG_PTP_DELAY_SN);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_PTP_DELAY_SN);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");
    seq_printf(m, "PORT_REG_PTP_DELAY_NSL\t\t(0x%04x):",
               PORT_REG_PTP_DELAY_NSL);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_PTP_DELAY_NSL);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");
    seq_printf(m, "PORT_REG_PTP_DELAY_NSH\t\t(0x%04x):",
               PORT_REG_PTP_DELAY_NSH);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_PTP_DELAY_NSH);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "PORT_REG_PTP_RX_DELAY_SN\t(0x%04x):",
               PORT_REG_PTP_RX_DELAY_SN);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_PTP_RX_DELAY_SN);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "PORT_REG_PTP_RX_DELAY_NS\t(0x%04x):",
               PORT_REG_PTP_RX_DELAY_NS);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_PTP_RX_DELAY_NS);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "PORT_REG_PTP_TX_DELAY_SN\t(0x%04x):",
               PORT_REG_PTP_TX_DELAY_SN);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_PTP_TX_DELAY_SN);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "PORT_REG_PTP_TX_DELAY_NS\t(0x%04x):",
               PORT_REG_PTP_TX_DELAY_NS);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_port_reg(dp->port[i], PORT_REG_PTP_TX_DELAY_NS);
        seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");

    return 0;
}

/**
 * Print port registers.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_port_macsec_regs(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    int i,j;
    int data;

    if (!dp) {
        return 0;
    }

    seq_printf(m, "\nMACsec registers of device %i\t   (REG):", dp->dev_num);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        seq_printf(m, "\tPORT%i", i);
    }
    seq_printf(m, "\n\n");

    seq_printf(m, "MACSEC_CONFIG\t\t\t(0x%04x):",
	       PORT_REG_MACSEC_CONFIG);
    for (i = 0; i < dp->num_of_ports; i++) {
	if (!dp->port[i])
	    continue;
	data = flx_frs_read_port_reg(dp->port[i], PORT_REG_MACSEC_CONFIG);
	seq_printf(m, "\t0x%04x", data);
    }
    seq_printf(m, "\n");
    
    for (j = 0; j <= 3; j++) {
	seq_printf(m, "MACSEC_SCI_TX_%d\t\t\t(0x%04x):",
		   j, PORT_REG_MACSEC_SCI_TX(j));
	for (i = 0; i < dp->num_of_ports; i++) {
	    if (!dp->port[i])
		continue;
	    data = flx_frs_read_port_reg(dp->port[i], PORT_REG_MACSEC_SCI_TX(j));
	    seq_printf(m, "\t0x%04x", data);
	}
	seq_printf(m, "\n");
    }
    for (j = 0; j <= 3; j++) {
	seq_printf(m, "MACSEC_SCI_RX_%d\t\t\t(0x%04x):",
		   j, PORT_REG_MACSEC_SCI_RX(j));
	for (i = 0; i < dp->num_of_ports; i++) {
	    if (!dp->port[i])
		continue;
	    data = flx_frs_read_port_reg(dp->port[i], PORT_REG_MACSEC_SCI_RX(j));
	    seq_printf(m, "\t0x%04x", data);
	}
	seq_printf(m, "\n");
    }
    
    for (j = 0; j <= 15; j++) {
	seq_printf(m, "MACSEC_KEY0_TX_%d\t\t(0x%04x):",
		   j, PORT_REG_MACSEC_KEY0_TX(j));
	for (i = 0; i < dp->num_of_ports; i++) {
	    if (!dp->port[i])
		continue;
	    data = flx_frs_read_port_reg(dp->port[i], PORT_REG_MACSEC_KEY0_TX(j));
	    seq_printf(m, "\t0x%04x", data);
	}
	seq_printf(m, "\n");
    }
    for (j = 0; j <= 15; j++) {
	seq_printf(m, "MACSEC_KEY0_RX_%d\t\t(0x%04x):",
		   j, PORT_REG_MACSEC_KEY0_RX(j));
	for (i = 0; i < dp->num_of_ports; i++) {
	    if (!dp->port[i])
		continue;
	    data = flx_frs_read_port_reg(dp->port[i], PORT_REG_MACSEC_KEY0_RX(j));
	    seq_printf(m, "\t0x%04x", data);
	}
	seq_printf(m, "\n");
    }
    for (j = 0; j <= 15; j++) {
	seq_printf(m, "MACSEC_KEY1_TX_%d\t\t(0x%04x):",
		   j, PORT_REG_MACSEC_KEY1_TX(j));
	for (i = 0; i < dp->num_of_ports; i++) {
	    if (!dp->port[i])
		continue;
	    data = flx_frs_read_port_reg(dp->port[i], PORT_REG_MACSEC_KEY1_TX(j));
	    seq_printf(m, "\t0x%04x", data);
	}
	seq_printf(m, "\n");
    }
    for (j = 0; j <= 15; j++) {
	seq_printf(m, "MACSEC_KEY1_RX_%d\t\t(0x%04x):",
		   j, PORT_REG_MACSEC_KEY1_RX(j));
	for (i = 0; i < dp->num_of_ports; i++) {
	    if (!dp->port[i])
		continue;
	    data = flx_frs_read_port_reg(dp->port[i], PORT_REG_MACSEC_KEY1_RX(j));
	    seq_printf(m, "\t0x%04x", data);
	}
	seq_printf(m, "\n");
    }

    return 0;
}

/**
 * Helper to convert adapter ID to human readable adapter type.
 * @param id Adapter ID value.
 * @return Adapter type string or NULL.
 */
static const char *flx_frs_adapter_type_str(int id)
{
    if (id < 0)
        return NULL;

    switch (id) {
    case 0:
    case ADAPTER_ID_ID_MASK: return NULL;
    case ADAPTER_ID_ALT_TSE: return "ALT_TSE";
    case ADAPTER_ID_1000BASE_X: return "1000Base-X";
    case ADAPTER_ID_100BASE_FX: return "100Base-FX";
    case ADAPTER_ID_100BASE_FX_EXT_TX_PLL: return "100Base-FX";
    case ADAPTER_ID_MII: return "MII";
    case ADAPTER_ID_RGMII: return "RGMII";
    case ADAPTER_ID_RMII: return "RMII";
    case ADAPTER_ID_SGMII_1000BASEX: return "SGMII/1000";
    case ADAPTER_ID_SGMII_1000BASEX_EXT_TX_PLL: return "SGMII/1000";
    case ADAPTER_ID_SGMII_1000BASEX_100BASEFX_EXT_TX_PLL: return "SGMII/100+";
    }

    return "unknown";
}

#define FMT_X16_ADAPTER "0x%04x     "
#define FMT_S_ADAPTER   "%-10s "
#define FMT_NA_ADAPTER  "   -       "

/**
 * Print port adapter registers.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_adapter_regs(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    int i;
    int data;
    int adapter_id[dp->num_of_ports];
    unsigned int adapter_count = 0;
    unsigned int alt_tse_count = 0;
    unsigned int sgmii_1000basex_count = 0;

    seq_printf(m, "\nAdapter registers of device %i\n\n"
               "\t\t\t   (REG):  ",
               dp->dev_num);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        seq_printf(m, "PORT%-7i", i);
    }
    seq_printf(m, "\n\n");

    // Common adapter registers.
    seq_printf(m, "ID\t\t\t(0x%04x):  ", ADAPTER_REG_ID);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        data = flx_frs_read_adapter_reg(dp->port[i], ADAPTER_REG_ID);
        if (data < 0) {
            adapter_id[i] = -1;
        }
        else {
            adapter_count++;
            adapter_id[i] = (data >> ADAPTER_ID_ID_SHIFT) & ADAPTER_ID_ID_MASK;
        }
        if (adapter_id[i] == ADAPTER_ID_ALT_TSE)
            alt_tse_count++;
        if (adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX ||
            adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_EXT_TX_PLL ||
            adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_100BASEFX_EXT_TX_PLL)
            sgmii_1000basex_count++;
        if (data < 0)
            seq_printf(m, FMT_NA_ADAPTER);
        else
            seq_printf(m, FMT_X16_ADAPTER, data);
    }
    seq_printf(m, "\n");

    seq_printf(m, "Type\t\t\t\t:  ");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str;
        if (!dp->port[i])
            continue;
        str = flx_frs_adapter_type_str(adapter_id[i]);
        if (str)
            seq_printf(m, FMT_S_ADAPTER, str);
        else
            seq_printf(m, FMT_NA_ADAPTER);
    }
    seq_printf(m, "\n");

    if (adapter_count == 0)
        return 0;

    seq_printf(m, "Link status\t\t(0x%04x):  ", ADAPTER_REG_LINK_STATUS);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        if (adapter_id[i] > 0 && adapter_id[i] < ADAPTER_ID_ID_MASK) {
            data = flx_frs_read_adapter_reg(dp->port[i],
                                            ADAPTER_REG_LINK_STATUS);
            seq_printf(m, FMT_X16_ADAPTER, data);
        }
        else {
            seq_printf(m, FMT_NA_ADAPTER);
        }
    }
    seq_printf(m, "\n");

    // Type-specific adapter registers.
    if (alt_tse_count > 0) {
        seq_printf(m, "PCS control\t\t(0x%04x):  ", ALT_TSE_PCS_CONTROL);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_ALT_TSE) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                ALT_TSE_PCS_CONTROL);
                seq_printf(m, FMT_X16_ADAPTER, data);
            }
            else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");

        seq_printf(m, "PCS status\t\t(0x%04x):  ", ALT_TSE_PCS_STATUS);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_ALT_TSE) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                ALT_TSE_PCS_STATUS);
                seq_printf(m, FMT_X16_ADAPTER, data);
            }
            else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");

        seq_printf(m, "PCS dev_ability\t\t(0x%04x):  ",
                   ALT_TSE_PCS_DEV_ABILITY);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_ALT_TSE) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                ALT_TSE_PCS_DEV_ABILITY);
                seq_printf(m, FMT_X16_ADAPTER, data);
            } else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");

        seq_printf(m, "PCS partner_ability\t(0x%04x):  ",
                   ALT_TSE_PCS_PARTNER_ABILITY);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_ALT_TSE) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                ALT_TSE_PCS_PARTNER_ABILITY);
                seq_printf(m, FMT_X16_ADAPTER, data);
            } else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");

        seq_printf(m, "PCS if_mode\t\t(0x%04x):  ", ALT_TSE_PCS_IFMODE);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_ALT_TSE) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                ALT_TSE_PCS_IFMODE);
                seq_printf(m, FMT_X16_ADAPTER, data);
            } else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");
    }

    if (sgmii_1000basex_count > 0) {
        seq_printf(m, "PCS control\t\t(0x%04x):  ",
                   SGMII_1000BASEX_REG_PCS_CONTROL);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_EXT_TX_PLL ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_100BASEFX_EXT_TX_PLL) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                SGMII_1000BASEX_REG_PCS_CONTROL);
                seq_printf(m, FMT_X16_ADAPTER, data);
            }
            else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");

        seq_printf(m, "PCS status\t\t(0x%04x):  ",
                   SGMII_1000BASEX_REG_PCS_STATUS);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_EXT_TX_PLL ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_100BASEFX_EXT_TX_PLL) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                SGMII_1000BASEX_REG_PCS_STATUS);
                seq_printf(m, FMT_X16_ADAPTER, data);
            }
            else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");

        seq_printf(m, "PCS SGMII control\t(0x%04x):  ",
                   SGMII_1000BASEX_REG_PCS_SGMII_CONTROL);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_EXT_TX_PLL ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_100BASEFX_EXT_TX_PLL) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                SGMII_1000BASEX_REG_PCS_SGMII_CONTROL);
                seq_printf(m, FMT_X16_ADAPTER, data);
            }
            else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");

        seq_printf(m, "PCS SGMII dev config\t(0x%04x):  ",
                   SGMII_1000BASEX_REG_PCS_SGMII_DEV_CONFIG);
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            if (adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_EXT_TX_PLL ||
                adapter_id[i] == ADAPTER_ID_SGMII_1000BASEX_100BASEFX_EXT_TX_PLL) {
                data = flx_frs_read_adapter_reg(dp->port[i],
                                                SGMII_1000BASEX_REG_PCS_SGMII_DEV_CONFIG);
                seq_printf(m, FMT_X16_ADAPTER, data);
            }
            else {
                seq_printf(m, FMT_NA_ADAPTER);
            }
        }
        seq_printf(m, "\n");
    }

    return 0;
}

/**
 * Read IPO registers.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_ipo_regs(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    unsigned int next_entry = 0;
    unsigned int i;
    unsigned int j;
    int data;

    seq_printf(m, "\nIPO registers of device %i\t(REG):\t", dp->dev_num);
    for (i = 0; i < dp->num_of_ports; i++) {
        if (!dp->port[i])
            continue;
        seq_printf(m, "\tPORT%i", i);
    }
    seq_printf(m, "\n\n");

    for (j = next_entry; j < 16; j++) {
        seq_printf(m, "ETH_ADDR_CFG(%2i)\t\t(0x%04x):",
                   j, PORT_REG_ETH_ADDR_CFG(j));
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i],
                                         PORT_REG_ETH_ADDR_CFG(j));
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");

        seq_printf(m, "PORT_REG_ETH_ADDR_FWD_ALLOW\t(0x%04x):",
                   PORT_REG_ETH_ADDR_FWD_ALLOW(j));
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i],
                                         PORT_REG_ETH_ADDR_FWD_ALLOW(j));
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");

        seq_printf(m, "PORT_REG_ETH_ADDR_FWD_MIRROR\t(0x%04x):",
                   PORT_REG_ETH_ADDR_FWD_MIRROR(j));
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i],
                                         PORT_REG_ETH_ADDR_FWD_MIRROR(j));
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");

        if (dp->type == FLX_FRS_TYPE_DS) {
            seq_printf(m, "PORT_REG_ETH_ADDR_POLICER\t(0x%04x):",
                       PORT_REG_ETH_ADDR_POLICER(j));
            for (i = 0; i < dp->num_of_ports; i++) {
                if (!dp->port[i])
                    continue;
                data = flx_frs_read_port_reg(dp->port[i],
                                             PORT_REG_ETH_ADDR_POLICER(j));
                seq_printf(m, "\t0x%04x", data);
            }
            seq_printf(m, "\n");
        }

        seq_printf(m, "PORT_REG_ETH_ADDR_0\t\t(0x%04x):",
                   PORT_REG_ETH_ADDR_0(j));
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i],
                                         PORT_REG_ETH_ADDR_0(j));
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");

        seq_printf(m, "PORT_REG_ETH_ADDR_1\t\t(0x%04x):",
                   PORT_REG_ETH_ADDR_1(j));
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i],
                                         PORT_REG_ETH_ADDR_1(j));
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n");

        seq_printf(m, "PORT_REG_ETH_ADDR_2\t\t(0x%04x):",
                   PORT_REG_ETH_ADDR_2(j));
        for (i = 0; i < dp->num_of_ports; i++) {
            if (!dp->port[i])
                continue;
            data = flx_frs_read_port_reg(dp->port[i],
                                         PORT_REG_ETH_ADDR_2(j));
            seq_printf(m, "\t0x%04x", data);
        }
        seq_printf(m, "\n\n");

    }

    return 0;
}

/**
 * MAC table iterator function to show one MAC table entry.
 * @param dp Device data
 * @param entry FRS MAC table entry
 * @param arg Sequential file
 */
static void flx_frs_proc_show_mac_table_entry(struct flx_frs_dev_priv *dp,
                                              struct frs_mac_table_entry *entry,
                                              void *arg)
{
    struct seq_file *m = arg;
    const uint8_t *mac_addr = &entry->mac_address[0];
    const char *ifname = NULL;
    unsigned int i;

    for (i = 0; i < FLX_FRS_MAX_PORTS; i++) {
        struct flx_frs_port_priv *pp = dp->port[i];

        if (pp && pp->netdev && pp->netdev->ifindex == entry->ifindex) {
            ifname = pp->if_name;
            break;
        }
    }

    seq_printf(m, "%s\t%02x:%02x:%02x:%02x:%02x:%02x\n",
               ifname ? ifname : "",
               mac_addr[0] & 0xff, mac_addr[1] & 0xff,
               mac_addr[2] & 0xff, mac_addr[3] & 0xff,
               mac_addr[4] & 0xff, mac_addr[5] & 0xff);
}

/**
 * Print FRS MAC table.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_mac_table(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;

    seq_printf(m, "MAC table of device %i:\n", dp->dev_num);
    seq_printf(m, "PORT\tMAC address\n");

    flx_frs_get_mac_table(dp, &flx_frs_proc_show_mac_table_entry, m);

    seq_printf(m, "\n");

    return 0;
}

/**
 * Helper function to wait for FRS to complete fetching next SMAC tables.
 * @param dp FRS device privates.
 * @return Register SMAC_CMD value when finished, negative on error.
 */
static inline int flx_frs_wait_smac_table_transfer(struct flx_frs_dev_priv *dp)
{
    int ret;
    unsigned int timeout = 1000;

    for (;;) {
        if (timeout-- == 0) {
            dev_err(dp->this_dev, "MAC table transfer timeout\n");
            break;
        }

        ret = flx_frs_read_switch_reg(dp, FRS_REG_SMAC_CMD);
        if (ret < 0)
            return ret;
        if (!(ret & FRS_SMAC_CMD_TRANSFER))
            return ret;
    }

    return -EBUSY;
}

/**
 * Helper function to read one DS SMAC table entry to registers.
 * @param dp FRS device privates.
 * @param row Row in the SMAC table.
 * @param col Column in the SMAC table.
 * @return Zero on success, negative on error.
 */
static int flx_frs_read_smac_entry(struct flx_frs_dev_priv *dp,
                                   uint16_t row, uint16_t col)
{
    int ret;
    uint16_t cmd;

    if (row > FLX_FRS_SMAC_TABLE_ROWS || col > FLX_FRS_SMAC_TABLE_COLS) {
        dev_err(dp->this_dev, "SMAC entry out of bounds: %i, %i (row, col)\n",
                row, col);
        return -1;
    }

    cmd = FRS_SMAC_CMD_TRANSFER
        | (row << FRS_SMAC_CMD_ROW_SHIFT)
        | (col << FRS_SMAC_CMD_COLUMN_SHIFT);
    ret = flx_frs_write_switch_reg(dp, FRS_REG_SMAC_CMD, cmd);

    ret = flx_frs_wait_smac_table_transfer(dp);
    if (ret < 0)
        return ret;

    return 0;
}

/**
 * Print FRS SMAC table.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_smac_table(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    uint16_t port, config, fwdmask, vlan;
    uint8_t mac_addr[ETH_ALEN] = { 0 };

    int ret, i;
    uint16_t row, col;

    seq_printf(m, "MAC table of device %i:\n", dp->dev_num);
    // mac address   policed port   config reg   forward mask   VLAN
    seq_printf(m, "MAC address\t\tPORT\tCONFIG\tFWDMASK\tVLAN\n");

    mutex_lock(&dp->smac_table_lock);

    for (row = 0; row < FLX_FRS_SMAC_TABLE_ROWS; row++) {
        for (col = 0; col < FLX_FRS_SMAC_TABLE_COLS; col++) {

            ret = flx_frs_read_smac_entry(dp, row, col);
            if (ret < 0)
                continue;

            for (i = 0; i < 3; i++) {
                ret = flx_frs_read_switch_reg(dp, FRS_REG_SMAC_ADDR(i));
                if (ret < 0)
                    return ret;
                mac_addr[i*2 + 0] = (ret >> 0) & 0xff;
                mac_addr[i*2 + 1] = (ret >> 8) & 0xff;
            }
            port = flx_frs_read_switch_reg(dp, FRS_REG_SMAC_PORT);
            config = flx_frs_read_switch_reg(dp, FRS_REG_SMAC_CONFIG);
            fwdmask = flx_frs_read_switch_reg(dp, FRS_REG_SMAC_FWDMASK);
            vlan = flx_frs_read_switch_reg(dp, FRS_REG_SMAC_VLAN);

            seq_printf(m, "%02x:%02x:%02x:%02x:%02x:%02x"
                       "\t%i\t0x%04x\t0x%04x\t0x%04x\n",
                       mac_addr[0] & 0xff, mac_addr[1] & 0xff,
                       mac_addr[2] & 0xff, mac_addr[3] & 0xff,
                       mac_addr[4] & 0xff, mac_addr[5] & 0xff,
                       port, config, fwdmask, vlan);
        }
    }

    mutex_unlock(&dp->smac_table_lock);

    return 0;
}

/**
 * Print driver runtime port status.
 * This is not for register values.
 * @param m Sequential file
 * @param v Iterator
 * @return 0
 */
static int flx_frs_proc_show_port_status(struct seq_file *m, void *v)
{
    struct flx_frs_dev_priv *dp = m->private;
    struct flx_frs_port_priv *port = NULL;
    unsigned int i;

    seq_printf(m, "\nPort status of device %2i:", dp->dev_num);
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "%10s%2i", "PORT", i);
    }
    seq_printf(m, "\n\n");

    seq_printf(m, "Name\t\t\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "%12s", port->if_name);
    }
    seq_printf(m, "\n");

    seq_printf(m, "SFP EEPROM access\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str = "-";
        port = dp->port[i];
        if (!port)
            continue;
        if (port->medium_type == FLX_FRS_MEDIUM_SFP) {
            if (port->flags & FLX_FRS_SFP_EEPROM)
                str = "YES";
            else
                str = "NO";
        }
        seq_printf(m, "%12s", str);
    }
    seq_printf(m, "\n");

    seq_printf(m, "SFP type\t\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str = "-";
        port = dp->port[i];
        if (!port)
            continue;
        if (port->medium_type == FLX_FRS_MEDIUM_SFP) {
            str = flx_frs_sfp_type_str(port->sfp.type);
        }
        seq_printf(m, "%12s", str);
    }
    seq_printf(m, "\n");

    seq_printf(m, "SFP interface\t\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str = "-";
        port = dp->port[i];
        if (!port)
            continue;
        if (port->medium_type == FLX_FRS_MEDIUM_SFP) {
            if (port->flags & FLX_FRS_HAS_SEPARATE_SFP)
                str = "separate";
            else
                str = "same";
        }
        else {
            str = "-";
        }
        seq_printf(m, "%12s", str);
    }
    seq_printf(m, "\n");

    seq_printf(m, "PHY\t\t\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str = NULL;
        port = dp->port[i];
        if (!port)
            continue;
        switch (port->medium_type) {
        case FLX_FRS_MEDIUM_SFP:
        case FLX_FRS_MEDIUM_PHY:
            if (port->ext_phy.phydev)
                str = "YES";
            else
                str = "NO";
            break;
        default:
            str = "-";
        }
        seq_printf(m, "%12s", str);
    }
    seq_printf(m, "\n");

    seq_printf(m, "SFP PHY\t\t\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str = NULL;
        port = dp->port[i];
        if (!port)
            continue;
        switch (port->medium_type) {
        case FLX_FRS_MEDIUM_SFP:
            if (port->sfp.phy.phydev)
                str = "YES";
            else
                str = "NO";
            break;
        default:
            str = "-";
        }
        seq_printf(m, "%12s", str);
    }
    seq_printf(m, "\n");

    seq_printf(m, "Mode\t\t\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str = "switched";
        port = dp->port[i];
        if (!port)
            continue;
        if (port->flags & FLX_FRS_PORT_INDEPENDENT)
            str = "independent";
        seq_printf(m, "%12s", str);
    }
    seq_printf(m, "\n");

    seq_printf(m, "Forwarding\t\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        const char *str = NULL;
        port = dp->port[i];
        if (!port)
            continue;
        switch (port->fwd_state) {
        case FRS_PORT_FWD_STATE_DISABLED: str = "disabled"; break;
        case FRS_PORT_FWD_STATE_LEARNING: str = "learn"; break;
        case FRS_PORT_FWD_STATE_FORWARDING: str = "forward"; break;
        case FRS_PORT_FWD_STATE_AUTO: str = "auto"; break;
        default: str = "(invalid)";
        }
        seq_printf(m, "%12s", str);
    }
    seq_printf(m, "\n");

    seq_printf(m, "Management trailer\t:");
    for (i = 0; i < dp->num_of_ports; i++) {
        port = dp->port[i];
        if (!port)
            continue;
        seq_printf(m, "      0x%04x", port->port_mask);
    }
    seq_printf(m, "\n");

    return 0;
}

/**
 * Init proc for driver
 * @return 0 on success
 */
int flx_frs_proc_init_driver(void)
{
    proc_root_entry = proc_mkdir("driver/flx_frs", NULL);
    if (!proc_root_entry) {
        printk(KERN_WARNING DRV_NAME
               ": creating proc root dir entry failed\n");
        return -EFAULT;
    }

    return 0;
}

/**
 * Cleanup driver
 */
void flx_frs_proc_cleanup_driver(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    proc_remove(proc_root_entry);
#else
    remove_proc_entry("driver/flx_frs", NULL);
#endif
}

static int flx_frs_proc_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, &flx_frs_proc_show_stats, PDE_DATA(inode));
}

static int flx_frs_proc_port_regs_open(struct inode *inode,
                                       struct file *file)
{
    return single_open(file, &flx_frs_proc_show_port_regs,
                       PDE_DATA(inode));
}

static int flx_frs_proc_port_macsec_regs_open(struct inode *inode,
					      struct file *file)
{
    return single_open(file, &flx_frs_proc_show_port_macsec_regs,
                       PDE_DATA(inode));
}

static int flx_frs_proc_ipo_regs_open(struct inode *inode,
                                      struct file *file)
{
    return single_open(file, &flx_frs_proc_show_ipo_regs, PDE_DATA(inode));
}

static int flx_frs_proc_adapter_regs_open(struct inode *inode,
                                          struct file *file)
{
    return single_open(file, &flx_frs_proc_show_adapter_regs,
                       PDE_DATA(inode));
}

static int flx_frs_proc_common_regs_open(struct inode *inode,
                                         struct file *file)
{
    return single_open(file, &flx_frs_proc_show_common_regs,
                       PDE_DATA(inode));
}

static int flx_frs_proc_vlan_regs_open(struct inode *inode,
                                       struct file *file)
{
    return single_open(file, &flx_frs_proc_show_vlan_regs,
                       PDE_DATA(inode));
}

static int flx_frs_proc_mac_table_open(struct inode *inode,
                                       struct file *file)
{
    return single_open(file, &flx_frs_proc_show_mac_table,
                       PDE_DATA(inode));
}

static int flx_frs_proc_smac_table_open(struct inode *inode,
                                        struct file *file)
{
    return single_open(file, &flx_frs_proc_show_smac_table,
                       PDE_DATA(inode));
}

static int flx_frs_proc_port_status_open(struct inode *inode,
                                         struct file *file)
{
    return single_open(file, &flx_frs_proc_show_port_status,
                       PDE_DATA(inode));
}

static const struct file_operations flx_frs_proc_stats_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_stats_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_port_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_port_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_port_macsec_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_port_macsec_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_ipo_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_ipo_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_adapter_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_adapter_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_common_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_common_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_vlan_regs_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_vlan_regs_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_mac_table_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_mac_table_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_smac_table_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_smac_table_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

static const struct file_operations flx_frs_proc_port_status_fops = {
    .owner = THIS_MODULE,
    .open = &flx_frs_proc_port_status_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};

/**
 * Init proc device under driver
 * @param dp Device data
 * @return 0 on success
 */
int flx_frs_proc_init_device(struct flx_frs_dev_priv *dp)
{
    char buf[50];
    int device = dp->dev_num;

    sprintf(buf, "statistic_device%02i", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_stats_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    sprintf(buf, "device%02i_port_registers", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_port_regs_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    if (dp->type == FLX_FRS_TYPE_DS) {
	sprintf(buf, "device%02i_port_macsec_registers", device);
	if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
			       &flx_frs_proc_port_macsec_regs_fops, dp))) {
	    dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
	}
    }

    sprintf(buf, "device%02i_ipo_registers", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_ipo_regs_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    sprintf(buf, "device%02i_adapter_registers", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_adapter_regs_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    sprintf(buf, "device%02i_common_registers", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_common_regs_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    sprintf(buf, "device%02i_vlan_config_registers", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_vlan_regs_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    sprintf(buf, "device%02i_mac_table", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_mac_table_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    if (dp->type == FLX_FRS_TYPE_DS) {
        sprintf(buf, "device%02i_smac_table", device);
        if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                               &flx_frs_proc_smac_table_fops, dp))) {
            dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
        }
    }

    sprintf(buf, "device%02i_port_status", device);
    if (!(proc_create_data(buf, S_IFREG | S_IRUGO, proc_root_entry,
                           &flx_frs_proc_port_status_fops, dp))) {
        dev_dbg(dp->this_dev, "creating proc entry %s failed.\n", buf);
    }

    return 0;
}

/**
 * Cleanup device proc under driver dir.
 */
void flx_frs_proc_cleanup_device(struct flx_frs_dev_priv *dp)
{
    char buf[50];
    int device = dp->dev_num;

    sprintf(buf, "statistic_device%02i", device);
    remove_proc_entry(buf, proc_root_entry);
    sprintf(buf, "device%02i_port_registers", device);
    remove_proc_entry(buf, proc_root_entry);
    if (dp->type == FLX_FRS_TYPE_DS) {
        sprintf(buf, "device%02i_port_macsec_registers", device);
        remove_proc_entry(buf, proc_root_entry);
    }
    sprintf(buf, "device%02i_ipo_registers", device);
    remove_proc_entry(buf, proc_root_entry);
    sprintf(buf, "device%02i_adapter_registers", device);
    remove_proc_entry(buf, proc_root_entry);
    sprintf(buf, "device%02i_common_registers", device);
    remove_proc_entry(buf, proc_root_entry);
    sprintf(buf, "device%02i_vlan_config_registers", device);
    remove_proc_entry(buf, proc_root_entry);
    sprintf(buf, "device%02i_mac_table", device);
    remove_proc_entry(buf, proc_root_entry);
    if (dp->type == FLX_FRS_TYPE_DS) {
        sprintf(buf, "device%02i_smac_table", device);
        remove_proc_entry(buf, proc_root_entry);
    }
    sprintf(buf, "device%02i_port_status", device);
    remove_proc_entry(buf, proc_root_entry);
}
