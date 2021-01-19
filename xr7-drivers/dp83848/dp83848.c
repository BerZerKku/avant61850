/**
 * @file
 * @brief National Semiconductor DP83848 PHYTER Linux driver
 */

/*

   DP83848 PHY Linux driver

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

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

/// DP83848 PHY ID value
#define DP83848_PHY_ID          0x20005c90

/// MII PHY status register
#define DP83848_PHYSTS          0x10
#define DP83848_PHYSTS_INT      (1u << 7)

/// MII interrupt control register
#define DP83848_MICR            0x11
#define DP83848_MICR_INT_OE     (1u << 0)       ///< Interrupt output enable
#define DP83848_MICR_INT_EN     (1u << 1)       ///< Interrupt enable

/// MII interrupt status register
#define DP83848_MISR            0x12
#define DP83848_MISR_MASK       0x3800          ///< Interesting interrupts
#define DP83848_MISR_INIT       0x0038          ///< Interesting interrupts

/// LED control register
#define DP83848_LEDCR                   0x18
#define DP83848_LEDCR_DRV_SPDLED        BIT(5)
#define DP83848_LEDCR_DRV_LNKLED        BIT(4)
#define DP83848_LEDCR_DRV_ACTLED        BIT(3)
#define DP83848_LEDCR_SPDLED            BIT(2)
#define DP83848_LEDCR_LNKLED            BIT(1)
#define DP83848_LEDCR_ACTLED            BIT(0)

/// PHY control register
#define DP83848_PHYCR                   0x19
#define DP83848_PHYCR_LEDCNFG1          BIT(6)
#define DP83848_PHYCR_LEDCNFG0          BIT(5)

MODULE_DESCRIPTION("NatSemi DP83848 PHY driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");

/**
 * Initialize PHY, also after reset.
 */
static int dp83848_config_init(struct phy_device *phydev)
{
    int ret;

    dev_dbg(&phydev->dev, "%s()\n", __func__);

    dev_dbg(&phydev->dev, "BMCR:  0x%x\n", phy_read(phydev, MII_BMCR));
    dev_dbg(&phydev->dev, "BMSR:  0x%x\n", phy_read(phydev, MII_BMSR));
    dev_dbg(&phydev->dev, "ID1:   0x%x\n", phy_read(phydev, MII_PHYSID1));
    dev_dbg(&phydev->dev, "ID2:   0x%x\n", phy_read(phydev, MII_PHYSID2));
    dev_dbg(&phydev->dev, "ADV:   0x%x\n", phy_read(phydev, MII_ADVERTISE));
    dev_dbg(&phydev->dev, "LPA:   0x%x\n", phy_read(phydev, MII_LPA));
    dev_dbg(&phydev->dev, "PHYSTS 0x%x\n", phy_read(phydev, 0x10));
    dev_dbg(&phydev->dev, "FCSCR: 0x%x\n", phy_read(phydev, 0x14));
    dev_dbg(&phydev->dev, "RECR:  0x%x\n", phy_read(phydev, 0x15));
    dev_dbg(&phydev->dev, "PCSR:  0x%x\n", phy_read(phydev, 0x16));
    dev_dbg(&phydev->dev, "RBR:   0x%x\n", phy_read(phydev, 0x17));
    dev_dbg(&phydev->dev, "LEDCR: 0x%x\n", phy_read(phydev, 0x18));
    dev_dbg(&phydev->dev, "PHYCR: 0x%x\n", phy_read(phydev, 0x19));

    /*
     * Set PWR_DOWN/INT pin to interrupt mode so that if it is connected to
     * other devices this PHY does not enter power down mode when other
     * devices generate interrupts.
     */
    ret = phy_write(phydev, DP83848_MICR, DP83848_MICR_INT_OE);
    if (ret < 0) {
        dev_err(&phydev->dev, "Config failed: write MICR I/O error\n");
        return ret;
    }

    // Configure LEDs: Use only link led.
    ret = phy_read(phydev, DP83848_LEDCR);
    if (ret < 0) {
        dev_err(&phydev->dev, "Config failed: read LEDCR I/O error\n");
        return ret;
    }
    ret |= DP83848_LEDCR_DRV_SPDLED | DP83848_LEDCR_DRV_ACTLED;
    ret |= DP83848_LEDCR_SPDLED | DP83848_LEDCR_ACTLED;
    ret = phy_write(phydev, DP83848_LEDCR, ret);
    if (ret < 0) {
        dev_err(&phydev->dev, "Config failed: write LEDCR I/O error\n");
    }

    // LED mode 3.
    ret = phy_read(phydev, DP83848_PHYCR);
    if (ret < 0) {
        dev_err(&phydev->dev, "Config failed: read PHYCR I/O error\n");
        return ret;
    }
    ret |= DP83848_PHYCR_LEDCNFG1;
    ret &= ~DP83848_PHYCR_LEDCNFG0;
    ret = phy_write(phydev, DP83848_PHYCR, ret);
    if (ret < 0) {
        dev_err(&phydev->dev, "Config failed: write PHYCR I/O error\n");
    }

    // genphy_config_init was exported in Linux 3.16.
    //ret = genphy_config_init(phydev);

    // This is not a generic driver, so nothing to do here.

    return ret;
}

/**
 * Configure autonegotiation or forced speed.
 */
static int dp83848_config_aneg(struct phy_device *phydev)
{
    int ret = -1;

    dev_dbg(&phydev->dev, "%s(): ANEG:%s SPEED:%i %s\n",
            __func__,
            phydev->autoneg == AUTONEG_ENABLE ? "on" : "off",
            phydev->speed == SPEED_100 ? 100 : (phydev->speed == SPEED_10 ? 10 : 0),
            phydev->duplex == DUPLEX_FULL ? "full-duplex" : "half-duplex");

    dev_dbg(&phydev->dev, "%s(): BMCR   0x%x\n",
            __func__, phy_read(phydev, MII_BMCR));
    dev_dbg(&phydev->dev, "%s(): BMSR   0x%x\n",
            __func__, phy_read(phydev, MII_BMSR));
    dev_dbg(&phydev->dev, "%s(): PHYSTS 0x%x\n",
            __func__, phy_read(phydev, DP83848_PHYSTS));
    dev_dbg(&phydev->dev, "%s(): PCSR   0x%x\n",
            __func__, phy_read(phydev, 0x16));
    dev_dbg(&phydev->dev, "%s(): PHYCR  0x%x\n",
            __func__, phy_read(phydev, 0x19));

    ret = genphy_config_aneg(phydev);

    return ret;
}

/**
 * Determine link status, speed and duplex.
 */
static int dp83848_read_status(struct phy_device *phydev)
{
    int ret = -1;

    ret = genphy_read_status(phydev);

    dev_dbg(&phydev->dev, "%s():"
            " BMCR 0x%04x BMSR 0x%04x"
            " PHYSTS 0x%04x PCSR 0x%04x PHYCR 0x%04x\n",
            __func__,
            phy_read(phydev, MII_BMCR),
            phy_read(phydev, MII_BMSR),
            phy_read(phydev, 0x10),
            phy_read(phydev, 0x16),
            phy_read(phydev, 0x19));

    return ret;
}

/**
 * Acknowledge interrupt.
 */
static int dp83848_ack_interrupt(struct phy_device *phydev)
{
    int ret;

    ret = phy_read(phydev, DP83848_MISR);
    if (ret < 0)
        return ret;

    return 0;
}

/**
 * Configure interrupt.
 */
static int dp83848_config_intr(struct phy_device *phydev)
{
    int ret;
    int micr;

    micr = phy_read(phydev, DP83848_MICR);
    if (micr < 0)
        return micr;

    if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
        ret = phy_write(phydev, DP83848_MISR, DP83848_MISR_INIT);
        if (ret == 0)
            ret = phy_write(phydev, DP83848_MICR, micr | DP83848_MICR_INT_EN);
    }
    else {
        ret = phy_write(phydev, DP83848_MISR, 0);
        if (ret == 0)
            ret = phy_write(phydev, DP83848_MICR, micr & ~DP83848_MICR_INT_EN);
    }
    if (ret < 0)
        return ret;

    return 0;
}

/**
 * Determine whether an interrupt has been generated.
 */
static int dp83848_did_interrupt(struct phy_device *phydev)
{
    int ret;

    ret = phy_read(phydev, DP83848_MISR);
    if (ret < 0)
        return 0;

    if (ret & DP83848_MISR_MASK)
        return 1;

    return 0;
}

/**
 * DP83848 driver definition.
 */
static struct phy_driver dp83848_driver = {
    .phy_id = DP83848_PHY_ID,
    .phy_id_mask = 0xfffffff0,
    .name = "NatSemi DP83848",
    .features = PHY_BASIC_FEATURES |
        SUPPORTED_Pause |
        SUPPORTED_Asym_Pause,
    .flags = 0,
    .config_init = dp83848_config_init,
    .config_aneg = dp83848_config_aneg,
    .read_status = dp83848_read_status,
    .ack_interrupt = &dp83848_ack_interrupt,
    .config_intr = &dp83848_config_intr,
    .did_interrupt = &dp83848_did_interrupt,
    .driver = {
        .owner = THIS_MODULE,
    },
};

/**
 * Driver initialization.
 */
static int __init dp83848_init(void)
{
    return phy_driver_register(&dp83848_driver);
}

/**
 * Driver cleanup.
 */
static void __exit dp83848_cleanup(void)
{
    phy_driver_unregister(&dp83848_driver);
}

module_init(dp83848_init);
module_exit(dp83848_cleanup);

static struct mdio_device_id __maybe_unused dp83848_tbl[] = {
    { DP83848_PHY_ID, 0xfffffff0 },
    { }
};

MODULE_DEVICE_TABLE(mdio, dp83848_tbl);
