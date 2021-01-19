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

#ifndef FLX_FRS_IFLIB_H
#define FLX_FRS_IFLIB_H

#include <linux/mii.h>

/// FRS ioctl request number
#define SIOCDEVFRSCMD (SIOCDEVPRIVATE+15)

/**
 * FRS netdevice ioctl commands.
 * To use the ioctl interface:
 * - create socket, e.g. socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL))
 * - create struct ifreq
 * - setup member ifrn_name to FRS netdevice name
 * - setup struct frs_ioctl_data within struct ifreq using provided helper,
 *   according to used FRS ioctl command
 * - call ioctl passing it the socket, SIOCDEVFRSCMD as request,
 *   and pointer to the struct ifreq
 * - examine return value, negative value indicates error
 * - some FRS commands return data in struct frs_ioctl_data
 */
enum frs_ioctl_cmd {
    FRS_PORT_READ,              ///< read FRS port register, uses mdio_data
    FRS_PORT_WRITE,             ///< write FRS port register, uses mdio_data
    FRS_SWITCH_READ,            ///< read FRS switch register, uses mdio_data
    FRS_SWITCH_WRITE,           ///< write FRS switch register, uses mdio_data
    FRS_PORT_NUM,               ///< get FRS port number, uses port_num
    FRS_MAC_TABLE_READ,         ///< read FRS MAC table, uses mac_table
    FRS_PORT_SET_FWD_STATE,     ///< set a port state,
                                ///< preserved over link mode changes,
                                ///< uses port_fwd_state
    FRS_AUX_DEV_ADD,            ///< create auxiliary netdevice, uses dev_info
    FRS_AUX_DEV_DEL,            ///< remove auxiliary netdevice
    FRS_AUX_PORT_ADD,           ///< add FRS port to additional netdevice,
                                ///< uses ifindex
    FRS_AUX_PORT_DEL,           ///< remove FRS port from additional netdevice,
                                ///< uses ifindex
    FRS_MAC_TABLE_CLEAR,        ///< clear MAC table entries of defined ports,
                                ///< uses port_mask
    FRS_SET_RX_DELAY,           ///< set RX (input) delay for PTP messages
    FRS_SET_TX_DELAY,           ///< set TX (output) delay for PTP messages
    FRS_SET_P2P_DELAY,          ///< set calculated P2P delay to be added to
                                ///< PTP Sync message correction field calc
};

/**
 * FRS MAC table entry.
 */
struct frs_mac_table_entry {
    unsigned int ifindex;               ///< FRS port network interface index
    uint8_t mac_address[ETH_ALEN];      ///< MAC address
};

/**
 * FRS MAC table read information.
 */
struct frs_mac_table {
    /**
     * Number of entries available on input and number of entries written
     * or would be written on output.
     */
    unsigned int count;

    /**
     * Place to store MAC table entries, or NULL to ask for number of
     * entries available.
     */
    struct frs_mac_table_entry *entries;
};

/**
 * FRS netdevice name
 */
struct frs_dev_info {
    char name[IFNAMSIZ];                ///< netdevice name to add or remove
};

/**
 * Allowed values for port forward state.
 */
enum frs_port_fwd_state_val {
    FRS_PORT_FWD_STATE_DISABLED,        ///< not forwarding, not learning
    FRS_PORT_FWD_STATE_LEARNING,        ///< learns MAC addresses
    FRS_PORT_FWD_STATE_FORWARDING,      ///< learns MAC addresses and forwards
    FRS_PORT_FWD_STATE_AUTO,            ///< forward state changes with link state
};

/**
 * FRS netdevice ioctl definitions.
 * This struct replaces member ifr_ifru in struct ifreq for FRS use.
 * Its size is limited, so pointers to other structs are used when needed.
 * Access contents through frs_ioctl_data helper function, defined below.
 */
struct frs_ioctl_data {
    enum frs_ioctl_cmd cmd;                     ///< command
    union {
        struct mii_ioctl_data mdio_data;        ///< read/write command data
        unsigned int port_num;                  ///< FRS port number
        struct frs_mac_table mac_table;         ///< FRS MAC table data
        enum frs_port_fwd_state_val port_fwd_state;     ///< port forward state
        struct frs_dev_info *dev_info;          ///< FRS device information
        unsigned int ifindex;                   ///< netdevice interface index
        unsigned int port_mask;                 ///< bitmask of ports, or
                                                ///< zero for single port
        unsigned int delay;                     ///< RX/TX/P2P delay
    };
};

// Helper functions for accessing struct frs_ioctl_data members

static inline struct frs_ioctl_data *frs_ioctl_data(struct ifreq *rq)
{
    return (struct frs_ioctl_data *) &rq->ifr_ifru;
}

static inline struct mii_ioctl_data *frs_mdio(struct ifreq *rq)
{
    return &((struct frs_ioctl_data *) &rq->ifr_ifru)->mdio_data;
}

static inline unsigned int *frs_port_num(struct ifreq *rq)
{
    return &((struct frs_ioctl_data *) &rq->ifr_ifru)->port_num;
}

static inline enum frs_ioctl_cmd *frs_ioctl_cmd(struct ifreq *rq)
{
    return &((struct frs_ioctl_data *) &rq->ifr_ifru)->cmd;
}

static inline struct frs_mac_table *frs_ioctl_mac_table(struct ifreq *rq)
{
    return &((struct frs_ioctl_data *) &rq->ifr_ifru)->mac_table;
}

#endif
