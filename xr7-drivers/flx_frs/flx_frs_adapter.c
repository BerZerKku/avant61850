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

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/netdevice.h>

#include "flx_frs_types.h"
#include "flx_frs_if.h"
#include "flx_frs_sfp.h"
#include "flx_frs_adapter.h"

/**
 * SGMII/1000Base-X and SGMII/1000Base-X/100Base-FX adapter modes
 */
enum flx_frs_adapter_mode {
    FLX_FRS_ADAPTER_SGMII,      ///< SGMII
    FLX_FRS_ADAPTER_1000BASEX,  ///< 1000Base-X
    FLX_FRS_ADAPTER_100BASEFX,  ///< 100Base-FX
};

/**
 * Determine mode adapter should be configured to.
 * @param pp FRS port privates.
 * @return Adapter mode that should be used.
 */
static enum flx_frs_adapter_mode flx_frs_get_adapter_mode(
        struct flx_frs_port_priv *pp)
{
    // Use SFP type if it is known.
    // 100Base-T SFP pretends to be 100Base-FX, so use that.
    switch (pp->sfp.type) {
    case FLX_FRS_SFP_1000BASEX:
        return FLX_FRS_ADAPTER_1000BASEX;
    case FLX_FRS_SFP_100BASEFX:
    case FLX_FRS_SFP_100BASET:
        return FLX_FRS_ADAPTER_100BASEFX;
    case FLX_FRS_SFP_1000BASET:
        return FLX_FRS_ADAPTER_SGMII;
    case FLX_FRS_SFP_NONE:
    case FLX_FRS_SFP_UNSUPPORTED:
        break;
    default:
        return FLX_FRS_ADAPTER_1000BASEX;
    }

    // SFP type is not known at all.
    if (pp->ext_phy.phydev || (pp->flags & FLX_FRS_ADAPTER_SGMII_PHY_MODE)) {
        return FLX_FRS_ADAPTER_SGMII;
    }

    return FLX_FRS_ADAPTER_1000BASEX;
}

/**
 * Common adapter link update function to set link LED according to link mode.
 * @param pp FRS port privates.
 */
static inline void flx_frs_update_link_led(struct flx_frs_port_priv *pp)
{
    struct flx_frs_netdev_priv *np = netdev_priv(pp->netdev);
    uint16_t value = np->link_mode == LM_DOWN ? 0 : ADAPTER_LINK_STATUS_UP;

    // Update link LED.
    flx_frs_write_adapter_reg(pp, ADAPTER_REG_LINK_STATUS, value);

    return;
}

// Altera triple-speed Ethernet adapter

/**
 * Setup Altera triple-speed Ethernet for SGMII.
 * @param pp FRS port privates.
 */
static int flx_frs_setup_alt_tse_to_sgmii(struct flx_frs_port_priv *pp)
{
    netdev_dbg(pp->netdev, "%s()\n", __func__);

    flx_frs_write_adapter_reg(pp, ALT_TSE_PCS_IFMODE, 0x0003);
    flx_frs_write_adapter_reg(pp, ALT_TSE_PCS_DEV_ABILITY, 0x0000);
    flx_frs_write_adapter_reg(pp, ALT_TSE_PCS_CONTROL, 0x9200);

    pp->adapter.port = PORT_MII;

    return 0;
}

/**
 * Setup Altera triple-speed Ethernet for 1000Base-X.
 * @param pp FRS port privates.
 */
static int flx_frs_setup_alt_tse_to_1000basex(struct flx_frs_port_priv *pp)
{
    netdev_dbg(pp->netdev, "%s()\n", __func__);

    flx_frs_write_adapter_reg(pp, ALT_TSE_PCS_IFMODE, 0x0000);
    flx_frs_write_adapter_reg(pp, ALT_TSE_PCS_DEV_ABILITY, 0x0020);
    flx_frs_write_adapter_reg(pp, ALT_TSE_PCS_CONTROL, 0x9200);

    pp->adapter.port = PORT_FIBRE;

    return 0;
}

/**
 * Setup Altera triple-speed Ethernet to correct mode.
 * @param pp FRS port privates.
 */
static void flx_frs_setup_alt_tse(struct flx_frs_port_priv *pp)
{
    enum flx_frs_adapter_mode mode = flx_frs_get_adapter_mode(pp);

    switch (mode) {
    case FLX_FRS_ADAPTER_SGMII:
        flx_frs_setup_alt_tse_to_sgmii(pp);
        break;
    case FLX_FRS_ADAPTER_1000BASEX:
        flx_frs_setup_alt_tse_to_1000basex(pp);
        break;
    case FLX_FRS_ADAPTER_100BASEFX:
        // 100Base-FX module in SGMII/1000Base-X adapter?
        // Can't handle this correctly.
        flx_frs_setup_alt_tse_to_1000basex(pp);
        break;
    }

    return;
}

/**
 * Check Altera triple-speed Ethernet link state.
 * @param pp FRS port privates.
 */
static enum link_mode flx_frs_check_link_alt_tse(struct flx_frs_port_priv *pp)
{
    enum link_mode link_mode = LM_DOWN;
    int ret = 0;

    ret = flx_frs_read_adapter_reg(pp, ALT_TSE_PCS_STATUS);
    if ((ret >= 0) &&
        (ret & ALT_TSE_PCS_STATUS_AUTONEG_COMPLETE) &&
        (ret & ALT_TSE_PCS_STATUS_LINK_UP)) {
        link_mode = LM_1000FULL;
    }

    netdev_dbg(pp->netdev, "%s() link mode %i\n", __func__, link_mode);

    return link_mode;
}

// SGMII/1000Base-X and SGMII/1000Base-X/100Base-FX adapter

/**
 * Setup SGMII/1000Base-X or SGMII/1000Base-X/100Base-FX adapter for SGMII.
 * @param pp FRS port privates.
 */
static int flx_frs_setup_sgmii_1000basex_to_sgmii(struct flx_frs_port_priv *pp)
{
    struct flx_frs_netdev_priv *np = netdev_priv(pp->netdev);
    uint16_t pcs_control = SGMII_1000BASEX_PCS_CONTROL_IF_SGMII;
    uint16_t pcs_sgmii_control = 0;
    uint16_t pcs_sgmii_dev_config = 0;

    netdev_dbg(pp->netdev, "%s()\n", __func__);

    // PCS control must be written last.

    if (np->force_link_mode == LM_DOWN) {
        // Autonegotiation.
        pcs_control |=
            SGMII_1000BASEX_PCS_CONTROL_AUTONEG_ENABLE |
            SGMII_1000BASEX_PCS_CONTROL_AUTONEG_RESTART;
    }

    switch (np->link_mode) {
    case LM_1000FULL:
        pcs_sgmii_control |=
            SGMII_1000BASEX_PCS_SGMII_CONTROL_SPEED_1000M;
        pcs_sgmii_dev_config |=
            SGMII_1000BASEX_PCS_SGMII_DEV_CONFIG_LINK_UP |
            SGMII_1000BASEX_PCS_SGMII_DEV_CONFIG_SPEED_1000M;
        break;
    case LM_100FULL:
        pcs_sgmii_control |=
            SGMII_1000BASEX_PCS_SGMII_CONTROL_SPEED_100M;
        pcs_sgmii_dev_config |=
            SGMII_1000BASEX_PCS_SGMII_DEV_CONFIG_LINK_UP |
            SGMII_1000BASEX_PCS_SGMII_DEV_CONFIG_SPEED_100M;
        break;
    case LM_10FULL:
        pcs_sgmii_control |=
            SGMII_1000BASEX_PCS_SGMII_CONTROL_SPEED_10M;
        pcs_sgmii_dev_config |=
            SGMII_1000BASEX_PCS_SGMII_DEV_CONFIG_LINK_UP |
            SGMII_1000BASEX_PCS_SGMII_DEV_CONFIG_SPEED_10M;
        break;
    case LM_DOWN:
        // Fallback to 1000 Mb/s instead of usual 10 Mb/s.
        pcs_sgmii_control |=
            SGMII_1000BASEX_PCS_SGMII_CONTROL_SPEED_1000M;
        pcs_sgmii_dev_config |=
            SGMII_1000BASEX_PCS_SGMII_DEV_CONFIG_SPEED_1000M;
        break;
    }

    // PCS SGMII device configuration is only used in SGMII PHY mode
    // when autonegotiation is enabled.
    if (pp->flags & FLX_FRS_ADAPTER_SGMII_PHY_MODE) {
        pcs_sgmii_control |= SGMII_1000BASEX_PCS_SGMII_CONTROL_MODE_PHY;
        if (np->force_link_mode == LM_DOWN) {
            flx_frs_write_adapter_reg(pp,
                                      SGMII_1000BASEX_REG_PCS_SGMII_DEV_CONFIG,
                                      pcs_sgmii_dev_config);
        }
    }
    else {
        pcs_sgmii_control |= SGMII_1000BASEX_PCS_SGMII_CONTROL_MODE_MAC;
    }

    flx_frs_write_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_SGMII_CONTROL,
                              pcs_sgmii_control);
    flx_frs_write_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_CONTROL,
                              pcs_control);

    pp->adapter.port = PORT_MII;

    return 0;
}

/**
 * Setup SGMII/1000Base-X or SGMII/1000Base-X/100Base-FX adapter
 * for 1000Base-X.
 * @param pp FRS port privates.
 */
static int flx_frs_setup_sgmii_1000basex_to_1000basex(
        struct flx_frs_port_priv *pp)
{
    struct flx_frs_netdev_priv *np = netdev_priv(pp->netdev);
    uint16_t pcs_control = SGMII_1000BASEX_PCS_CONTROL_IF_1000BASEX;
    uint16_t pcs_sgmii_control = SGMII_1000BASEX_PCS_SGMII_CONTROL_SPEED_1000M;

    netdev_dbg(pp->netdev, "%s()\n", __func__);

    // PCS control must be written last.

    if (np->force_link_mode == LM_DOWN) {
        // Autonegotiation.
        pcs_control |=
            SGMII_1000BASEX_PCS_CONTROL_AUTONEG_ENABLE |
            SGMII_1000BASEX_PCS_CONTROL_AUTONEG_RESTART;
    }

    flx_frs_write_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_SGMII_CONTROL,
                              pcs_sgmii_control);
    flx_frs_write_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_CONTROL,
                              pcs_control);

    pp->adapter.port = PORT_FIBRE;

    return 0;
}

/**
 * Setup SGMII/1000Base-X/100Base-FX adapter for 100Base-FX.
 * @param pp FRS port privates.
 */
static int flx_frs_setup_sgmii_1000basex_to_100basefx(
        struct flx_frs_port_priv *pp)
{
    struct flx_frs_netdev_priv *np = netdev_priv(pp->netdev);
    uint16_t pcs_control = SGMII_1000BASEX_PCS_CONTROL_IF_100BASEFX;
    uint16_t pcs_sgmii_control = SGMII_1000BASEX_PCS_SGMII_CONTROL_SPEED_1000M;

    netdev_dbg(pp->netdev, "%s()\n", __func__);

    // PCS control must be written last.

    if (np->force_link_mode == LM_DOWN) {
        // Autonegotiation.
        pcs_control |=
            SGMII_1000BASEX_PCS_CONTROL_AUTONEG_ENABLE |
            SGMII_1000BASEX_PCS_CONTROL_AUTONEG_RESTART;
    }

    flx_frs_write_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_SGMII_CONTROL,
                              pcs_sgmii_control);
    flx_frs_write_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_CONTROL,
                              pcs_control);

    pp->adapter.port = PORT_FIBRE;

    return 0;
}

/**
 * Setup SGMII/1000Base-X adapter to correct mode.
 * @param pp FRS port privates.
 */
static void flx_frs_setup_sgmii_1000basex(struct flx_frs_port_priv *pp)
{
    enum flx_frs_adapter_mode mode = flx_frs_get_adapter_mode(pp);

    switch (mode) {
    case FLX_FRS_ADAPTER_SGMII:
        flx_frs_setup_sgmii_1000basex_to_sgmii(pp);
        break;
    case FLX_FRS_ADAPTER_1000BASEX:
        flx_frs_setup_sgmii_1000basex_to_1000basex(pp);
        break;
    case FLX_FRS_ADAPTER_100BASEFX:
        // 100Base-FX module in SGMII/1000Base-X adapter?
        // Can't handle this correctly.
        flx_frs_setup_sgmii_1000basex_to_1000basex(pp);
        break;
    }

    return;
}

/**
 * Setup SGMII/1000Base-X/100Base-FX adapter to correct mode.
 * @param pp FRS port privates.
 */
static void flx_frs_setup_sgmii_1000basex_100basefx(struct flx_frs_port_priv *pp)
{
    enum flx_frs_adapter_mode mode = flx_frs_get_adapter_mode(pp);

    switch (mode) {
    case FLX_FRS_ADAPTER_SGMII:
        flx_frs_setup_sgmii_1000basex_to_sgmii(pp);
        break;
    case FLX_FRS_ADAPTER_1000BASEX:
        flx_frs_setup_sgmii_1000basex_to_1000basex(pp);
        break;
    case FLX_FRS_ADAPTER_100BASEFX:
        flx_frs_setup_sgmii_1000basex_to_100basefx(pp);
        break;
    }

    return;
}

/**
 * Check SGMII/1000Base-X adapter link state.
 * @param pp FRS port privates.
 */
static enum link_mode flx_frs_check_link_sgmii_1000basex(
        struct flx_frs_port_priv *pp)
{
    enum link_mode link_mode = LM_DOWN;
    int ret = 0;

    ret = flx_frs_read_adapter_reg(pp, ADAPTER_REG_LINK_STATUS);
    if (ret < 0)
        goto out;

    if (ret & SGMII_1000BASEX_LINK_STATUS_SGMII_1000BASEX_UP) {
        ret = flx_frs_read_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_STATUS);
        if (ret >= 0 &&
            (ret & SGMII_1000BASEX_PCS_STATUS_AUTONEG_COMPLETE)) {
            switch (ret & SGMII_1000BASEX_PCS_STATUS_SPEED_MASK) {
            case SGMII_1000BASEX_PCS_STATUS_SPEED_1000M:
                link_mode = LM_1000FULL;
                break;
            case SGMII_1000BASEX_PCS_STATUS_SPEED_100M:
                link_mode = LM_100FULL;
                break;
            case SGMII_1000BASEX_PCS_STATUS_SPEED_10M:
                link_mode = LM_10FULL;
                break;
            }
        }
    }

out:
    netdev_dbg(pp->netdev, "%s() link mode %i\n", __func__, link_mode);

    return link_mode;
}

/**
 * Check SGMII/1000Base-X/100Base-FX adapter link state.
 * @param pp FRS port privates.
 */
static enum link_mode flx_frs_check_link_sgmii_1000basex_100basefx(
        struct flx_frs_port_priv *pp)
{
    enum link_mode link_mode = LM_DOWN;
    enum flx_frs_adapter_mode mode = flx_frs_get_adapter_mode(pp);
    int ret = 0;

    ret = flx_frs_read_adapter_reg(pp, ADAPTER_REG_LINK_STATUS);
    if (ret < 0)
        goto out;

    switch (mode) {
    case FLX_FRS_ADAPTER_SGMII:
    case FLX_FRS_ADAPTER_1000BASEX:
        if (ret & SGMII_1000BASEX_LINK_STATUS_SGMII_1000BASEX_UP) {
            ret = flx_frs_read_adapter_reg(pp, SGMII_1000BASEX_REG_PCS_STATUS);
            if (ret < 0)
                goto out;
            if (ret & SGMII_1000BASEX_PCS_STATUS_AUTONEG_COMPLETE) {
                switch (ret & SGMII_1000BASEX_PCS_STATUS_SPEED_MASK) {
                case SGMII_1000BASEX_PCS_STATUS_SPEED_1000M:
                    link_mode = LM_1000FULL;
                    break;
                case SGMII_1000BASEX_PCS_STATUS_SPEED_100M:
                    link_mode = LM_100FULL;
                    break;
                case SGMII_1000BASEX_PCS_STATUS_SPEED_10M:
                    link_mode = LM_10FULL;
                    break;
                }
            }
        }
        break;
    case FLX_FRS_ADAPTER_100BASEFX:
        // No autoneg.
        if (ret & SGMII_1000BASEX_LINK_STATUS_100BASEFX_UP) {
            link_mode = LM_100FULL;
        }
        break;
    }

out:
    netdev_dbg(pp->netdev, "%s() link mode %i\n", __func__, link_mode);

    return link_mode;
}

// 1000Base-X adapter

/**
 * Check 1000Base-X adapter link status.
 * @param pp FRS port privates.
 */
static enum link_mode flx_frs_check_link_1000basex(
        struct flx_frs_port_priv *pp)
{
    enum link_mode link_mode = LM_DOWN;
    int ret;

    ret = flx_frs_read_adapter_reg(pp, ADAPTER_REG_LINK_STATUS);
    if (ret >= 0 && ret & ADAPTER_LINK_STATUS_UP) {
        link_mode = LM_1000FULL;
    }

    netdev_dbg(pp->netdev, "%s() link mode %i\n", __func__, link_mode);

    return link_mode;
}

// 100Base-FX adapter

//
/**
 * Check 100Base-FX adapter link status.
 * @param pp FRS port privates.
 */
static enum link_mode flx_frs_check_link_100basefx(
        struct flx_frs_port_priv *pp)
{
    enum link_mode link_mode = LM_DOWN;
    int ret;

    ret = flx_frs_read_adapter_reg(pp, ADAPTER_REG_LINK_STATUS);
    if (ret >= 0 && ret & ADAPTER_LINK_STATUS_UP) {
        link_mode = LM_100FULL;
    }

    netdev_dbg(pp->netdev, "%s() link mode %i\n", __func__, link_mode);

    return link_mode;
}

// MII adapter

// RMII adapter

// RGMII adapter

/**
 * Check RGMII adapter link status.
 */
static enum link_mode flx_frs_check_link_rgmii(struct flx_frs_port_priv *pp)
{
    enum link_mode link_mode = LM_DOWN;
    int ret;

    ret = flx_frs_read_adapter_reg(pp, ADAPTER_REG_LINK_STATUS);

    if (ret >= 0 && ret & ADAPTER_LINK_STATUS_UP) {
        switch (ret & ADAPTER_RGMII_SPEED_MASK) {
        case ADAPTER_RGMII_SPEED_1000M:
            link_mode = LM_1000FULL;
            break;
        case ADAPTER_RGMII_SPEED_100M:
            link_mode = LM_100FULL;
            break;
        case ADAPTER_RGMII_SPEED_10M:
            link_mode = LM_10FULL;
            break;
        }
    }

    netdev_dbg(pp->netdev, "%s() link mode %i\n", __func__, link_mode);

    return link_mode;
}

// All adapters

/**
 * Setup FRS port adapter handling according to adapter type.
 * Detect also SFP module type from presence of PHY.
 * if it cannot be detected from SFP EEPROM.
 * @param pp FRS port privates.
 */
int flx_frs_init_adapter(struct flx_frs_port_priv *pp)
{
    int adapter_id = 0;
    int ret = 0;

    pp->adapter.ops.check_link = NULL;
    pp->adapter.ops.update_link = NULL;

    ret = flx_frs_read_adapter_reg(pp, ADAPTER_REG_ID);
    if (ret >= 0)
        adapter_id = (ret >> ADAPTER_ID_ID_SHIFT) & ADAPTER_ID_ID_MASK;
    switch (adapter_id) {
    case 0:
    case ADAPTER_ID_ID_MASK:
        pp->adapter.supported =
            SUPPORTED_MII |
            SUPPORTED_TP |
            SUPPORTED_FIBRE |
            SUPPORTED_1000baseT_Full |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full |
            SUPPORTED_Autoneg | 0;
        pp->adapter.port = PORT_MII;
        break;
    case ADAPTER_ID_ALT_TSE:
        pp->adapter.ops.setup = &flx_frs_setup_alt_tse;
        pp->adapter.ops.check_link = &flx_frs_check_link_alt_tse;
        pp->adapter.ops.update_link = &flx_frs_update_link_led;
        pp->adapter.supported =
            SUPPORTED_MII |
            SUPPORTED_TP |
            SUPPORTED_FIBRE |
            SUPPORTED_1000baseT_Full |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full |
            SUPPORTED_Autoneg | 0;
        break;
    case ADAPTER_ID_SGMII_1000BASEX:
    case ADAPTER_ID_SGMII_1000BASEX_EXT_TX_PLL:
        pp->adapter.ops.setup = &flx_frs_setup_sgmii_1000basex;
        pp->adapter.ops.check_link = &flx_frs_check_link_sgmii_1000basex;
        pp->adapter.ops.update_link = &flx_frs_update_link_led;
        pp->adapter.supported =
            SUPPORTED_MII |
            SUPPORTED_TP |
            SUPPORTED_FIBRE |
            SUPPORTED_1000baseT_Full |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full |
            SUPPORTED_Autoneg | 0;
        break;
    case ADAPTER_ID_SGMII_1000BASEX_100BASEFX_EXT_TX_PLL:
        pp->adapter.ops.setup = &flx_frs_setup_sgmii_1000basex_100basefx;
        pp->adapter.ops.check_link =
            &flx_frs_check_link_sgmii_1000basex_100basefx;
        pp->adapter.ops.update_link = &flx_frs_update_link_led;
        pp->adapter.supported =
            SUPPORTED_MII |
            SUPPORTED_TP |
            SUPPORTED_FIBRE |
            SUPPORTED_1000baseT_Full |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full |
            SUPPORTED_Autoneg | 0;
        break;
    case ADAPTER_ID_1000BASE_X:
        pp->adapter.ops.check_link = &flx_frs_check_link_1000basex;
        pp->adapter.supported =
            SUPPORTED_FIBRE |
            SUPPORTED_1000baseT_Full |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full |
            SUPPORTED_Autoneg | 0;
        pp->adapter.port = PORT_FIBRE;
        break;
    case ADAPTER_ID_100BASE_FX:
    case ADAPTER_ID_100BASE_FX_EXT_TX_PLL:
        pp->adapter.ops.check_link = &flx_frs_check_link_100basefx;
        pp->adapter.supported =
            SUPPORTED_FIBRE |
            SUPPORTED_100baseT_Full |
            SUPPORTED_Autoneg | 0;
        pp->adapter.port = PORT_FIBRE;
        break;
    case ADAPTER_ID_MII:
        pp->adapter.ops.update_link = &flx_frs_update_link_led;
        pp->adapter.supported =
            SUPPORTED_MII |
            SUPPORTED_TP |
            SUPPORTED_FIBRE |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full | 0;
        pp->adapter.port = PORT_MII;
        break;
    case ADAPTER_ID_RMII:
        pp->adapter.ops.update_link = &flx_frs_update_link_led;
        pp->adapter.supported =
            SUPPORTED_MII |
            SUPPORTED_TP |
            SUPPORTED_FIBRE |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full | 0;
        pp->adapter.port = PORT_MII;
        break;
    case ADAPTER_ID_RGMII:
        pp->adapter.ops.check_link = &flx_frs_check_link_rgmii;
        pp->adapter.supported =
            SUPPORTED_MII |
            SUPPORTED_TP |
            SUPPORTED_FIBRE |
            SUPPORTED_1000baseT_Full |
            SUPPORTED_100baseT_Full |
            SUPPORTED_10baseT_Full |
            SUPPORTED_Autoneg | 0;
        pp->adapter.port = PORT_MII;
        break;
    }

    // SFP module type detection without SFP EEPROM.
    // Note that this may be called before SFP EEPROM I2C client is known.
    if (pp->medium_type == FLX_FRS_MEDIUM_SFP &&
        !(pp->flags & FLX_FRS_SFP_EEPROM)) {
        // Based on presence of PHY and supported adapter speed.
        if (pp->sfp.phy.phydev) {
            if (pp->adapter.supported & SUPPORTED_1000baseT_Full)
                flx_frs_set_sfp(pp, FLX_FRS_SFP_1000BASET);
            else
                flx_frs_set_sfp(pp, FLX_FRS_SFP_100BASET);
        }
        else {
            if (pp->adapter.supported & SUPPORTED_1000baseT_Full)
                flx_frs_set_sfp(pp, FLX_FRS_SFP_1000BASEX);
            else
                flx_frs_set_sfp(pp, FLX_FRS_SFP_100BASEFX);
        }
    }

    // Get link status from adapter only when adapter supports it and
    // when there is no PHY.
    if (pp->ext_phy.phydev || pp->sfp.phy.phydev) {
        // Drop unsupported modes from PHY.
        uint32_t supported_mask = pp->adapter.supported;

        pp->adapter.ops.check_link = NULL;

        // SFP type affects only when SFP PHY is the only PHY.
        if (!(pp->flags & FLX_FRS_HAS_SEPARATE_SFP))
            supported_mask &= pp->sfp.supported;

        // In any case drop fiber if SFP is not fiber.
        if (!(pp->sfp.supported & SUPPORTED_FIBRE))
            supported_mask &= ~SUPPORTED_FIBRE;

        if (pp->ext_phy.phydev) {
            pp->ext_phy.phydev->supported =
                pp->ext_phy.orig_supported & supported_mask;
            pp->ext_phy.phydev->advertising = pp->ext_phy.phydev->supported;
        }

        if (pp->sfp.phy.phydev) {
            pp->sfp.phy.phydev->supported =
                pp->sfp.phy.orig_supported & supported_mask;
            pp->sfp.phy.phydev->advertising = pp->sfp.phy.phydev->supported;
        }
    }

    // Force adapter to readjust itself to current SFP module and link mode.
    if (pp->adapter.ops.setup)
        pp->adapter.ops.setup(pp);
    if (pp->adapter.ops.update_link)
        pp->adapter.ops.update_link(pp);

    return 0;
}

/**
 * Check whether given link mode is supported by adapter or not.
 * Does not take into account current SFP module nor PHY.
 * @param pp FRS port privates
 * @param link_mode Link mode whose usability to check.
 * @return True if link_mode is acceptable, false otherwise.
 */
bool flx_frs_is_supported_by_adapter(struct flx_frs_port_priv *pp,
                                     enum link_mode link_mode)
{
    switch (link_mode) {
    case LM_1000FULL:
        if (pp->adapter.supported & SUPPORTED_1000baseT_Full)
            return true;
        break;
    case LM_100FULL:
        if (pp->adapter.supported & SUPPORTED_100baseT_Full)
            return true;
        break;
    case LM_10FULL:
        if (pp->adapter.supported & SUPPORTED_10baseT_Full)
            return true;
        break;
    case LM_DOWN:
        return true;
    }

    return false;
}

/**
 * Determine best link mode supported by adapter and current SFP module.
 * @param pp Port private data.
 * @return A link mode supported by the adapter or LM_DOWN.
 */
enum link_mode flx_frs_best_adapter_link_mode(struct flx_frs_port_priv *pp)
{
    if (pp->adapter.supported & SUPPORTED_1000baseT_Full &&
        pp->sfp.supported & SUPPORTED_1000baseT_Full)
        return LM_1000FULL;
    else if (pp->adapter.supported & SUPPORTED_100baseT_Full &&
        pp->sfp.supported & SUPPORTED_100baseT_Full)
        return LM_100FULL;
    else if (pp->adapter.supported & SUPPORTED_10baseT_Full &&
        pp->sfp.supported & SUPPORTED_10baseT_Full)
        return LM_10FULL;
    return LM_DOWN;
}

/**
 * Cleanup FRS port adapter handling.
 * @param pp FRS port privates.
 */
int flx_frs_cleanup_adapter(struct flx_frs_port_priv *pp)
{
    pp->adapter.ops.check_link = NULL;
    pp->adapter.ops.update_link = NULL;

    return 0;
}
