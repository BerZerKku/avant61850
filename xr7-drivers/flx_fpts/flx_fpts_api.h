/** @file
 */

/*

   Flexibilis PPx Time Stamper Linux driver

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

#ifndef FLX_FPTS_API_H
#define FLX_FPTS_API_H

/// Magic ioctl number
#define FLX_FPTS_IOCTL_MAGIC 0xf8

/**
 * This file defines the user space API.
 * It is used by both the driver and by user space software
 * using this driver.
 */

/**
 * Event information structure for read system call.
 */
struct flx_fpts_event {
    uint64_t sec;               ///< time stamp seconds part
    uint32_t nsec;              ///< time stamp nanoseconds part,
                                ///< always between 0 and 999999999, inclusive
    uint32_t counter;           ///< total event count
};

/**
 * Modes of operation
 */
enum flx_fpts_mode {
    FLX_FPTS_MODE_INTERRUPT,    ///< interrupt driven
                                ///< (default if interrupt has been defined)
    FLX_FPTS_MODE_POLL,         ///< polling mode with predefined interval,
                                ///< (default if interrupt is not available)
    FLX_FPTS_MODE_DIRECT,       ///< direct mode, read triggers check,
                                ///< returns always EAGAIN error if no events
};

/**
 * Settings information structure for ioctl system call.
 */
struct flx_fpts_settings {
    enum flx_fpts_mode mode;            ///< mode of operation
    struct timespec poll_interval;      ///< poll interval for polling mode
};

/// Set settings ioctl
#define FLX_FPTS_IOCTL_SET_SETTINGS \
    _IOW(FLX_FPTS_IOCTL_MAGIC, 0, struct flx_fpts_settings)

#endif
