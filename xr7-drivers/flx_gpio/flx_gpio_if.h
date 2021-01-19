/** @file
 * @brief Flexibilis general purpose I/O
 */

/*

   Flexibilis general purpose I/O Linux driver

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

#ifndef FLX_GPIO_IF_H
#define FLX_GPIO_IF_H

#define FLX_GPIO_CONFIG_BASE    0x1000  ///< Config and status registers

/// Register for a given output
#define FLX_GPIO_CONFIG_REG(output) \
    (FLX_GPIO_CONFIG_BASE + (((output) / 8u) * 4u))

/// Register for a given input
#define FLX_GPIO_INPUT_STATUS_REG(input) \
    (FLX_GPIO_CONFIG_REG(input) + 2)

#define FLX_GPIO_VALUE          0x1u    ///< Bits for GPIO config/status value
#define FLX_GPIO_OUT_DIR        0x2u    ///< Bits for setting GPIO direction
#define FLX_GPIO_OUT_BITS(value) \
    (FLX_GPIO_OUT_DIR | !!(value))                      ///< Bits for setting GPIO as output
#define FLX_GPIO_OUT_LOW        FLX_GPIO_OUT_BITS(0)    ///< Bits for setting GPIO as output low
#define FLX_GPIO_OUT_HIGH       FLX_GPIO_OUT_BITS(1)    ///< Bits for setting GPIO as output high
#define FLX_GPIO_SHIFT(gpio)    (((gpio) & 0x7u) << 1)  ///< How many bits to shift for given GPIO
#define FLX_GPIO_MASK           FLX_GPIO_OUT_HIGH       ///< Bits for a single GPIO config/status

#endif
