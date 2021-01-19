/** @file flx_time_ioctl.h
 * Flexibilis time driver ioctl interface.
 */

/*

   Flexibilis time driver for Linux

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

#ifndef FLX_TIME_IOCTL_H
#define FLX_TIME_IOCTL_H

#define FLX_TIME_IOCTL_MAGIC 0xE5
// FLX time control device name
#define FLX_TIME_CTRL_DEV "/dev/flx_time0"

/**
 * Time interface types.
 */
enum flx_time_if_type {
    FLX_TIME_LOCAL_NCO,         ///< Local clock
    FLX_TIME_E1,                ///< E1 interface (for frequency input)
    FLX_TIME_ETHERNET,          ///< Ethernet interface
    FLX_TIME_PULSE_INPUT,       ///< pps_calibrate
    FLX_TIME_10MHZ,             ///< Pulse input, e.g. PPS
    FLX_TIME_SYNC,              ///< Pulse input, PPS
    FLX_TIME_LOCAL_CLOCK,       ///< Local oscillator
    FLX_TIME_GPS,               ///< GPS input
    FLX_TIME_IRIG,              ///< IRIG input
    FLX_TIME_PPS_GEN,           ///< Pulse output, e.g. PPS
    FLX_TIME_E1_GEN,            ///< Frequency output over E1 line
    FLX_TIME_PPS_HISTOGRAM,     ///< Histogram, e.g. PPS or 10 MHz
    FLX_TIME_TSENSE,            ///< Temperature sensor
    FLX_TIME_USER_INPUT0,       ///< User input, user_in(0)
    FLX_TIME_USER_INPUT1,       ///< User input, user_in(1)
    FLX_TIME_USER_INPUT2,       ///< User input, user_in(2)
    FLX_TIME_USER_INPUT3,       ///< User input, user_in(3)
    FLX_TIME_USER_INPUT4,       ///< User input, user_in(4)
    FLX_TIME_USER_INPUT5,       ///< User input, user_in(5)
};

// Time interface properties flags.
#define TIME_PROP_E1_GEN        0x00080 ///< Synhronous E1 output (@see flx_time_pps)
#define TIME_PROP_PPS_HISTOGRAM 0x00040 ///< supports histogram (@see flx_time_histogram)
#define TIME_PROP_PPS_GEN       0x00020 ///< supports pulse per second generation (@see flx_time_pps)
#define TIME_PROP_FREQ_ADJ      0x00010 ///< supports frequency adjust (@see flx_time_freq_adj)
#define TIME_PROP_CLOCK_ADJ     0x00008 ///< supports clock adjust (@see flx_time_clock_adj)
#define TIME_PROP_COUNTER       0x00004 ///< Supports counter output (@see flx_time_get_data)
#define TIME_PROP_TIMESTAMP     0x00002 ///< Supports timestamp output (@see flx_time_get_data)
#define TIME_PROP_ACTUAL_TIME   0x00001 ///< Supports actual time output (@see flx_time_get_data)
#define TIME_PROP_LED           0x10000 ///< Supports LED indicator (@see flx_time_get_data)
#define TIME_PROP_IO_MUX        0x20000 ///< Supports IO mux (@see flx_time_get_data)
#define TIME_PROP_IO_RS_SEL     0x40000 ///< Supports IO protocol selection between RS422 and RS232 (@see flx_time_get_data)
#define TIME_PROP_IO_INVERT     0x80000 ///< Supports invertion of input and output (@see flx_time_get_data)
#define TIME_PROP_INPUT_TERM   0x100000 ///< Supports input termination (@see flx_time_get_data)
#define TIME_PROP_OUTPUT_DELAY_COMP   0x200000  ///< Supports output delay compensation (@see flx_time_get_data)

/**
 * Time interface information.
 */
#define MAX_IF_NAME_LEN 32
struct flx_if_property {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    const struct flx_if_property *const next;   ///< for internal purposes
    char name[MAX_IF_NAME_LEN]; ///< Human readable interface name
    enum flx_time_if_type type; ///< Interface type
    uint32_t properties;        ///< Interface properties
};

/**
 * Time presentation.
 */
struct flx_time_type {
    uint64_t sec;               ///< seconds
    uint32_t nsec;              ///< nanoseconds
    uint16_t subnsec;           ///< subnanoseconds (nanoseconds<<16)
};

/**
 * NMEA 0183 Time presentation.
 */
struct nmea_time_type {
    char msg[82];               ///<
    uint8_t len;                ///< Actual length of the message
};

/**
 * IRIG-B Time presentation.
 */
struct irig_time_type {
    uint8_t sec;                ///< seconds
    uint8_t dsec;               ///< tens of seconds
    uint8_t min;                ///< minutes
    uint8_t dmin;               ///< tens of minutes
    uint8_t hour;               ///< hours
    uint8_t dhour;              ///< tens of hours

    uint8_t day;                ///< days
    uint8_t dday;               ///< tens of days
    uint8_t cday;               ///< hundred of days

    uint32_t cf;                ///< IRIG control function // 27 bits

    uint32_t sbs;               ///< IRIG Straight binary seconds // 17 bits
};

/**
 * Data transferred with flx_time_get.
 * @see flx_time_get
 */
struct flx_time_get_data {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    uint64_t counter;           ///< counter value
    ///< timestamp (NCO time)  when counter value has updated
    struct flx_time_type timestamp;
    ///< source time, e.g. time from Localclock
    union {
        ///< Actual time (coming from interface
        struct flx_time_type source_time;
        ///< irig time
        struct irig_time_type irig_time;
        ///< nmea time message
        struct nmea_time_type nmea_time;
    };
};

/**
 * Data transferred with flx_time_clock_adjust.
 * @see flx_time_clock_adjust
 */
struct flx_time_clock_adjust_data {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    int sign;                   ///< negative=adjust backward, positive=adjust forward
    struct flx_time_type adjust_time;   ///< amount of adjust
};

/**
 * Data transferred with flx_time_freq_adjust.
 * @see flx_time_clock_adjust
 */
struct flx_time_freq_adjust_data {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    int adjust;                 ///< frequency adjust
};

/**
 * Data transferred with flx_time_pps.
 * @see flx_time_pps
 */
struct flx_time_pps_data {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    uint32_t setting;           ///< How many pulses per second
};

/**
 * Data transferred with flx_time_get_histogram.
 * @see flx_time_histogram
 */
struct flx_time_histogram_adjust_data {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    uint32_t offset;
};

/**
 * Data transferred with flx_time_get_histogram.
 * @see flx_time_histogram
 */
struct flx_time_get_histogram_data {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    uint32_t last_value;
    uint32_t last_out_of_range;
#define IOCTL_HISTOGRAM_SIZE 256
    uint32_t data[IOCTL_HISTOGRAM_SIZE];        ///< How many pulses per second
    uint32_t value_count;
    uint32_t out_of_range_count;
};

enum led_state {
    off,
    on,
    blink,
};

/**
 * Data transferred with flx_time_led_ctrl.
 * @see flx_time_ctrl
 */
struct flx_time_led_ctrl {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    enum led_state time_led;
};

/**
 * Data transferred with flx_time_baudrate_ctrl.
 * @see flx_time_ctrl
 */
struct flx_time_baud_rate_ctrl {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    uint32_t baudrate_divisor;
};

enum termination_enum {
    R100k,
    R50,
};

enum io_selection_enum {
    io_rs232,
    io_rs422,                   // Physically same interface as rs232, just different protcol
    io_ttl,
};

/**
 * Data transferred with FLX_TIME_IOCTL_SET_IO
 * @see flx_time_ctrl
 */
struct flx_time_io_ctrl {
    ///< interface index from 0 to N (number of interfaces - 1)
    uint32_t index;
    uint8_t invert_input;
    uint8_t invert_output;
    enum io_selection_enum input_port;  //
    enum io_selection_enum output_port; // output selects between rs and ttl, input selects the correct rs mode
    enum termination_enum input_termination;
    /** delay compensation for output: <0 means output is given before event,
     * >0 indicates delay after the output is set.(ps)*/
    int32_t delay_comp_output;
};

// IOCTLS

// Get number of interfaces, output: number of interfaces
#define FLX_TIME_IOCTL_GET_IF_COUNT     _IOR(FLX_TIME_IOCTL_MAGIC, 40, uint32_t)
// Get interface properties, input: interface index, output interface property
#define FLX_TIME_IOCTL_GET_IF           _IOWR(FLX_TIME_IOCTL_MAGIC,41, struct flx_if_property)
// Get time interface data, input: interface index, output interface time data
#define FLX_TIME_IOCTL_GET_DATA         _IOWR(FLX_TIME_IOCTL_MAGIC,42, struct flx_time_get_data)
// Time adjust, input: time adjust
#define FLX_TIME_IOCTL_CLOCK_ADJUST     _IOW(FLX_TIME_IOCTL_MAGIC, 43, struct flx_time_clock_adjust_data)
// frequency adjust, input: frequency adjust in ppb
#define FLX_TIME_IOCTL_FREQ_ADJUST      _IOW(FLX_TIME_IOCTL_MAGIC, 44, struct flx_time_freq_adjust_data)
// frequency adjust, input: frequency adjust in ppb
#define FLX_TIME_IOCTL_SET_PPS_GEN      _IOW(FLX_TIME_IOCTL_MAGIC, 45, struct flx_time_pps_data)
// Read histogram memory
#define FLX_TIME_IOCTL_GET_HISTOGRAM    _IOWR(FLX_TIME_IOCTL_MAGIC,46, struct flx_time_get_histogram_data)
// Adjust histogram window
#define FLX_TIME_IOCTL_HISTOGRAM_ADJUST _IOW(FLX_TIME_IOCTL_MAGIC, 47, struct flx_time_histogram_adjust_data)
// Blocking Write IRIG interface data, input: interface index, irig time data
#define FLX_TIME_IOCTL_SET_IRIG_DATA    _IOW(FLX_TIME_IOCTL_MAGIC, 48, struct flx_time_get_data)
// Non-blocking write to NMEA interface, input: interface index, nmea time data
#define FLX_TIME_IOCTL_SEND_NMEA_DATA   _IOW(FLX_TIME_IOCTL_MAGIC, 49, struct flx_time_get_data)

// Write UART baud rate divisor to NMEA port
#define FLX_TIME_IOCTL_SET_BAUD_RATE    _IOW(FLX_TIME_IOCTL_MAGIC, 51, struct flx_time_baud_rate_ctrl)

// Control input and output ports, polarity, and input termination
#define FLX_TIME_IOCTL_SET_IO           _IOW(FLX_TIME_IOCTL_MAGIC, 53, struct flx_time_io_ctrl)

#endif
