/** @file
 */

/*

   XRS identification Linux driver

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

#ifndef FLX_XRS_IF_H
#define FLX_XRS_IF_H

/*
 * Device Identification Registers
 */
#define XRS_REG_DEV_ID0                 0x0000  ///< XRS DEV_ID0 register
#define XRS_DEV_ID0_MASK                0xff01  ///< mask for relevant bits
#define XRS_DEV_ID0_XRS7003E            0x0100  ///< value for XRS7003E
#define XRS_DEV_ID0_XRS7003F            0x0101  ///< value for XRS7003F
#define XRS_DEV_ID0_XRS7004E            0x0200  ///< value for XRS7004E
#define XRS_DEV_ID0_XRS7004F            0x0201  ///< value for XRS7004F
#define XRS_DEV_ID0_XRS3003E            0x0300  ///< value for XRS3003E
#define XRS_DEV_ID0_XRS3003F            0x0301  ///< value for XRS3003F
#define XRS_DEV_ID0_XRS5003E            0x0400  ///< value for XRS5003E
#define XRS_DEV_ID0_XRS5003F            0x0401  ///< value for XRS5003F
#define XRS_DEV_ID0_XRS7103E            0x0500  ///< value for XRS7103E
#define XRS_DEV_ID0_XRS7103F            0x0501  ///< value for XRS7103F
#define XRS_DEV_ID0_XRS7104E            0x0600  ///< value for XRS7104E
#define XRS_DEV_ID0_XRS7104F            0x0601  ///< value for XRS7104F

#define XRS_REG_DEV_ID1                 0x0002  ///< XRS DEV_ID1 register
#define XRS_DEV_ID1_XRS                 0x0040  ///< value for XRS

#define XRS_REG_REV_ID                  0x0008  ///< XRS REV_ID register
#define XRS_REV_ID_MINOR_MASK           0xff    ///< Minor number mask
#define XRS_REV_ID_MINOR_OFFSET         0       ///< Minor number offset
#define XRS_REV_ID_MAJOR_MASK           0xff    ///< Major number mask
#define XRS_REV_ID_MAJOR_OFFSET         8       ///< Major number offset

#define XRS_REG_INTERNAL_REV_ID0        0x4     ///< Internal revision LSBs
#define XRS_REG_INTERNAL_REV_ID1        0x6     ///< Internal revision MSBs

#endif
