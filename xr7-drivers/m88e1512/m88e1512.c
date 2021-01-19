/**
 * @file
 */

/*

   Marvell 88E1512 PHY Linux driver

   Copyright (C) 2015 Flexibilis Oy

   Based heavily on marvell.c, driver for Marvell PHYs:
       Author: Andy Fleming
       Copyright (c) 2004 Freescale Semiconductor, Inc.
       Copyright (c) 2013 Michael Stapelberg <michael@stapelberg.de>

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

/// Uncomment to enable debug messages and debug features
//#define DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/marvell_phy.h>
#include <linux/of.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

// They have the same PHY ID. MARVELL_PHY_ID_88E1510 appeared in Linux 3.11.
//#define MARVELL_PHY_ID_88E1512                  MARVELL_PHY_ID_88E1510
#define MARVELL_PHY_ID_88E1512                  0x01410dd0

#define MII_MARVELL_PHY_PAGE                    22

#define MII_M1011_IEVENT                        0x13
#define MII_M1011_IEVENT_CLEAR                  0x0000

#define MII_M1011_IMASK                         0x12
#define MII_M1011_IMASK_INIT                    0x6400
#define MII_M1011_IMASK_CLEAR                   0x0000

#define MII_88E1512_PHY_SCR                     0x10
#define MII_88E1512_PHY_SCR_AUTO_CROSS          0x0060
#define MII_88E1512_PHY_SCR_DOWNSHIFT           0x3800

#define MII_88E1121_PHY_MSCR_PAGE               2
#define MII_88E1121_PHY_MSCR_REG                21
#define MII_88E1121_PHY_MSCR_RX_DELAY           BIT(5)
#define MII_88E1121_PHY_MSCR_TX_DELAY           BIT(4)
#define MII_88E1121_PHY_MSCR_DELAY_MASK         (~(0x3 << 4))

#define MII_M1011_PHY_STATUS                    0x11
#define MII_M1011_PHY_STATUS_1000               0x8000
#define MII_M1011_PHY_STATUS_100                0x4000
#define MII_M1011_PHY_STATUS_SPD_MASK           0xc000
#define MII_M1011_PHY_STATUS_FULLDUPLEX         0x2000
#define MII_M1011_PHY_STATUS_RESOLVED           0x0800
#define MII_M1011_PHY_STATUS_LINK               0x0400

#define MII_88E1512_GEN_PAGE                    18
#define MII_88E1512_GCR1                        20
#define MII_88E1512_GCR1_RESET                  BIT(15)
#define MII_88E1512_GCR1_RETAIN                 (0x4 << 7)
#define MII_88E1512_GCR1_AMD_FIBER_100          BIT(6)
#define MII_88E1512_GCR1_PREF_FIRST             (0x0 << 4)
#define MII_88E1512_GCR1_PREF_COPPER            (0x1 << 4)
#define MII_88E1512_GCR1_PREF_FIBER             (0x2 << 4)
#define MII_88E1512_GCR1_MODE                   0x7
#define MII_88E1512_GCR1_RGMII_COPPER           0x0
#define MII_88E1512_GCR1_SGMII_COPPER           0x1
#define MII_88E1512_GCR1_RGMII_FIBER_1000       0x2
#define MII_88E1512_GCR1_RGMII_FIBER_100        0x3
#define MII_88E1512_GCR1_RGMII_SGMII            0x4
#define MII_88E1512_GCR1_RGMII_COPPER_SGMII     0x6
#define MII_88E1512_GCR1_RGMII_COPPER_FIBER     0x7
#define MII_88E1512_GCR1_RGMII_AMD_COPPER_SGMII \
    (MII_88E1512_GCR1_RGMII_COPPER_SGMII)
#define MII_88E1512_GCR1_RGMII_AMD_COPPER_1000BASEX \
    (MII_88E1512_GCR1_RGMII_COPPER_FIBER)
#define MII_88E1512_GCR1_RGMII_AMD_COPPER_100BASEFX \
    (MII_88E1512_GCR1_AMD_FIBER_100 | MII_88E1512_GCR1_RGMII_FIBER_100)

/* LED Timer Control Register */
#define MII_88E1512_PHY_LED_PAGE                0x03
#define MII_88E1512_PHY_LED_CTRL                16
#define MII_88E1512_PHY_LED_DEF                 0x0066
#define MII_88E1512_PHY_LED_PCR                 0x11
#define MII_88E1512_PHY_LED_PCR_MASK            0x3
#define MII_88E1512_PHY_LED_PCR_LED0_SHIFT      0
#define MII_88E1512_PHY_LED_PCR_LED1_SHIFT      2
#define MII_88E1512_PHY_LED_PCR_LED2_SHIFT      4
#define MII_88E1512_PHY_LED_PCR_ACT_HIGH        0x1
#define MII_88E1512_PHY_LED_PCR_ACT_LOW         0x0
#define MII_88E1512_PHY_LED_PCR_ACT_LOW_OC      0x2
#define MII_88E1512_PHY_LED_PCR_ACT_HIGH_OC     0x3
#define MII_88E1512_PHY_LED_TCR                 0x12
#define MII_88E1512_PHY_LED_TCR_FORCE_INT       BIT(15)
#define MII_88E1512_PHY_LED_TCR_INTn_ENABLE     BIT(7)
#define MII_88E1512_PHY_LED_TCR_INT_ACTIVE_LOW  BIT(11)

#define MII_88E1512_COPPER_PAGE                 0
#define MII_88E1512_FIBER_PAGE                  1

// struct phy_device field lp_advertising appeared in Linux 3.14.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
# define PHYDEV_HAS_LP_ADVERTISING
#endif

MODULE_DESCRIPTION("Marvell 88E1512 PHY driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");

static int ignore_mode_check = 0;
module_param(ignore_mode_check, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ignore_mode_check,
                 "Ignore MODE value at probe to skip check for 88E1512 chip");

static int disable_sgmii = 0;
module_param(disable_sgmii, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(disable_sgmii, "Do not use SGMII interface");

static int disable_if_port = 0;
module_param(disable_if_port, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(disable_if_port, "Do not use net device if_port");

#define MARVELL_LOOPBACK_COPPER 1
#define MARVELL_LOOPBACK_FIBER 2
#define MARVELL_LOOPBACK_COPPER_ANEG 3

#define MARVELL_VCT 1
#define MARVELL_ALT_VCT 2

// Debugging features as module parameters: loopback and VCT.
#ifdef DEBUG

static int loopback = 0;
module_param(loopback, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(loopback,
                 "Loopback test:"
                 " 0:disabled 1:copper 2:fiber"
                 " 3:copper repeat autoneg\n");

static int vct = 0;
module_param(vct, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(vct,
                 "Virtual Cable Test (VCT):"
                 " 0:disabled 1:orignal 2:alternative\n");

#else

static const int loopback = 0;
static const int vct = 0;

#endif

/**
 * Available interfaces on 88E1512.
 */
enum m88e1512_interface {
    M88E1512_IF_NONE,           ///< no interface is currently active (UP)
    M88E1512_IF_COPPER,         ///< PHY copper interface active
    M88E1512_IF_1000BASEX,      ///< PHY 1000Base fiber interface active
    M88E1512_IF_100BASEFX,      ///< PHY 100Base-FX fiber interface
    M88E1512_IF_SGMII,          ///< PHY SGMII interface active
};

/**
 * Fiber interface to use.
 */
enum m88e1512_fiber {
    M88E1512_FIBER_IF_1000,     ///< fiber is 1000Base-X
    M88E1512_FIBER_IF_100,      ///< fiber is 100Base-FX
    M88E1512_FIBER_IF_SGMII,    ///< SGMII
};

/**
 * 88E1512 PHY device private context
 */
struct m88e1512_dev_priv {
    enum m88e1512_fiber fiber_if;       ///< fiber interface to use
    enum m88e1512_interface force_if;   ///< current forced interface
                                        ///< (none = auto-media-detect)
    enum m88e1512_interface current_if; ///< current interface
    uint32_t lp_advertising;            ///< link partner advertising flags
    struct dentry *debug_dir;   ///< device debugfs directory
    struct dentry *reg_dump;    ///< debugfs register dump file
};

/**
 * 88E1512 PHY driver context
 */
struct m88e1512_drv_priv {
    struct dentry *debug_dir;   ///< debugfs directory
};

/// Driver privates
static struct m88e1512_drv_priv drv_priv = {
    .debug_dir = NULL,
};

/**
 * Interface name strings.
 */
static const char *m88e1512_interfaces[] = {
    [M88E1512_IF_NONE] = "none",
    [M88E1512_IF_COPPER] = "copper",
    [M88E1512_IF_1000BASEX] = "1000Base-X",
    [M88E1512_IF_100BASEFX] = "100Base-FX",
    [M88E1512_IF_SGMII] = "SGMII",
};

static void m88e1512_set_interface(struct phy_device *phydev,
                                   enum m88e1512_interface interface)
{
    struct m88e1512_dev_priv *dp = phydev->priv;

    if (disable_sgmii && interface == M88E1512_IF_SGMII) {
        // Prefer 1000Base-X.
        if (phydev->supported &
            (SUPPORTED_1000baseT_Full | SUPPORTED_1000baseT_Half)) {
            interface = M88E1512_IF_1000BASEX;
        }
        else if (phydev->supported &
            (SUPPORTED_100baseT_Full | SUPPORTED_100baseT_Half)) {
            interface = M88E1512_IF_100BASEFX;
        }
        else {
            interface = M88E1512_IF_1000BASEX;
        }
    }

    if (interface != dp->current_if) {
        dev_dbg(&phydev->dev,
                "Change interface from %s to %s supported 0x%x\n",
                m88e1512_interfaces[dp->current_if],
                m88e1512_interfaces[interface],
                phydev->supported);
        dp->current_if = interface;
    }
}

static const char *m88e1512_get_interface_str(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;

    return m88e1512_interfaces[dp->current_if];
}

static int m88e1512_set_led(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;
    int err;
    uint16_t led0, led1;

    switch (dp->current_if) {
    case M88E1512_IF_NONE:
        // Force both off
        led0 = 0x8;
        led1 = 0x8;
        break;
    case M88E1512_IF_COPPER:
        // on=link blink=act off=no-link
        led0 = 0x1;
        // Force off
        led1 = 0x8;
        break;
    default:
        // Fiber or SGMII
        // Force off
        led0 = 0x8;
        // on=link blink=act off=no-link
        led1 = 0x1;
    }

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_PHY_LED_PAGE);
    if (err < 0)
        goto out;

    err = phy_read(phydev, MII_88E1512_PHY_LED_CTRL);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_88E1512_PHY_LED_CTRL,
                    (err & ~0xff) | (led1 << 4) | led0);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}

static int m88e1512_ack_interrupt(struct phy_device *phydev)
{
    int err, oldpage;

    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0)
        return oldpage;
    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_COPPER_PAGE);
    if (err < 0)
        goto out;

    /* Clear the interrupts by reading the reg */
    err = phy_read(phydev, MII_M1011_IEVENT);
    if (err < 0)
        goto out;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_FIBER_PAGE);
    if (err < 0)
        goto out;

    /* Clear the interrupts by reading the reg */
    err = phy_read(phydev, MII_M1011_IEVENT);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0)
        return err;

    return 0;
}

static int m88e1512_config_intr(struct phy_device *phydev)
{
    int err, oldpage;

    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0)
        return oldpage;
    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_COPPER_PAGE);
    if (err < 0)
        goto out;

    if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
        err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_INIT);
    else
        err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_CLEAR);
    if (err)
        goto out;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_FIBER_PAGE);
    if (err < 0)
        goto out;

    if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
        err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_INIT);
    else
        err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_CLEAR);
    if (err)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0)
        return err;

    return err;
}

#ifdef CONFIG_OF_MDIO
/*
 * Set and/or override some configuration registers based on the
 * marvell,reg-init property stored in the of_node for the phydev.
 *
 * marvell,reg-init = <reg-page reg mask value>,...;
 *
 * There may be one or more sets of <reg-page reg mask value>:
 *
 * reg-page: which register bank to use.
 * reg: the register.
 * mask: if non-zero, ANDed with existing register value.
 * value: ORed with the masked value and written to the regiser.
 *
 */
static int marvell_of_reg_init(struct phy_device *phydev)
{
    const __be32 *paddr;
    int len, i, saved_page, current_page, page_changed, ret;

    if (!phydev->dev.of_node)
        return 0;

    paddr = of_get_property(phydev->dev.of_node, "marvell,reg-init", &len);
    if (!paddr || len < (4 * sizeof(*paddr)))
        return 0;

    saved_page = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (saved_page < 0)
        return saved_page;
    page_changed = 0;
    current_page = saved_page;

    ret = 0;
    len /= sizeof(*paddr);
    for (i = 0; i < len - 3; i += 4) {
        u16 reg_page = be32_to_cpup(paddr + i);
        u16 reg = be32_to_cpup(paddr + i + 1);
        u16 mask = be32_to_cpup(paddr + i + 2);
        u16 val_bits = be32_to_cpup(paddr + i + 3);
        int val;

        if (reg_page != current_page) {
            current_page = reg_page;
            page_changed = 1;
            ret = phy_write(phydev, MII_MARVELL_PHY_PAGE, reg_page);
            if (ret < 0)
                goto err;
        }

        val = 0;
        if (mask) {
            val = phy_read(phydev, reg);
            if (val < 0) {
                ret = val;
                goto err;
            }
            val &= mask;
        }
        val |= val_bits;

        ret = phy_write(phydev, reg, val);
        if (ret < 0)
            goto err;

    }

err:
    if (page_changed) {
        i = phy_write(phydev, MII_MARVELL_PHY_PAGE, saved_page);
        if (ret == 0)
            ret = i;
    }
    return ret;
}
#else
static int marvell_of_reg_init(struct phy_device *phydev)
{
    return 0;
}
#endif /* CONFIG_OF_MDIO */

/**
 * Errata 3.1: EEE.
 * Call once after hw reset.
 */
static int m88e1512_errata_3_1(struct phy_device *phydev)
{
    int err;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xff);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 17, 0x214b);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 16, 0x2144);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 17, 0x0c28);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 16, 0x2146);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 17, 0xb233);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 16, 0x214d);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 17, 0xcc0c);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 16, 0x2159);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}

/**
 * Errata 3.3: 1000Base-X autoneg.
 * Call after each mode change.
 */
static int m88e1512_errata_3_3(struct phy_device *phydev)
{
    int err;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_FIBER_PAGE);
    if (err < 0)
        goto out;
    err = phy_read(phydev, 0x0060);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}

/**
 * Errata 4.4: non IEEE compliant link partners.
 * Call after hw reset.
 */
static int m88e1512_errata_4_4(struct phy_device *phydev)
{
    int err;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfc);
    if (err < 0)
        goto out;
    err = phy_read(phydev, 1);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 1, err | (1 << 15));
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}

/**
 * Errata 4.7: LED[2] as active low open-drain interrupt output.
 */
static int m88e1512_errata_4_7(struct phy_device *phydev)
{
    int err;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_PHY_LED_PAGE);
    if (err < 0)
        goto out;
    err = phy_read(phydev, MII_88E1512_PHY_LED_PCR);
    if (err < 0)
        goto out;
    err &= ~(MII_88E1512_PHY_LED_PCR_MASK <<
             MII_88E1512_PHY_LED_PCR_LED2_SHIFT);
    err |= MII_88E1512_PHY_LED_PCR_ACT_LOW_OC <<
        MII_88E1512_PHY_LED_PCR_LED2_SHIFT;
    err = phy_write(phydev, MII_88E1512_PHY_LED_PCR, err);
    if (err < 0)
        goto out;
    err = phy_read(phydev, MII_88E1512_PHY_LED_TCR);
    if (err < 0)
        goto out;
    err |= MII_88E1512_PHY_LED_TCR_INTn_ENABLE;
    err = phy_write(phydev, MII_88E1512_PHY_LED_TCR, err);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}

#if 0
/**
 * Errata 4.18: RGMII RX_CLK improvement.
 * For modes 010 and 100.
 * Call once after hw reset.
 */
static int m88e1512_errata_4_18_fiber(struct phy_device *phydev)
{
    int err;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x12);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 20, 0x820);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 9, 0x1f00);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 0, 0x9140);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfa);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 7, 0x20a);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 25, 0x80ff);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 26, 0x80ff);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfb);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 6, 0x8f);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfc);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 11, 0x39);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}
#endif

/**
 * Errata 4.18: RGMII RX_CLK improvement.
 * For modes 110 and 111.
 * Call when link comes up.
 */
static int m88e1512_errata_4_18_amd_up(struct phy_device *phydev)
{
    int err;

    // Original version is exactly the same as in m88e1512_errata_4_18_fiber.

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x12);
    if (err < 0)
        goto out;
    // Modification: retain preferred media and fiber type and mode.
#if 0
    err = phy_write(phydev, 20, 0x820);
#else
    err = phy_read(phydev, 20);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 20, (err & 0x77) | 0x800);
#endif
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;
    // Modification: retain 1000Base-T advertisements.
#if 0
    err = phy_write(phydev, 9, 0x1f00);
#else
    err = phy_read(phydev, 9);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 9, (err | 0x1c00) & 0x1f00);
#endif
    if (err < 0)
        goto out;
    // Modification: retain autoneg/forced mode.
#if 0
    err = phy_write(phydev, 0, 0x9140);
#else
    err = phy_read(phydev, 0);
    if (err < 0)
        goto out;
    if (err & 0x1000)
        err = phy_write(phydev, 0, 0x9140);
    else
        err = phy_write(phydev, 0, err | 0x8000);
#endif
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfa);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 7, 0x20a);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 25, 0x80ff);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 26, 0x80ff);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfb);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 6, 0x8f);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfc);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 11, 0x39);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}

/**
 * Errata 4.18: RGMII RX_CLK improvement.
 * For modes 110 and 111.
 * Call when link comes down.
 */
static int m88e1512_errata_4_18_amd_down(struct phy_device *phydev)
{
    int err;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x12);
    if (err < 0)
        goto out;
    // Modification: retain preferred media and fiber type and mode.
#if 0
    err = phy_write(phydev, 20, 0x7);
#else
    err = phy_read(phydev, 20);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 20, err & 0x77);
#endif
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfa);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 7, 0x200);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 25, 0x0);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 26, 0x0);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfb);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 6, 0x0);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0xfc);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 11, 0x19);
    if (err < 0)
        goto out;
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    if (err < 0)
        goto out;
    // Modification: retain 1000Base-T advertisements.
#if 0
    err = phy_write(phydev, 9, 0x300);
#else
    err = phy_read(phydev, 9);
    if (err < 0)
        goto out;
    err = phy_write(phydev, 9, err & 0x0300);
#endif
    if (err < 0)
        goto out;
    // Modification: retain autoneg/forced mode.
#if 0
    err = phy_write(phydev, 0, 0x9140);
#else
    err = phy_read(phydev, 0);
    if (err < 0)
        goto out;
    if (err & 0x1000)
        err = phy_write(phydev, 0, 0x9140);
    else
        err = phy_write(phydev, 0, err | 0x8000);
#endif
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    return err;
}

/**
 * Print current interface from sysfs:
 * - 1000Base-X
 * - 100Base-FX
 * - SGMII
 * - copper
 * - none
 */
static ssize_t m88e1512_show_current_interface(struct device *dev,
                                               struct device_attribute *attr,
                                               char *buf)
{
    struct phy_device *phydev = to_phy_device(dev);
    ssize_t len = 0;

    len += sprintf(buf + len, "%s\n", m88e1512_get_interface_str(phydev));

    return len;
}

static DEVICE_ATTR(current_interface, 0444,
                   &m88e1512_show_current_interface,
                   NULL);

/**
 * Print current forced interface from sysfs:
 * - 1000Base-X
 * - 100Base-FX
 * - SGMII
 * - copper
 * - none
 */
static ssize_t m88e1512_show_force_interface(struct device *dev,
                                             struct device_attribute *attr,
                                             char *buf)
{
    struct phy_device *phydev = to_phy_device(dev);
    struct m88e1512_dev_priv *dp = phydev->priv;
    ssize_t len = 0;

    len += sprintf(buf + len, "%s\n", m88e1512_interfaces[dp->force_if]);

    return len;
}

/**
 * Read desired interface from sysfs:
 * - 1000Base-X
 * - 100Base-FX
 * - SGMII
 * - copper
 * - none
 */
static ssize_t m88e1512_store_force_interface(struct device *dev,
                                         struct device_attribute *attr,
                                         const char *buf,
                                         size_t count)
{
    struct phy_device *phydev = to_phy_device(dev);
    struct m88e1512_dev_priv *dp = phydev->priv;
    ssize_t len;
    const char *end;

    end = strnchr(buf, count, '\n');
    if (!end)
        end = buf + count;
    len = end - buf;

    if (strncmp(buf, m88e1512_interfaces[M88E1512_IF_NONE], len) == 0)
        dp->force_if = M88E1512_IF_NONE;
    else if (strncmp(buf, m88e1512_interfaces[M88E1512_IF_1000BASEX],
                     len) == 0)
        dp->force_if = M88E1512_IF_1000BASEX;
    else if (strncmp(buf, m88e1512_interfaces[M88E1512_IF_100BASEFX],
                     len) == 0)
        dp->force_if = M88E1512_IF_100BASEFX;
    else if (strncmp(buf, m88e1512_interfaces[M88E1512_IF_SGMII],
                     len) == 0)
        dp->force_if = M88E1512_IF_SGMII;
    else if (strncmp(buf, m88e1512_interfaces[M88E1512_IF_COPPER],
                     len) == 0)
        dp->force_if = M88E1512_IF_COPPER;
    else
        return -EINVAL;

    return count;
}

static DEVICE_ATTR(force_interface, 0644,
                   &m88e1512_show_force_interface,
                   &m88e1512_store_force_interface);

#ifdef CONFIG_DEBUG_FS
static void m88e1512_phy_start_machine(struct phy_device *phydev)
{
    // system_power_efficient_wq appeared in Linux 3.12.
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0)
    queue_delayed_work(system_wq, &phydev->state_queue, HZ);
#else
    queue_delayed_work(system_power_efficient_wq, &phydev->state_queue, HZ);
#endif
}

/**
 * phy_stop_machine - stop the PHY state machine tracking
 * @phydev: target phy_device struct
 *
 * Description: Stops the state machine timer, sets the state to UP
 *   (unless it wasn't up yet). This function must be called BEFORE
 *   phy_detach.
 */
static void m88e1512_phy_stop_machine(struct phy_device *phydev)
{
    cancel_delayed_work_sync(&phydev->state_queue);

    mutex_lock(&phydev->lock);
    if (phydev->state > PHY_UP)
        phydev->state = PHY_UP;
    mutex_unlock(&phydev->lock);
}

/**
 * Debugfs PHY register dump file for debugging.
 */
static int m88e1512_reg_dump_show(struct seq_file *m, void *v)
{
    struct phy_device *phydev = m->private;
    int oldpage;
    int ret;
    unsigned int page;
    unsigned int reg;
    const uint32_t page_regs[] = {
        [0] = 0x0fffffffu,
        [1] = 0x0fff81ffu,
        [2] = 0x0fff0000u,
        [3] = 0x000f0000u,
        [4] = 0x00100000u,
        [5] = 0x0fff0000u,
        [6] = 0x0fff0000u,
        [7] = 0x1fff0000u,
        [8] = 0x0000ff0fu,
        [9] = 0x000000ffu,
        [12] = 0x0000ffffu,
        [14] = 0x0000ff0fu,
        [17] = 0x0fff0000u,
        [18] = 0x0fff000fu,
    };

    m88e1512_phy_stop_machine(phydev);

    oldpage = phy_read(phydev, 22);
    if (oldpage < 0)
        goto out;

    seq_printf(m, "Page\tReg\tValue\n");
    for (page = 0; page < ARRAY_SIZE(page_regs); page++) {
        if (page_regs[page] == 0)
            continue;

        for (reg = 0; reg < 0x20; reg++) {
            if (!(page_regs[page] & (1u << reg)))
                continue;

            ret = phy_write(phydev, 22, page);
            if (ret == 0)
                ret = phy_read(phydev, reg);
            if (ret < 0)
                seq_printf(m, "%u\t%u\tERROR\n", page, reg);
            else
                seq_printf(m, "%u\t%u\t0x%04x\n", page, reg, ret);
        }
    }

    phy_write(phydev, 22, oldpage);

out:
    m88e1512_phy_start_machine(phydev);

    return 0;
}

static int m88e1512_reg_dump_open(struct inode *inode,
                                  struct file *file)
{
    return single_open(file, &m88e1512_reg_dump_show, inode->i_private);
}

static const struct file_operations m88e1512_reg_dump_fops = {
    .owner = THIS_MODULE,
    .open = &m88e1512_reg_dump_open,
    .read = &seq_read,
    .llseek = &seq_lseek,
    .release = &single_release,
};
#endif

static int m88e1512_probe(struct phy_device *phydev)
{
    struct m88e1512_drv_priv *drv = &drv_priv;
    struct m88e1512_dev_priv *dp = NULL;
    int err, oldpage;

    /*
     * 88E1512 has the same PHY ID as 88E15120/88E1518/88E1514.
     * Detect 88E1512 from different register.
     */
    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0)
        return oldpage;
    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    /* Errata 1 */
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 18);
    if (err < 0)
        goto reject;

    err = phy_read(phydev, 30);
    if (err < 0)
        goto reject;

    if (err == 0x0004) {
        /* Not 88E1512/88E1514 */
        err = -ENODEV;
        goto reject;
    }

    if (err == 0x0006) {
        /* 88E1512/88E1514 for sure */
        dev_dbg(&phydev->dev, "Device is 88E1512\n");
    }
    else {
        /* Possibly 88E1512 */
        err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                        MII_88E1512_GEN_PAGE);
        if (err < 0)
            goto reject;

        err = phy_read(phydev, MII_88E1512_GCR1);
        if (err < 0)
            goto reject;

        if ((err & MII_88E1512_GCR1_MODE) != 0x7) {
            dev_warn(&phydev->dev, "Device is not necessarily 88E1512\n");
            if (!ignore_mode_check) {
                err = -ENODEV;
                goto reject;
            }
        }
    }

    // It is a 88E1512.
    dp = kzalloc(sizeof(*dp), GFP_KERNEL);
    if (!dp) {
        dev_warn(&phydev->dev, "kmalloc failed\n");
        err = -ENOMEM;
        goto reject;
    }

#ifdef CONFIG_DEBUG_FS
    // Ignore debugfs errors.
    if (drv->debug_dir) {
        dp->debug_dir = debugfs_create_dir(dev_name(&phydev->dev),
                                           drv->debug_dir);
        if (IS_ERR(dp->debug_dir)) {
            dp->reg_dump = NULL;
        }
        else {
            dp->reg_dump = debugfs_create_file("reg_dump",
                                               S_IRUGO,
                                               dp->debug_dir,
                                               phydev,
                                               &m88e1512_reg_dump_fops);
            if (IS_ERR(dp->reg_dump))
                dp->reg_dump = NULL;
        }
    }
#endif

    phydev->priv = dp;

    // Configure LED polarity.
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_PHY_LED_PAGE);
    if (err < 0)
        goto fail;
    err = phy_read(phydev, MII_88E1512_PHY_LED_PCR);
    if (err < 0)
        goto fail;
    err &= ~(MII_88E1512_PHY_LED_PCR_MASK <<
             MII_88E1512_PHY_LED_PCR_LED0_SHIFT);
    err &= ~(MII_88E1512_PHY_LED_PCR_MASK <<
             MII_88E1512_PHY_LED_PCR_LED1_SHIFT);
    err |= (MII_88E1512_PHY_LED_PCR_ACT_HIGH <<
            MII_88E1512_PHY_LED_PCR_LED0_SHIFT);
    err |= (MII_88E1512_PHY_LED_PCR_ACT_HIGH <<
            MII_88E1512_PHY_LED_PCR_LED1_SHIFT);
    err = phy_write(phydev, MII_88E1512_PHY_LED_PCR, err);
    if (err < 0)
        goto fail;

#if 1
    err = m88e1512_errata_3_1(phydev);
    if (err < 0)
        goto fail;
#endif

    err = m88e1512_errata_4_4(phydev);
    if (err < 0)
        goto fail;

    err = m88e1512_errata_4_7(phydev);
    if (err < 0)
        goto fail;

    m88e1512_set_interface(phydev, M88E1512_IF_NONE);

    err = m88e1512_set_led(phydev);
    if (err)
        goto fail;

    err = device_create_file(&phydev->dev, &dev_attr_current_interface);
    if (err)
        goto fail;

    err = device_create_file(&phydev->dev, &dev_attr_force_interface);
    if (err)
        goto fail2;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0)
        goto fail3;

    dev_dbg(&phydev->dev, "%s() link %s speed %i %s irq %i\n",
            __func__,
            phydev->link ? "UP" : "DOWN",
            phydev->speed,
            phydev->duplex == DUPLEX_FULL ? "full-duplex" : "half-duplex",
            phydev->irq);

    return 0;

fail3:
    device_remove_file(&phydev->dev, &dev_attr_force_interface);

fail2:
    device_remove_file(&phydev->dev, &dev_attr_current_interface);

fail:
    phydev->priv = NULL;

    if (dp->reg_dump) {
        debugfs_remove(dp->reg_dump);
        dp->reg_dump = NULL;
    }
    if (dp->debug_dir) {
        debugfs_remove(dp->debug_dir);
        dp->debug_dir = NULL;
    }
    kfree(dp);

reject:
    phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);

    dev_err(&phydev->dev, "%s() failed\n", __func__);

    return err;
}

static void m88e1512_remove(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;
    int err, oldpage;

    device_remove_file(&phydev->dev, &dev_attr_force_interface);
    device_remove_file(&phydev->dev, &dev_attr_current_interface);

    if (dp->reg_dump) {
        debugfs_remove(dp->reg_dump);
        dp->reg_dump = NULL;
    }
    if (dp->debug_dir) {
        debugfs_remove(dp->debug_dir);
        dp->debug_dir = NULL;
    }
    phydev->priv = NULL;
    kfree(dp);

    /*
     * Write 88E1512 specific value to a register so that next probe
     * can detect the chip correctly.
     */
    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0)
        return;
    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_GEN_PAGE);
    if (err < 0)
        goto out;

    err = phy_read(phydev, MII_88E1512_GCR1);
    if (err < 0)
        goto out;

    err = phy_write(phydev, MII_88E1512_GCR1, err | 0x7);
    if (err < 0)
        goto out;

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0)
        return;

    return;
}

/**
 * Try to choose correct interface automatically based on information
 * from higher level. Prefer fiber.
 * - FIBRE 1000/full only: auto-media-detect copper/1000Base-X
 * - FIBRE 100/full only:  auto-media-detect copper/100Base-FX
 * - no FIBRE: auto-media-detect copper/SGMII
 * - default: auto-media-detect copper/1000Base-X or copper/100Base-FX
 *   depending on gigabit speed supported flag
 */
static int m88e1512_gcr1(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;
    int gcr1 = 0;
    int if_port = IF_PORT_UNKNOWN;

    switch (dp->force_if) {
    case M88E1512_IF_NONE:
        // not forced
        break;
    case M88E1512_IF_COPPER:
        return MII_88E1512_GCR1_RGMII_COPPER;
    case M88E1512_IF_1000BASEX:
        return MII_88E1512_GCR1_RGMII_FIBER_1000;
    case M88E1512_IF_100BASEFX:
        return MII_88E1512_GCR1_RGMII_FIBER_100;
    case M88E1512_IF_SGMII:
        return MII_88E1512_GCR1_RGMII_SGMII;
    }

    if (!disable_if_port && phydev->attached_dev)
        if_port = phydev->attached_dev->if_port;

    if (phydev->supported & SUPPORTED_FIBRE) {
        if (if_port == IF_PORT_100BASEFX) {
            dp->fiber_if = M88E1512_FIBER_IF_100;
            gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_100BASEFX;
        }
        else if (phydev->supported &
                 (SUPPORTED_1000baseT_Full | SUPPORTED_1000baseT_Half)) {
            dp->fiber_if = M88E1512_FIBER_IF_1000;
            gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_1000BASEX;
        }
        else if (phydev->supported &
                 (SUPPORTED_100baseT_Full | SUPPORTED_100baseT_Half)) {
            dp->fiber_if = M88E1512_FIBER_IF_100;
            gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_100BASEFX;
        }
        else if (disable_sgmii) {
            dp->fiber_if = M88E1512_FIBER_IF_1000;
            gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_1000BASEX;
        }
        else {
            dp->fiber_if = M88E1512_FIBER_IF_SGMII;
            gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_SGMII;
        }
    }
    else {
        // Gigabit copper-SFP modules use SGMII,
        // 100Base-TX copper-SFP modules appear as being 100Base-FX.
        if (disable_sgmii) {
            if (if_port == IF_PORT_100BASET || if_port == IF_PORT_100BASETX) {
                dp->fiber_if = M88E1512_FIBER_IF_100;
                gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_100BASEFX;
            }
            if (phydev->supported &
                (SUPPORTED_1000baseT_Full | SUPPORTED_1000baseT_Half)) {
                dp->fiber_if = M88E1512_FIBER_IF_1000;
                gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_1000BASEX;
            }
            else if (phydev->supported &
                     (SUPPORTED_100baseT_Full | SUPPORTED_100baseT_Half)) {
                dp->fiber_if = M88E1512_FIBER_IF_100;
                gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_100BASEFX;
            }
            else {
                dp->fiber_if = M88E1512_FIBER_IF_1000;
                gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_1000BASEX;
            }
        }
        else {
            dp->fiber_if = M88E1512_FIBER_IF_SGMII;
            gcr1 = MII_88E1512_GCR1_RGMII_AMD_COPPER_SGMII;
        }
    }

    gcr1 |= MII_88E1512_GCR1_PREF_FIBER;

    return gcr1;
}

static int m88e1512_config_aneg(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;
    int err, oldpage, gcr1, bmcr, adv;

    dev_dbg(&phydev->dev, "%s() %s speed %i %s supported 0x%x\n",
            __func__,
            phydev->autoneg == AUTONEG_ENABLE ? "autoneg" : "forced",
            phydev->speed,
            phydev->duplex == DUPLEX_FULL ? "full-duplex" : "half-duplex",
            phydev->supported);

    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0)
        return oldpage;
    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_GEN_PAGE);
    if (err < 0)
        goto out;

    if (loopback == MARVELL_LOOPBACK_COPPER) {
        dev_info(&phydev->dev, "Copper loopback\n");
        gcr1 = MII_88E1512_GCR1_RGMII_COPPER;
        gcr1 |= MII_88E1512_GCR1_PREF_COPPER;
    }
    else if (loopback == MARVELL_LOOPBACK_FIBER) {
        dev_info(&phydev->dev, "Fiber loopback\n");
        gcr1 = MII_88E1512_GCR1_RGMII_FIBER_1000;
        gcr1 |= MII_88E1512_GCR1_PREF_FIBER;
    }
    else {
        gcr1 = m88e1512_gcr1(phydev);
    }
    gcr1 |= MII_88E1512_GCR1_RETAIN;
    dev_dbg(&phydev->dev, "Supported 0x%x GCR1 0x%x\n",
            phydev->supported, gcr1);
    err = phy_write(phydev, MII_88E1512_GCR1, gcr1);
    if (err < 0)
        goto out;

    err = phy_write(phydev, MII_88E1512_GCR1,
                    gcr1 | MII_88E1512_GCR1_RESET);
    if (err < 0)
        goto out;

    err = m88e1512_errata_3_3(phydev);
    if (err < 0)
        goto out;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1121_PHY_MSCR_PAGE);
    if (err < 0)
        goto out;

    if ((phydev->interface == PHY_INTERFACE_MODE_RGMII) ||
        (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) ||
        (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) ||
        (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)) {

        int mscr;

        mscr = phy_read(phydev, MII_88E1121_PHY_MSCR_REG) &
            MII_88E1121_PHY_MSCR_DELAY_MASK;
        if (mscr < 0)
            goto out;

        if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
            mscr |= (MII_88E1121_PHY_MSCR_RX_DELAY |
                     MII_88E1121_PHY_MSCR_TX_DELAY);
        else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
            mscr |= MII_88E1121_PHY_MSCR_RX_DELAY;
        else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
            mscr |= MII_88E1121_PHY_MSCR_TX_DELAY;

        err = phy_write(phydev, MII_88E1121_PHY_MSCR_REG, mscr);
        if (err < 0)
            goto out;
    }

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_COPPER_PAGE);
    if (err < 0)
        goto out;

    err = phy_write(phydev, MII_BMCR, BMCR_RESET);
    if (err < 0)
        goto out;

    // Downshift causes link to often get up at 100 Mb/s instead of 1000 Mb/s.
    err = phy_write(phydev, MII_88E1512_PHY_SCR,
                    MII_88E1512_PHY_SCR_AUTO_CROSS |
                    0); //MII_88E1512_PHY_SCR_DOWNSHIFT);
    if (err < 0)
        goto out;

    phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_PHY_LED_PAGE);
    phy_write(phydev, MII_88E1512_PHY_LED_CTRL, MII_88E1512_PHY_LED_DEF);
    phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1512_COPPER_PAGE);

    err = genphy_config_aneg(phydev);
    if (err < 0)
        goto out;

    bmcr = phy_read(phydev, MII_BMCR);
    if (bmcr < 0)
        goto out;

    if (loopback == MARVELL_LOOPBACK_COPPER) {
        phy_write(phydev, MII_MARVELL_PHY_PAGE, 6);
        phy_write(phydev, 16, phy_read(phydev, 16) | (1 << 4));
        phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
        bmcr &= ~BMCR_SPEED100;
        bmcr |= BMCR_SPEED1000 | BMCR_FULLDPLX;
        phy_write(phydev, MII_BMCR, bmcr);
        bmcr |= BMCR_LOOPBACK;
    }
    else {
        bmcr |= BMCR_RESET;
    }
    err = phy_write(phydev, MII_BMCR, bmcr);
    if (err < 0)
        goto out;
    dev_dbg(&phydev->dev, "Copper RESET page %i BMCR 0x%x verify 0x%x\n",
            phy_read(phydev, MII_MARVELL_PHY_PAGE),
            bmcr,
            phy_read(phydev, MII_BMCR));

    // Setup fiber autonegotiation.
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_FIBER_PAGE);
    if (err < 0)
        goto out;

    bmcr = phy_read(phydev, MII_BMCR);
    if (bmcr < 0)
        goto out;

    if (phydev->autoneg == AUTONEG_ENABLE) {
        adv = 0;
        // Only for 1000Base-X.
        if (dp->fiber_if == M88E1512_FIBER_IF_1000) {
            adv = phy_read(phydev, MII_ADVERTISE);
            if (adv < 0)
                goto out;
            if (phydev->advertising & ADVERTISED_1000baseT_Full)
                adv |= ADVERTISE_1000XFULL;
            else
                adv &= ~ADVERTISE_1000XFULL;
            if (phydev->advertising & ADVERTISED_1000baseT_Half)
                adv |= ADVERTISE_1000XHALF;
            else
                adv &= ~ADVERTISE_1000XHALF;
        }
        bmcr &= ~BMCR_ANRESTART;
        bmcr |= BMCR_ANENABLE;
        bmcr &= ~(BMCR_SPEED1000 | BMCR_SPEED100);
        // Duplex in fiber status follows BMCR bit in 100Base-FX mode.
        //bmcr &= ~BMCR_FULLDPLX;
        bmcr |= BMCR_FULLDPLX;
        bmcr &= ~BMCR_PDOWN;
    }
    else {
        adv = 0;
        bmcr &= ~(BMCR_ANENABLE | BMCR_ANRESTART);
        bmcr &= ~(BMCR_SPEED1000 | BMCR_SPEED100);
        switch (phydev->speed) {
        case SPEED_1000:
            bmcr |= BMCR_SPEED1000;
            break;
        case SPEED_100:
            bmcr |= BMCR_SPEED100;
            break;
        case SPEED_10:
            break;
        }
        if (phydev->duplex == DUPLEX_FULL)
            bmcr |= BMCR_FULLDPLX;
        else
            bmcr &= ~BMCR_FULLDPLX;
        bmcr &= ~BMCR_PDOWN;
    }
    if (dp->fiber_if == M88E1512_FIBER_IF_1000) {
        err = phy_write(phydev, MII_ADVERTISE, adv);
        if (err < 0)
            goto out;
    }

    err = phy_write(phydev, MII_BMCR, bmcr);
    if (err < 0)
        goto out;

    if (loopback == MARVELL_LOOPBACK_FIBER) {
        //phy_write(phydev, 23, phy_read(phydev, 23) | (1 << 4) | (1 << 1));
        bmcr &= ~BMCR_SPEED100;
        bmcr |= BMCR_SPEED1000 | BMCR_FULLDPLX;
        bmcr |= BMCR_RESET;
        phy_write(phydev, MII_BMCR, bmcr);
        bmcr &= ~BMCR_RESET;
        bmcr |= BMCR_LOOPBACK;
    }
    else {
        bmcr |= BMCR_RESET;
    }
    dev_dbg(&phydev->dev, "Fiber RESET to page %i BMCR 0x%x\n",
            phy_read(phydev, MII_MARVELL_PHY_PAGE), bmcr);
    err = phy_write(phydev, MII_BMCR, bmcr);
    if (err < 0)
        goto out;
    dev_dbg(&phydev->dev, "Fiber page %i BMCR 0x%x\n",
            phy_read(phydev, MII_MARVELL_PHY_PAGE),
            phy_read(phydev, MII_BMCR));

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0)
        return err;

#if 0
    if (loopback) {
        phydev->link = 0;
        phydev->speed = SPEED_UNKNOWN;
        phydev->duplex = DUPLEX_UNKNOWN;
    }
    else {
        phydev->link = 1;
        phydev->speed = SPEED_1000;
        phydev->duplex = DUPLEX_FULL;
    }
#endif

    /* VCT */
    if (vct == MARVELL_VCT) {
        int timeout = 100000;

        msleep(2000);
        dev_info(&phydev->dev, "Start VCT\n");
        phy_write(phydev, MII_MARVELL_PHY_PAGE, 5);
        err = phy_read(phydev, 23);
        err |= 1 << 15;
        err &= ~(0x3 << 11);
        err |= 0x7 << 11;
        phy_write(phydev, 23, err);
        do {
            if (timeout-- == 0)
                break;
            schedule();
            err = phy_read(phydev, 23);
            if (err < 0)
                break;
        } while ((err & (1 << 15)) || !(err & (1 << 14)));
        dev_info(&phydev->dev, "VCT 23:0x%04x results: timeout:%i"
                " 16:0x%04x 17:0x%04x 18:0x%04x 19:0x%04x\n",
                err, 100000 - timeout,
                phy_read(phydev, 16),
                phy_read(phydev, 17),
                phy_read(phydev, 18),
                phy_read(phydev, 19));
        phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    }
    else if (vct == MARVELL_ALT_VCT) {
        int timeout = 100000;

        msleep(5000);
        dev_info(&phydev->dev, "Start ALT VCT\n");
        phy_write(phydev, MII_MARVELL_PHY_PAGE, 7);
        phy_write(phydev, 21, 0);
        phy_write(phydev, 21, 1 << 15);
        do {
            if (timeout-- == 0)
                break;
            schedule();
            err = phy_read(phydev, 21);
            if (err < 0)
                break;
        } while ((err & (1 << 15)) || (err & (1 << 11)));
        dev_info(&phydev->dev, "ALT VCT 21:0x%04x results: timeout:%i"
                " 16:0x%04x 17:0x%04x 18:0x%04x 19:0x%04x 20:0x%04x\n",
                err, 100000 - timeout,
                phy_read(phydev, 16),
                phy_read(phydev, 17),
                phy_read(phydev, 18),
                phy_read(phydev, 19),
                phy_read(phydev, 20));
        phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
    }

    dev_dbg(&phydev->dev, "%s() done link %s speed %i %s\n",
            __func__,
            phydev->link ? "UP" : "DOWN",
            phydev->speed,
            phydev->duplex == DUPLEX_FULL ? "full-duplex" : "half-duplex");

    return marvell_of_reg_init(phydev);
}

/**
 * This is effectively broken on Linux < 3.15
 * for fiber/SGMII.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
static int m88e1512_aneg_done(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;
    int err, oldpage, retval = -EIO;

    dev_dbg(&phydev->dev, "%s()\n", __func__);

    if (loopback) {
        return 1;
    }

    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0)
        return oldpage;
    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    switch (dp->current_if) {
    case M88E1512_IF_NONE:
    case M88E1512_IF_COPPER:
        err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                        MII_88E1512_COPPER_PAGE);
        break;
    default:
        err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                        MII_88E1512_FIBER_PAGE);
    }
    if (err < 0)
        return err;

    // No autoneg in 100Base-FX, check speed and duplex resolved instead.
    if (dp->current_if == M88E1512_IF_100BASEFX) {
        err = phy_read(phydev, MII_M1011_PHY_STATUS);
        if (err < 0)
            goto out;
        if ((err & MII_M1011_PHY_STATUS_LINK) &&
            (err & MII_M1011_PHY_STATUS_RESOLVED))
            retval = 1;
        else
            retval = 0;
    }
    else {
        retval = genphy_aneg_done(phydev);
        if (retval < 0)
            goto out;
    }

out:
    if (err < 0)
        dev_err(&phydev->dev, "%s() failed\n", __func__);
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0)
        return err;

    return retval;
}
#endif

static int m88e1512_read_copper_status(struct phy_device *phydev)
{
    int adv;
    int err;
    int lpa;
    int lpagb;
    int status;
    int bmsr;

    dev_dbg(&phydev->dev, "%s()\n", __func__);

    if (loopback == MARVELL_LOOPBACK_COPPER) {
#if 0
        if (phydev->speed == SPEED_UNKNOWN) {
            phydev->link = 0;
            phydev->speed = SPEED_1000;
            return 0;
        }

        if (phydev->duplex == DUPLEX_UNKNOWN) {
            phydev->link = 0;
            phydev->duplex = DUPLEX_FULL;
            return 0;
        }
#endif
        phydev->link = 1;
        phydev->speed = SPEED_1000;
        phydev->duplex = DUPLEX_FULL;
        return 0;
    }
    else if (loopback == MARVELL_LOOPBACK_FIBER) {
        return 0;
    }

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_COPPER_PAGE);
    if (err < 0)
        return err;

    bmsr = phy_read(phydev, MII_BMSR);
    if (bmsr < 0)
        return bmsr;
    bmsr = phy_read(phydev, MII_BMSR);
    if (bmsr < 0)
        return bmsr;

    status = phy_read(phydev, MII_M1011_PHY_STATUS);
    if (status < 0)
        return status;

    /* VCT */
    if (vct == MARVELL_VCT) {
        if (phydev->link == 0 && (status & MII_M1011_PHY_STATUS_LINK)) {
            int timeout = 100000;

            dev_info(&phydev->dev, "Start VCT (link UP)\n");
            phy_write(phydev, MII_MARVELL_PHY_PAGE, 5);
            err = phy_read(phydev, 23);
            err |= 1 << 15;
            err &= ~(0x3 << 11);
            err |= 0x7 << 11;
            phy_write(phydev, 23, err);
            do {
                if (timeout-- == 0)
                    break;
                schedule();
                err = phy_read(phydev, 23);
                if (err < 0)
                    break;
            } while ((err & (1 << 15)) || !(err & (1 << 14)));
            dev_info(&phydev->dev, "VCT 23:0x%04x results: timeout:%i"
                    " 16:0x%04x 17:0x%04x 18:0x%04x 19:0x%04x\n",
                    err, 100000 - timeout,
                    phy_read(phydev, 16),
                    phy_read(phydev, 17),
                    phy_read(phydev, 18),
                    phy_read(phydev, 19));
            phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
        }
    }
    else if (vct == MARVELL_ALT_VCT) {
        if (phydev->link == 0 && (status & MII_M1011_PHY_STATUS_LINK)) {
            int timeout = 100000;

            msleep(1000);
            dev_info(&phydev->dev, "Start ALT VCT (link UP)\n");
            phy_write(phydev, MII_MARVELL_PHY_PAGE, 7);
            phy_write(phydev, 21, 1 << 15);
            do {
                if (timeout-- == 0)
                    break;
                schedule();
                err = phy_read(phydev, 21);
                if (err < 0)
                    break;
            } while ((err & (1 << 15)) || (err & (1 << 11)));
            dev_info(&phydev->dev, "ALT VCT UP 21:0x%04x results: timeout:%i"
                    " 16:0x%04x 17:0x%04x 18:0x%04x 19:0x%04x 20:0x%04x\n",
                    err, 100000 - timeout,
                    phy_read(phydev, 16),
                    phy_read(phydev, 17),
                    phy_read(phydev, 18),
                    phy_read(phydev, 19),
                    phy_read(phydev, 20));
            phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
        }
    }

    // Special test mode: restart aneg.
    if (loopback == MARVELL_LOOPBACK_COPPER_ANEG) {
        int bmcr = phy_read(phydev, MII_BMCR);
        if (bmcr < 0)
            return bmcr;
        bmcr |= BMCR_ANENABLE | BMCR_ANRESTART;
        err = phy_write(phydev, MII_BMCR, bmcr);
        if (err < 0)
            return err;
        return 0;
    }

    dev_dbg(&phydev->dev,
            "%s() link %s real-time %s global %s BMSR %s BMCR 0x%x\n",
            __func__,
            status & MII_M1011_PHY_STATUS_RESOLVED ? "resolved" : "unresolved",
            status & MII_M1011_PHY_STATUS_LINK ? "UP" : "DOWN",
            status & 0x0004 ? "UP" : "DOWN",
            bmsr & BMSR_LSTATUS ? "UP" : "DOWN",
            phy_read(phydev, MII_BMCR));

    if ((status & MII_M1011_PHY_STATUS_LINK) &&
        (status & MII_M1011_PHY_STATUS_RESOLVED))
        phydev->link = 1;
    else
        phydev->link = 0;
    if (phydev->link != !!(bmsr & BMSR_LSTATUS))
        dev_dbg(&phydev->dev, "BMSR link %i != current link %i\n",
                !!(bmsr & BMSR_LSTATUS),
                phydev->link);
    if (!(bmsr & BMSR_LSTATUS))
        phydev->link = 0;

    if (AUTONEG_ENABLE == phydev->autoneg) {
        lpa = phy_read(phydev, MII_LPA);
        if (lpa < 0)
            return lpa;

#ifdef PHYDEV_HAS_LP_ADVERTISING
        lpagb = phy_read(phydev, MII_STAT1000);
        if (lpagb < 0)
            return lpagb;
        phydev->lp_advertising = mii_lpa_to_ethtool_lpa_t(lpa) |
            mii_stat1000_to_ethtool_lpa_t(lpagb);
#endif

        adv = phy_read(phydev, MII_ADVERTISE);
        if (adv < 0)
            return adv;

        lpa &= adv;

        if (status & MII_M1011_PHY_STATUS_FULLDUPLEX)
            phydev->duplex = DUPLEX_FULL;
        else
            phydev->duplex = DUPLEX_HALF;

        phydev->pause = phydev->asym_pause = 0;

        switch (status & MII_M1011_PHY_STATUS_SPD_MASK) {
        case MII_M1011_PHY_STATUS_1000:
            phydev->speed = SPEED_1000;
            break;

        case MII_M1011_PHY_STATUS_100:
            phydev->speed = SPEED_100;
            break;

        default:
            phydev->speed = SPEED_10;
            break;
        }

        if (phydev->duplex == DUPLEX_FULL) {
            phydev->pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
            phydev->asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
        }
    } else if (phydev->link) {
        int bmcr = phy_read(phydev, MII_BMCR);

        if (bmcr < 0)
            return bmcr;

        if (bmcr & BMCR_FULLDPLX)
            phydev->duplex = DUPLEX_FULL;
        else
            phydev->duplex = DUPLEX_HALF;

        if (bmcr & BMCR_SPEED1000)
            phydev->speed = SPEED_1000;
        else if (bmcr & BMCR_SPEED100)
            phydev->speed = SPEED_100;
        else
            phydev->speed = SPEED_10;

        phydev->pause = phydev->asym_pause = 0;
    }

    dev_dbg(&phydev->dev, "Copper status: link %s %s speed %i %s\n",
            phydev->link ? "UP" : "DOWN",
            phydev->autoneg ? "aneg" : "forced",
            phydev->speed,
            phydev->duplex == DUPLEX_FULL ? "full-duplex" : "half-duplex");

    return 0;
}

/**
 * Translate MII_LPA bits, when in SGMII mode, to ethtool
 * LP advertisement settings.
 */
static inline uint32_t m88e1512_lpa_to_ethtool_lpa_sgmii(uint32_t lpa)
{
    uint32_t result = 0;

    if (lpa & LPA_LPACK)
        result |= ADVERTISED_Autoneg;

    if (lpa & (1u << 7))
        result |= ADVERTISED_FIBRE;
    else
        result |= ADVERTISED_TP;

    switch ((lpa >> 10) & 0x7) {
    case 0x0: result |= ADVERTISED_10baseT_Half; break;
    case 0x1: result |= ADVERTISED_100baseT_Half; break;
    case 0x2: result |= ADVERTISED_1000baseT_Half; break;
    case 0x4: result |= ADVERTISED_10baseT_Full; break;
    case 0x5: result |= ADVERTISED_100baseT_Full; break;
    case 0x6: result |= ADVERTISED_1000baseT_Full; break;
    }

    return result;
}

static int m88e1512_read_fiber_status(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;
    int adv;
    int err;
    int lpa;
    int status;
    int bmsr;

    dev_dbg(&phydev->dev, "%s()\n", __func__);

    if (loopback == MARVELL_LOOPBACK_FIBER) {
#if 0
        if (phydev->speed == SPEED_UNKNOWN) {
            phydev->link = 0;
            phydev->speed = SPEED_1000;
            return 0;
        }

        if (phydev->duplex == DUPLEX_UNKNOWN) {
            phydev->link = 0;
            phydev->duplex = DUPLEX_FULL;
            return 0;
        }
#endif
        phydev->link = 1;
        phydev->speed = SPEED_1000;
        phydev->duplex = DUPLEX_FULL;
        return 0;
    }
    else if (loopback == MARVELL_LOOPBACK_COPPER) {
        return 0;
    }

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_FIBER_PAGE);
    if (err < 0)
        return err;

    bmsr = phy_read(phydev, MII_BMSR);
    if (bmsr < 0)
        return bmsr;

    status = phy_read(phydev, MII_M1011_PHY_STATUS);
    if (status < 0)
        return status;

    if ((status & MII_M1011_PHY_STATUS_LINK) &&
        (status & MII_M1011_PHY_STATUS_RESOLVED))
        phydev->link = 1;
    else
        phydev->link = 0;
    if (phydev->link != !!(bmsr & BMSR_LSTATUS))
        dev_dbg(&phydev->dev, "BMSR link %i != current link %i\n",
                !!(bmsr & BMSR_LSTATUS),
                phydev->link);
    if (!(bmsr & BMSR_LSTATUS))
        phydev->link = 0;

    if (AUTONEG_ENABLE == phydev->autoneg) {
        lpa = phy_read(phydev, MII_LPA);
        if (lpa < 0)
            return lpa;

#ifdef PHYDEV_HAS_LP_ADVERTISING
        switch (dp->fiber_if) {
        case M88E1512_FIBER_IF_1000:
            phydev->lp_advertising = mii_lpa_to_ethtool_lpa_x(lpa) |
                ADVERTISED_FIBRE;
            break;
        case M88E1512_FIBER_IF_100:
            break;
        case M88E1512_FIBER_IF_SGMII:
            phydev->lp_advertising = m88e1512_lpa_to_ethtool_lpa_sgmii(lpa);
            break;
        }
#endif

        adv = phy_read(phydev, MII_ADVERTISE);
        if (adv < 0)
            return adv;

        lpa &= adv;

        if (status & MII_M1011_PHY_STATUS_FULLDUPLEX)
            phydev->duplex = DUPLEX_FULL;
        else
            phydev->duplex = DUPLEX_HALF;

        phydev->pause = phydev->asym_pause = 0;

        switch (status & MII_M1011_PHY_STATUS_SPD_MASK) {
        case MII_M1011_PHY_STATUS_1000:
            phydev->speed = SPEED_1000;
            break;

        case MII_M1011_PHY_STATUS_100:
            phydev->speed = SPEED_100;
            break;

        default:
            phydev->speed = SPEED_10;
            break;
        }

        if (phydev->duplex == DUPLEX_FULL) {
            phydev->pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
            phydev->asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
        }
    }
    else if (phydev->link) {
        int bmcr = phy_read(phydev, MII_BMCR);

        if (bmcr < 0)
            return bmcr;

        if (bmcr & BMCR_FULLDPLX)
            phydev->duplex = DUPLEX_FULL;
        else
            phydev->duplex = DUPLEX_HALF;

        if (bmcr & BMCR_SPEED1000)
            phydev->speed = SPEED_1000;
        else if (bmcr & BMCR_SPEED100)
            phydev->speed = SPEED_100;
        else
            phydev->speed = SPEED_10;

        phydev->pause = phydev->asym_pause = 0;
    }

    dev_dbg(&phydev->dev, "Fiber status: BMSR 0x%04x STATUS 0x%04x"
            " link %s %s speed %i %s\n",
            bmsr, status,
            phydev->link ? "UP" : "DOWN",
            phydev->autoneg ? "aneg" : "forced",
            phydev->speed,
            phydev->duplex == DUPLEX_FULL ? "full-duplex" : "half-duplex");

    return 0;
}

static int m88e1512_read_status(struct phy_device *phydev)
{
    struct m88e1512_dev_priv *dp = phydev->priv;
    int err;
    int oldpage;
    int oldlink = phydev->link;
    enum m88e1512_interface old_if = dp->current_if;

    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0) {
        m88e1512_set_interface(phydev, M88E1512_IF_NONE);
        return oldpage;
    }
    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    err = m88e1512_read_fiber_status(phydev);
    if (err < 0)
        goto out;

    if (!phydev->link) {
        if (oldlink && dp->current_if != M88E1512_IF_COPPER) {
            err = m88e1512_errata_4_18_amd_down(phydev);
            if (err < 0)
                goto out;
        }

        err = m88e1512_read_copper_status(phydev);
        if (err < 0)
            goto out;

        if (phydev->link)
            m88e1512_set_interface(phydev, M88E1512_IF_COPPER);
        else
            m88e1512_set_interface(phydev, M88E1512_IF_NONE);
    }
    else {
        if (!oldlink) {
            err = m88e1512_errata_4_18_amd_up(phydev);
            if (err < 0)
                goto out;
        }
        switch (dp->fiber_if) {
        case M88E1512_FIBER_IF_1000:
            m88e1512_set_interface(phydev, M88E1512_IF_1000BASEX);
            break;
        case M88E1512_FIBER_IF_100:
            m88e1512_set_interface(phydev, M88E1512_IF_100BASEFX);
            break;
        case M88E1512_FIBER_IF_SGMII:
            m88e1512_set_interface(phydev, M88E1512_IF_SGMII);
            break;
        }
    }

    if (dp->current_if != old_if) {
        err = m88e1512_set_led(phydev);
        if (err < 0)
            goto out;
    }

out:
    if (err < 0) {
        dev_err(&phydev->dev, "%s() failed\n", __func__);
        m88e1512_set_interface(phydev, M88E1512_IF_NONE);
    }
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0) {
        m88e1512_set_interface(phydev, M88E1512_IF_NONE);
        return err;
    }

    dev_dbg(&phydev->dev, "Final status: %s link %s %s speed %i %s oldpage %i\n",
            m88e1512_get_interface_str(phydev),
            phydev->link ? "UP" : "DOWN",
            phydev->autoneg ? "aneg" : "forced",
            phydev->speed,
            phydev->duplex == DUPLEX_FULL ? "full-duplex" : "half-duplex",
            oldpage);

    return 0;
}

static int m88e1512_did_interrupt(struct phy_device *phydev)
{
    int oldpage;
    int err = -EIO;
    int imask_copper = -EIO;
    int imask_fiber = -EIO;

    oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
    if (oldpage < 0)
        return 0;

    if (oldpage != 0)
        dev_warn(&phydev->dev, "%s() current page %i != expected 0\n",
                 __func__, oldpage);

    imask_copper = phy_read(phydev, MII_M1011_IEVENT);
    if (imask_copper < 0)
        goto out;

    err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
                    MII_88E1512_FIBER_PAGE);
    if (err < 0)
        goto out;

    imask_fiber = phy_read(phydev, MII_M1011_IEVENT);
    if (imask_fiber < 0)
        goto out;

out:
    err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
    if (err < 0)
        return 0;

    if (imask_copper < 0 || imask_fiber < 0)
        return 0;

    if ((imask_copper & MII_M1011_IMASK_INIT) |
        (imask_fiber & MII_M1011_IMASK_INIT))
        return 1;

    return 0;
}

static struct phy_driver m88e1512_drivers[] = {
    {
        .phy_id = MARVELL_PHY_ID_88E1512,
        .phy_id_mask = MARVELL_PHY_ID_MASK,
        .name = "Marvell 88E1512",
        .features = PHY_GBIT_FEATURES | SUPPORTED_FIBRE,
        .flags = PHY_HAS_INTERRUPT,
        .probe = &m88e1512_probe,
        .remove = &m88e1512_remove,
        .config_aneg = &m88e1512_config_aneg,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
        .aneg_done = &m88e1512_aneg_done,
#endif
        .read_status = &m88e1512_read_status,
        .ack_interrupt = &m88e1512_ack_interrupt,
        .config_intr = &m88e1512_config_intr,
        .did_interrupt = &m88e1512_did_interrupt,
        .resume = &genphy_resume,
        .suspend = &genphy_suspend,
        .driver = { .owner = THIS_MODULE },
    },
};

static int __init m88e1512_init(void)
{
    struct m88e1512_drv_priv *drv = &drv_priv;

    drv->debug_dir = debugfs_create_dir("m88e1512", NULL);
    if (IS_ERR(drv->debug_dir))
        drv->debug_dir = NULL;

    return phy_drivers_register(m88e1512_drivers,
                                ARRAY_SIZE(m88e1512_drivers));
}

static void __exit m88e1512_exit(void)
{
    struct m88e1512_drv_priv *drv = &drv_priv;

    phy_drivers_unregister(m88e1512_drivers,
                           ARRAY_SIZE(m88e1512_drivers));

    if (drv->debug_dir) {
        debugfs_remove(drv->debug_dir);
        drv->debug_dir = NULL;
    }

    return;
}

module_init(m88e1512_init);
module_exit(m88e1512_exit);

static struct mdio_device_id __maybe_unused m88e1512_tbl[] = {
    { MARVELL_PHY_ID_88E1512, MARVELL_PHY_ID_MASK },
    { }
};

MODULE_DEVICE_TABLE(mdio, m88e1512_tbl);
