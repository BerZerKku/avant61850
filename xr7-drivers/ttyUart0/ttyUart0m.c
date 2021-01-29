//===============================================================================================================
//
// ttyUart0 - real time linux kernel module for the ebusd using the PL011 UART on a Rasperry Pi
//
// Copyright (C) 2017 Galileo53 <galileo53@gmx.at>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// This is a LINUX kernel module exclusively for the ebusd using the PL011 UART running on Raspberry Pi 1 to 4.
// The latency between receiving and transmitting of a character is nearly zero. This is achieved by
// disabling the hardware FIFO at the UART completely and using a ring-buffer managed at the interrupt
// handler of the "Receiver Holding Register" interrupt.
//
// With RASPI 1 to 3 we are replacing the original interrupt of ttyAMA0. With RASPI 4 the interrupt is shared between
// all 5 UARTs and so the interrupt of ttyUart0 is added to the shared list. Note that interrupt numbers in Raspbian
// (Debian) are re-ordered by some Linux-internal logic, so we must always see what interrupt is assigned at a specific
// RASPI / Raspbian version. This can be done by executing "cat /proc/interrupts" while ttyAMA0 is still active.
//
// Btw., all procedures to dynamically get the interrupt number, as described in the literature, like polling all
// possible interrupts, have failed until now.
//
//===============================================================================================================
//
// Revision history:
// 2017-12-12   V1.1    Initial release
// 2017-12-18   V1.2    Added module description
// 2018-02-13   V1.3    Added more debug messages for IRQ read operations. Changed read timeout to 1 minute
// 2018-02-14   V1.4    Added poll to file operations
// 2018-03-21   V1.5    Fixed read buffer overrun issue
// 2019-06-16   V1.6    Changed IRQ for V4.19.42
// 2020-01-08   V1.7    Added support for RASPI4
// 2020-07-25	V1.8	Corrected set_fs(KERNEL_DS) for kernel 5.4
// 2021-01-07	V1.9p   Fix get the Rasperry Pi model number. Changed baud rate to 38400  
//
//===============================================================================================================

#include <linux/fs.h> 	            // file stuff
#include <linux/kernel.h>           // printk()
#include <linux/errno.h>            // error codes
#include <linux/module.h>           // THIS_MODULE
#include <linux/delay.h>            // udelay
#include <linux/interrupt.h>        // request_irq
#include <linux/miscdevice.h>       // misc_register
#include <linux/io.h>               // ioremap
#include <linux/spinlock.h>         // spinlocks
#include <linux/wait.h>             // poll
#include <linux/poll.h>             // poll
#include <asm/uaccess.h>            // copy_to_user

#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/version.h>
#include <uapi/asm-generic/ioctls.h>

// #define DEBUG 1                  // if uncommented, will write some debug messages to /var/log/kern.log

#ifdef DEBUG
#define IRQDEBUG 1               // if uncommented, writes messages from the interrupt handler too (there are a lot of messages!)
#endif

// #define LOOPBACK 1               // if uncommented, connects the Tx output to the Rx input of the UART. For testing only.

// prototypes
static int ttyUart0_open(struct inode* inode, struct file* file);
static int ttyUart0_close(struct inode* inode, struct file* file);
static unsigned int ttyUart0_poll(struct file* file_ptr, poll_table* wait);
static ssize_t ttyUart0_read(struct file* file_ptr, char __user* user_buffer, size_t count, loff_t* offset);
static ssize_t ttyUart0_write(struct file* file_ptr, const char __user* user_buffer, size_t count, loff_t* offset);
static long ttyUart0_ioctl(struct file* fp, unsigned int cmd, unsigned long arg);

static int init_gpio(bool enable);
static void set_gpio_mode(unsigned int Gpio, unsigned int Function);
static void set_gpio_pullupdown(unsigned int Gpio, unsigned int pud);

#define DEVICE_NAME         "ttyUart0"           // The device will appear at /dev/ttyUart0
#define BAUD_RATE 38400

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Galileo53");
MODULE_DESCRIPTION("Kernel module for the ebusd directly connected through the PL011 UART to the eBus adapter");
MODULE_VERSION("1.9p");

static char ConnectToBVP[] = "BVP";
static char ConnectToBSP[] = "BSP";
static char *ConnectTo = ConnectToBSP;
module_param(ConnectTo, charp, 0444);
MODULE_PARM_DESC(ConnectTo, " Connect " DEVICE_NAME " to 'BSP' or 'BVP'");


// file operations with this kernel module
static struct file_operations ttyUart0_fops =
    {
    .owner          = THIS_MODULE,
    .open           = ttyUart0_open,
    .release        = ttyUart0_close,
    .poll           = ttyUart0_poll,
    .read           = ttyUart0_read,
    .write          = ttyUart0_write,
    .unlocked_ioctl = ttyUart0_ioctl
    };

static struct miscdevice misc =
    {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &ttyUart0_fops,
    .mode = S_IRUSR |   // User read
            S_IWUSR |   // User write
            S_IRGRP |   // Group read
            S_IWGRP |   // Group write
            S_IROTH |   // Other read
            S_IWOTH     // Other write
    };

static unsigned int RaspiModel;
static unsigned int MajorNumber;
static void* GpioAddr;
static void* UartAddr;
static unsigned int DeviceOpen;
static wait_queue_head_t WaitQueue;
static spinlock_t SpinLock;

// ring buffer used for receiving data
enum { RX_BUFF_SIZE = 128 };
static volatile unsigned int RxTail = 0;
static volatile unsigned int RxHead = 0;
static unsigned int RxBuff[RX_BUFF_SIZE];

// linear buffer used for transmitting data
enum { TX_BUFF_SIZE = 64 };
static volatile unsigned int TxTail = TX_BUFF_SIZE;
static volatile unsigned int TxHead = TX_BUFF_SIZE;
static unsigned char TxBuff[TX_BUFF_SIZE];

#ifdef IRQDEBUG
static int IrqCounter = 0;
#endif


// RASPI I/O Base address
// ======================
#define RASPI_1_PERI_BASE    0x20000000              // RASPI 1
#define RASPI_23_PERI_BASE   0x3F000000              // RASPI 2 and 3
#define RASPI_4_PERI_BASE    0xFE000000              // RASPI 4

// BCM2835 base address
// ====================
#define SYST_BASE            0x00003000
#define DMA_BASE             0x00007000
#define IRQ_BASE             0x0000B000
#define CLK_BASE             0x00101000
#define GPIO_BASE            0x00200000
#define UART0_BASE           0x00201000
#define PCM_BASE             0x00203000
#define SPI0_BASE            0x00204000
#define I2C0_BASE            0x00205000
#define PWM_BASE             0x0020C000
#define UART1_BASE           0x00215000
#define I2C1_BASE            0x00804000
#define I2C2_BASE            0x00805000
#define DMA15_BASE           0x00E05000

// GPIO register
// =============
#define GPIO_INPUT          0
#define GPIO_OUTPUT         1
#define GPIO_ALT_0          4
#define GPIO_ALT_1          5
#define GPIO_ALT_2          6
#define GPIO_ALT_3          7
#define GPIO_ALT_4          3
#define GPIO_ALT_5          2
 
#define GPIO_FSEL0          (GpioAddr+0x00)
#define GPIO_FSEL1          (GpioAddr+0x04)
#define GPIO_FSEL2          (GpioAddr+0x08)
#define GPIO_FSEL3          (GpioAddr+0x0C)
#define GPIO_FSEL4          (GpioAddr+0x10)
#define GPIO_FSEL5          (GpioAddr+0x14)

#define GPIO_PULL           (GpioAddr+0x94)                       // Pull up/pull down
#define GPIO_PULLCLK0       (GpioAddr+0x98)                       // Pull up/pull down clock
#define GPIO_PULLCLK1       (GpioAddr+0x9C)                       // Pull up/pull down clock
#define GPIO_BANK           (Gpio >> 5)
#define GPIO_BIT            (1 << (Gpio & 0x1F))


#define GPIO_PULL_OFF       0
#define GPIO_PULL_DOWN      1
#define GPIO_PULL_UP        2

// The UART interrupt on model B+ is allocated to 81.
// The UART interrupt on RASPI2,3 is allocated to 87, beginning with kernel 4.19.42, it is allocated to 81.
// The UART interrupt on PASPI2,3 is allocated to 83 (kernel 3.18.11)
// For RASPI 4, the interrupt is 34 and is shared with all other UARTs.
#define RASPI_1_UART_IRQ       81
#define RASPI_4_UART_IRQ       34
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0)
#define RASPI_23_UART_IRQ      83
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,19,42)
#define RASPI_23_UART_IRQ      87
#else
#define RASPI_23_UART_IRQ      81
#endif

// PL011 UART register (16C650 type)
// =================================
#define UART_DATA         (UartAddr+0x00)
#define UART_RX_ERR       (UartAddr+0x04)
#define UART_FLAG         (UartAddr+0x18)
#define UART_ILPR         (UartAddr+0x20)
#define UART_INT_BAUD     (UartAddr+0x24)
#define UART_FRAC_BAUD    (UartAddr+0x28)
#define UART_LINE_CTRL    (UartAddr+0x2C)
#define UART_CTRL         (UartAddr+0x30)
#define UART_FIFO_LEVEL   (UartAddr+0x34)
#define UART_INT_MASK     (UartAddr+0x38)
#define UART_RAW_INT      (UartAddr+0x3C)
#define UART_INT_STAT     (UartAddr+0x40)
#define UART_INT_CLR      (UartAddr+0x44)
#define UART_DMA_CTRL     (UartAddr+0x48)
#define UART_TEST_CTRL    (UartAddr+0x80)
#define UART_TEST_IN      (UartAddr+0x84)
#define UART_IEST_OUT     (UartAddr+0x88)
#define UART_TEST_DATA    (UartAddr+0x8C)
#define UART_MEM_SIZE     0xC0

// UART_FLAG register
// ==================
#define UART_RX_FIFO_EMPTY (1 << 4)
#define UART_TX_FIFO_FULL  (1 << 5)

// UART Line Control Register
// ==========================
#define UART_LCR_BREAK          (1 << 0)
#define UART_LCR_PARITY_EN      (1 << 1)
#define UART_LCR_EVEN_PARITY    (1 << 2)
#define UART_LCR_2_STOP         (1 << 3)
#define UART_LCR_FIFO_EN        (1 << 4)
#define UART_LCR_8_BITS         (3 << 5)
#define UART_LCR_STICK_PARITY   (1 << 7)

// UART Control Register
// ======================
#define UARTCR_UART_ENABLE      (1 << 0)
#define UARTCR_LOOPBACK         (1 << 7)
#define UARTCR_TX_ENABLE        (1 << 8)
#define UARTCR_RX_ENABLE        (1 << 9)
#define UARTCR_RTS              (1 << 11)

// UART Interrupt masks
// ====================
#define INT_CTS                 (1 << 1)
#define INT_RX                  (1 << 4)
#define INT_TX                  (1 << 5)
#define INT_RX_TIMEOUT          (1 << 6)
#define INT_FRAMING_ERR         (1 << 7)
#define INT_PARITY_ERR          (1 << 8)
#define INT_BREAK_ERR           (1 << 9)
#define INT_OVER_ERR            (1 << 10)



// ===============================================================================================
//
//                                    delay
//
// ===============================================================================================
static inline void delay(int32_t count) {
	asm volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
	    : "=r"(count): [count]"0"(count) : "cc");
}

// ===============================================================================================
//
//                                    ttyUart0_irq_handler
//
// ===============================================================================================
//
// Parameter:
//
// Returns:
//
// Description:
//      Fired on interrupt. If data is in the receiver holding register, transfer it to the ring
//      buffer. If transmitter holding register has become empty, fill it with another data from
//      the linear buffer.
//
// ===============================================================================================
static irqreturn_t ttyUart0_irq_handler(int irq, void* dev_id) {
    unsigned int IntStatus;
    unsigned int DataWord;
    unsigned int IntMask;
    unsigned int RxNext;

#ifdef IRQDEBUG
    printk(KERN_NOTICE "ttyUart0: IRQ %d called. RxHead=%d, RxTail=%d, TxHead=%d, TxTail=%d\n", IrqCounter, RxHead, RxTail, TxHead, TxTail);
#endif

    IntStatus = ioread32(UART_INT_STAT);

    if (IntStatus & INT_RX) {
        // clear the interrupt
        // ===================
        iowrite32(INT_RX, UART_INT_CLR);

        // data was received and is available in the receiver holding register
        // ===================================================================
        DataWord = ioread32(UART_DATA);

        // see if the buffer will be full after this interrupt
        // ===================================================
        spin_lock(&SpinLock);
        RxNext = RxHead + 1;
        if (RxNext >= RX_BUFF_SIZE)
            RxNext = 0;

        if (RxNext != RxTail) {
            // data was received and is available in the receiver holding register
            // ===================================================================
            RxBuff[RxHead] = DataWord;
            RxHead = RxNext;
#ifdef IRQDEBUG
            printk(KERN_NOTICE "ttyUart0: IRQ: One byte received. RxHead=%d, RxTail=%d\n", RxHead, RxTail);
#endif
        }

        else {
            // buffer overrun. do nothing. just discard the data.
            // eventually todo: if someone needs to know, we can throw an error here
            // =====================================================================
#ifdef IRQDEBUG
            printk(KERN_NOTICE "ttyUart0: IRQ: Buffer overrun. RxHead=%d, RxTail=%d\n", RxHead, RxTail);
#endif
        }
        spin_unlock(&SpinLock);

        // clear any receiver error
        // ========================
        iowrite32(0, UART_RX_ERR);

        // if the calling task is waiting, wake him up. If there is no task at all, this is a NOP
        // ======================================================================================
        wake_up(&WaitQueue);
    }

    // Transmitter
    // ===========
    if (IntStatus & INT_TX) {
        // clear the interrupt
        // ===================
        iowrite32(INT_TX, UART_INT_CLR);

        // The transmitter holding register has become empty.
        // see if some more data available
        // ==================================================
        spin_lock(&SpinLock);
        if (TxTail < TxHead) {
#ifdef IRQDEBUG
            printk(KERN_NOTICE "ttyUart0: IRQ: Transmitting one byte. TxHead=%d, TxTail=%d\n", TxHead, TxTail);
#endif
            // fill the transmitter holding register with new data
            // ===================================================
            DataWord = TxBuff[TxTail++];
            iowrite32(DataWord, UART_DATA);
            // (do nothing with the interrupt line - keep the INT_TX active)
        }
        else {
#ifdef IRQDEBUG
            printk(KERN_NOTICE "ttyUart0: IRQ: Stopping Tx Interrupt. TxHead=%d, TxTail=%d\n", TxHead, TxTail);
#endif
            // no more data in the transmit buffer. disable the TX interrupt
            // =============================================================
            IntMask = ioread32(UART_INT_MASK);
            iowrite32(IntMask & ~INT_TX, UART_INT_MASK);
        }
        spin_unlock(&SpinLock);
    }

#ifdef IRQDEBUG
    printk(KERN_NOTICE "ttyUart0: IRQ %d exit. RxHead=%d, RxTail=%d, TxHead=%d, TxTail=%d\n", IrqCounter, RxHead, RxTail, TxHead, TxTail);
    IrqCounter++;
#endif

    return IRQ_HANDLED;
}


// ===============================================================================================
//
//                                    ttyUart0_set_gpio_mode
//
// ===============================================================================================
//
// Parameter:
//      Gpio                Number of the GPIO port
//      Function            one of GPIO_INPUT, GPIO_OUTPUT, GPIO_ALT_0, etc.
//
// Returns:
//
// Description:
//      Set the mode for the GPIO port. Especially in this program GPIO_ALT_0 at port 14, 15 will
//      connect the ports to the UART Rx and Tx
//
// ===============================================================================================
static void ttyUart0_set_gpio_mode(unsigned int Gpio, unsigned int Function) {
    unsigned int RegOffset = (Gpio / 10) << 2;
    unsigned int Bit = (Gpio % 10) * 3;
    volatile unsigned int Value = ioread32(GpioAddr + RegOffset);
    iowrite32((Value & ~(0x7 << Bit)) | ((Function & 0x7) << Bit), GpioAddr + RegOffset);
}


// ===============================================================================================
//
//                                    ttyUart0_gpio_pullupdown
//
// ===============================================================================================
//
// Parameter:
//      Gpio                Number of the GPIO port
//      pud                 one of GPIO_PULL_OFF, GPIO_PULL_DOWN or GPIO_PULL_UP
//
// Returns:
//
// Description:
//      Set the pull-up or pull-down at the specified GPIO port
//
// ===============================================================================================
void ttyUart0_gpio_pullupdown(unsigned int Gpio, unsigned int pud) {
    // fill the new value for pull up or down
    // ======================================
    iowrite32(pud, GPIO_PULL);
    delay(150);     // provide hold time for the control signal

    // transfer the new value to the GPIO pin
    // ======================================
    iowrite32(GPIO_BIT, GPIO_PULLCLK0 + GPIO_BANK);
    delay(150);     // provide hold time for the control signal

    // remove the control signal to make it happen
    // ===========================================
    iowrite32(0, GPIO_PULL);
    iowrite32(0, GPIO_PULLCLK0 + GPIO_BANK);
}


// ===============================================================================================
//
//                                    ttyUart0_poll
//
// ===============================================================================================
//
// Parameter:
//      file_ptr            Pointer to the open file
//      wait                Timeout structure
//
// Returns:
//      POLLIN              Data is available
//
// Description:
//      Probe the receiver if some data available. Return after timeout anyway.
//
// ===============================================================================================
static unsigned int ttyUart0_poll(struct file* file_ptr, poll_table* wait) {
#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: Poll request");
#endif

    poll_wait(file_ptr, &WaitQueue, wait);
    if (RxTail != RxHead) {
#ifdef DEBUG
        printk(KERN_NOTICE "ttyUart0: Poll succeeded. RxHead=%d, RxTail=%d\n", RxHead, RxTail);
#endif
        return POLLIN | POLLRDNORM;
    }
    else {
#ifdef DEBUG
        printk(KERN_NOTICE "ttyUart0: Poll timeout");
#endif
        return 0;
    }
}


// ===============================================================================================
//
//                                    ttyUart0_read
//
// ===============================================================================================
//
// Parameter:
//      file_ptr            Pointer to the open file
//      user_buffer         Buffer in user space where to receive the data
//      Count               Number of bytes to read
//      offset              Pointer to a counter that can hold an offset when reading chunks
//
// Returns:
//      Number of bytes read
//
// Description:
//      Called when a process, which already opened the dev file, attempts to read from it, like
//      "cat /dev/ttyUart0"
//
// ===============================================================================================
static ssize_t ttyUart0_read(struct file* file_ptr, char __user* user_buffer, size_t Count, loff_t* offset) {
    unsigned int NumBytes;
    unsigned int result;
    unsigned long Flags;
    enum { BUFFER_SIZE = 512 };
    char buffer[BUFFER_SIZE];

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: Read request with offset=%d and count=%u\n", (int)*offset, (unsigned int)Count);
#endif

    // wait until a character is received or if timeout (1 min) occurs.
    // note that wait_event_timeout is a macro that will already avoid the race condition that may
    // happen where new data arrives between testing (RxTail != RxHead) and effective sleeping of this task.
    // =====================================================================================================
    result = wait_event_timeout(WaitQueue, RxTail != RxHead, usecs_to_jiffies(10));
    if (result == 0) {
#ifdef DEBUG
        printk(KERN_NOTICE "ttyUart0: Read timeout");
#endif
		return -EBUSY; // timeout
    }

#ifdef IRQDEBUG
    printk(KERN_NOTICE "ttyUart0: Read event. RxHead=%d, RxTail=%d\n", RxHead, RxTail);
#endif

    // collect all bytes received so far from the receive buffer
    // we must convert from a ring buffer to a linear buffer
    // =========================================================
    NumBytes = 0;
    spin_lock_irqsave(&SpinLock, Flags);
    while (RxTail != RxHead && NumBytes < Count) {
        buffer[NumBytes++] = RxBuff[RxTail++];
        if (RxTail >= RX_BUFF_SIZE)
            RxTail = 0;
    }
    spin_unlock_irqrestore(&SpinLock, Flags);

    // copying data to user space requires a special function to be called
    // ===================================================================
    if (copy_to_user(user_buffer, buffer, Count) != 0)
        return -EFAULT;

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: Read exit with %d bytes read\n", NumBytes);
#endif

    return NumBytes;        // the number of bytes actually received
}
    
    
// ===============================================================================================
//
//                                    ttyUart0_write
//
// ===============================================================================================
//
// Parameter:
//      file_ptr            Pointer to the open file
//      user_buffer         Buffer in user space where to receive the data
//      Count               Number of bytes to write
//      offset              Pointer to a counter that can hold an offset when writing chunks
//
// Returns:
//      Number of bytes written
//
// Description:
//      Called when a process, which already opened the dev file, attempts to write to it, like
//      "echo "hello" > /dev/ttyUart0"
//
// ===============================================================================================
static ssize_t ttyUart0_write(struct file* file_ptr, const char __user* user_buffer, size_t Count, loff_t* offset) {
    int result;
    int Timeout;
    unsigned long Flags;
    unsigned int DataWord;
    unsigned int IntMask;

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: Write request with offset=%d and count=%u\n", (int)*offset, (unsigned int)Count);
#endif
#ifdef IRQDEBUG
    printk(KERN_NOTICE "ttyUart0: Write request. TxHead=%d, TxTail=%d\n", TxHead, TxTail);
#endif

    // if transmission is still in progress, wait until done
    // =====================================================
    Timeout = 1;
    while (TxTail < TxHead)
        {
        if (--Timeout < 0)
            return -EBUSY;
        udelay(10);
        }

    // copying data from user space requires a special function to be called
    // =====================================================================
    if (Count > TX_BUFF_SIZE)
        Count = TX_BUFF_SIZE;
    result = copy_from_user(TxBuff, user_buffer, Count);
    if (result > 0)             // not all requested bytes copied
        Count = result;         // nuber of bytes copied
    else if (result != 0)
        return -EFAULT;

    // Fill the first character directly to the hardware, the rest will be
    // fetched by the interrupt handler upon handling the TX interrupt
    // ===================================================================
    spin_lock_irqsave(&SpinLock, Flags);
    DataWord = TxBuff[0];
    TxTail = 1;
    TxHead = Count;
    iowrite32(DataWord, UART_DATA);

    // enable the TX interrupt. will be asserted when the transmitter holding becomes empty
    // ====================================================================================
    IntMask = ioread32(UART_INT_MASK);
    iowrite32(IntMask | INT_TX, UART_INT_MASK);
    spin_unlock_irqrestore(&SpinLock, Flags);

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: Write exit with %d bytes written\n", Count);
#endif

    return Count;        // the number of bytes actually transmitted
}

/**
 * 
 */ 
void set_gpio_mode(unsigned int Gpio, unsigned int Function) {
    unsigned int RegOffset = (Gpio / 10) << 2;
    unsigned int Bit = (Gpio % 10) * 3;

    volatile unsigned int Value = ioread32(GpioAddr + RegOffset);

    Value = (Value & ~(0x7 << Bit)) | ((Function & 0x7) << Bit);
    iowrite32(Value, GpioAddr + RegOffset);
}

/**
 * 
 */ 
void set_gpio_pullupdown(unsigned int Gpio, unsigned int pud) {
    iowrite32(pud, GPIO_PULL);
    delay(150);     // provide hold time for the control signal

    iowrite32(GPIO_BIT, GPIO_PULLCLK0 + GPIO_BANK);
    delay(150);     // provide hold time for the control signal

    iowrite32(0, GPIO_PULL);
    iowrite32(0, GPIO_PULLCLK0 + GPIO_BANK);
}    

/**
 * 
 */ 
int init_gpio(bool enable) {
    static unsigned int gpioTx = 0;
    static unsigned int gpioRx = 0;

    unsigned int function = 0;
    unsigned int pull = GPIO_PULL_UP;

    if (!enable) {
        pull = GPIO_PULL_OFF;
        function = GPIO_INPUT;
    } else if (strcmp(ConnectTo, ConnectToBSP) == 0) {
        gpioTx = 14;
        gpioRx = 15;
        function = GPIO_ALT_0;
    } else if (strcmp(ConnectTo, ConnectToBVP) == 0) {
        gpioTx = 32;
        gpioRx = 33;
        function = GPIO_ALT_3;
    } else {
        return -1;
    }

    if (gpioTx != 0) {
        set_gpio_mode(gpioTx, function);
        set_gpio_pullupdown(gpioTx, pull);
    }

    if (gpioRx != 0) {
        set_gpio_mode(gpioRx, function);
        set_gpio_pullupdown(gpioRx, pull);
    }

    printk(KERN_NOTICE DEVICE_NAME " : Connect to %s\n", ConnectTo);

    return 0;
}

// ===============================================================================================
//
//                                    ttyUart0_open
//
// ===============================================================================================
//
// Parameter:
//
// Returns:
//
// Description:
//      Called when a process tries to open the device file, like "cat /dev/ttyUart0"
//
// ===============================================================================================
static int ttyUart0_open(struct inode* inode, struct file* file) {
    unsigned int UartCtrl;

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: Open at at major %d  minor %d\n", imajor(inode), iminor(inode));
#endif

    // do not allow another open if already open
    // =========================================
	if (DeviceOpen)
		return -EBUSY;
	DeviceOpen++;

	// Disable UART0
    // =============
	iowrite32(0, UART_CTRL);

    // reset the ring buffer and the linear buffer
    // ===========================================
    RxTail = RxHead = 0;
    TxTail = TxHead = TX_BUFF_SIZE;

	// Clear pending interrupts
    // ========================
	iowrite32(0x7FF, UART_INT_CLR);

    // Set integer & fractional part of baud rate to BAUD_RATE fixed. Divider = 3.000.000 / 2400
    // Fractional part = 0. For Raspi3 and upwards, UART default frequency is 48.000.000 and divided
    // by 16, which is again 3.000.000. It is assumed that the default values are not changed.
    // ===========================================================================================
	iowrite32(3000000 / BAUD_RATE, UART_INT_BAUD);
	iowrite32(0, UART_FRAC_BAUD);
 
	// Disable FIFO & 8 bit (1 stop bit, no parity)
    // ============================================
	iowrite32(UART_LCR_8_BITS, UART_LINE_CTRL);

    // Enable receiver interrupt
    // =========================
    iowrite32(INT_RX, UART_INT_MASK);

    // Read data register to clear overflow error bit. In addition, clear any other receiver error
    // =========================
    ioread32(UART_DATA);
    iowrite32(0, UART_RX_ERR);

	// Enable UART0, receive & transfer part of UART
    // =============================================
	UartCtrl = UARTCR_UART_ENABLE | UARTCR_TX_ENABLE | UARTCR_RX_ENABLE | UARTCR_RTS;
#ifdef LOOPBACK
    UartCtrl |= UARTCR_LOOPBACK;
#endif
	iowrite32(UartCtrl, UART_CTRL);

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: Open exit\n");
#endif

	return 0;
}


// ===============================================================================================
//
//                                    ttyUart0_close
//
// ===============================================================================================
//
// Parameter:
//
// Returns:
//
// Description:
//      Called when a process closes the device file.
//
// ===============================================================================================
static int ttyUart0_close(struct inode *inode, struct file *file) {
    printk(KERN_NOTICE "ttyUart0: Close at at major %d  minor %d\n", imajor(inode), iminor(inode));

	DeviceOpen--;

	// Disable UART0
    // =============
	iowrite32(0, UART_CTRL);

    printk(KERN_NOTICE "ttyUart0: Close exit");

	return 0;
}


// ===============================================================================================
//
//                                    ttyUart0_ioctl
//
// ===============================================================================================
//
// Parameter:
//
// Returns:
//      OK
//
// Description:
//      I/O control. Currently this does nothing. ebusd just calls it to see if the device
//      is working. So only return an OK status
//
// ===============================================================================================
static long ttyUart0_ioctl(struct file *fp, unsigned int cmd, unsigned long arg) {
    int numbytes;
    unsigned long Flags;
    int ret = 0; /* -ENOIOCTLCMD; */

    switch(cmd) {
    case TIOCINQ: 
        spin_lock_irqsave(&SpinLock, Flags);
        if (RxTail > RxHead) {
            numbytes = RxHead - RxTail + RX_BUFF_SIZE;
        } else {
            numbytes = RxHead - RxTail;
        }
        spin_unlock_irqrestore(&SpinLock, Flags);

        // if (numbytes > 0)    
        //     printk(KERN_NOTICE "ttyUart0: ttyUart0_ioctl, numbytes = %d\n", numbytes);

        ret = put_user(numbytes, (unsigned int __user *) arg);
    default:
        break;    
    }

    return ret;
}


// ===============================================================================================
//
//                                      ttyUart0_raspi_model
//
// ===============================================================================================
// Description:
//      Get the Rasperry Pi model number from /sys/firmware/devicetree/base/model. The string
//      has usually the form "Raspberry Pi 3 Model B Rev 1.2"
//      Extract the number and return it.
//
// ===============================================================================================
unsigned int ttyUart0_raspi_model(void) {
    struct file* filp = NULL;
    char buf[32];
    unsigned int NumBytes = 0;

    // get current segment descriptor, set segment descriptor
    // associated to kernel space
    // ======================================================
    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);

    // read the file
    // =============
    filp = filp_open("/sys/firmware/devicetree/base/model", O_RDONLY, 0);
    if (filp == NULL) {
        set_fs(old_fs);
        return 0;
    }
    NumBytes = filp->f_op->read(filp, buf, sizeof(buf), &filp->f_pos);
    set_fs(old_fs);

    // restore the segment descriptor
    // ==============================
    filp_close(filp, NULL);

    // interpret the data from the file
    // ================================
    if (NumBytes < 13)
        return 0;

    if ((NumBytes >= 29) && (buf[28] == '3'))
        return 3;

    switch(buf[12]) {
        case '2' : return 2; break;
        case '3' : return 3; break;
        case '4' : return 4; break;
    }

    return 1;
}


// ===============================================================================================
//
//                                    ttyUart0_register
//
// ===============================================================================================
//
// Parameter:
//
// Returns:
//      Major Number of the driver
//
// Description:
//      Register the device to the kernel by use of the register-chrdev(3) call. Since the first
//      parameter to this call is 0, the system will assign a Major Number by itself. A
//      device name is given and the file_operations structure is also passed to the kernel.
//
// ===============================================================================================
int ttyUart0_register(void) {
    int result;
    unsigned int PeriBase;
    unsigned int UartIrq;

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: register_device() is called\n");
#endif

    // Get the RASPI model
    // ===================
    RaspiModel = ttyUart0_raspi_model();
    if (RaspiModel < 1 || RaspiModel > 4) {
        printk(KERN_NOTICE "ttyUart0: Unknown RASPI model %d\n", RaspiModel);
        result = -EFAULT;
        goto err_model;
    }
    printk(KERN_NOTICE "ttyUart0: Found RASPI model %d\n", RaspiModel);

    // Dynamically allocate a major number for the device
    // ==================================================
    MajorNumber = register_chrdev(0, DEVICE_NAME, &ttyUart0_fops);
    if (MajorNumber < 0) {
        printk(KERN_WARNING "ttyUart0: can\'t register character device with errorcode = %i\n", MajorNumber);
        result = MajorNumber;
        goto err_model;
    }

#ifdef DEBUG
    printk(KERN_NOTICE "ttyUart0: registered character device with major number = %i and minor numbers 0...255\n", MajorNumber);
#endif

    // Register the device driver. We are using misc_register instead of
    // device_create so we are able to set the attributes to rw for everybody
    // ======================================================================
    result = misc_register(&misc);
    if (result) {
        unregister_chrdev(MajorNumber, DEVICE_NAME);
        printk(KERN_ALERT "ttyUart0: Failed to create the device\n");
        goto err_misc;
    }

    // remap the I/O registers to some memory we can access later on
    // =============================================================
    PeriBase = (RaspiModel == 1) ? RASPI_1_PERI_BASE : (RaspiModel == 4) ? RASPI_4_PERI_BASE : RASPI_23_PERI_BASE;
    GpioAddr = ioremap(PeriBase + GPIO_BASE, SZ_4K);
    UartAddr = ioremap(PeriBase + UART0_BASE, SZ_4K);

    if (init_gpio(true) != 0) {
        printk(KERN_ALERT "ttyUart0: Invalid value of parameter 'ConnectTo': %s\n", ConnectTo);
        result = -EINVAL;
        goto err_gpio;
    }

    // set up a queue for waiting
    // ==========================
    init_waitqueue_head(&WaitQueue);

    // initialize the spinlock
    // =======================
    spin_lock_init(&SpinLock);

    // Install Interrupt Handler
    // =========================
    UartIrq = (RaspiModel == 1) ? RASPI_1_UART_IRQ : (RaspiModel == 4) ? RASPI_4_UART_IRQ : RASPI_23_UART_IRQ;
    if (RaspiModel == 4)
        result = request_irq(UartIrq, ttyUart0_irq_handler, IRQF_SHARED, "ttyUart0_irq_handler", DEVICE_NAME);
    else
        result = request_irq(UartIrq, ttyUart0_irq_handler, 0, "ttyUart0_irq_handler", NULL);
    
    if (result) {
        init_gpio(false);
        misc_deregister(&misc);
        unregister_chrdev(MajorNumber, DEVICE_NAME);
        printk(KERN_ALERT "ttyUart0: Failed to request IRQ %d\n", UartIrq);
        goto err_irq;
    }
    printk(KERN_INFO "ttyUart0: device %s (irq %d) created correctly\n", DEVICE_NAME, UartIrq);

    DeviceOpen = 0;
    return 0;

err_irq:
    init_gpio(false);
err_gpio:
    iounmap(GpioAddr);
    iounmap(UartAddr);
    misc_deregister(&misc);
err_misc:
    unregister_chrdev(MajorNumber, DEVICE_NAME);
err_model:

    return result;
    }
    
    
// ===============================================================================================
//
//                                    ttyUart0_unregister
//
// ===============================================================================================
// Parameter:
//
// Returns:
//
// Description:
//      Unmap the I/O, free the IRQ and unregister the device
//
// ===============================================================================================
void ttyUart0_unregister(void) {
    unsigned int UartIrq;

    printk(KERN_NOTICE "ttyUart0: unregister_device()\n");

    init_gpio(false);

    // release the mapping
    if (GpioAddr)
        iounmap(GpioAddr);
    if (UartAddr)
        iounmap(UartAddr);

    GpioAddr = 0;
    UartAddr = 0;

    UartIrq = (RaspiModel == 1) ? RASPI_1_UART_IRQ : (RaspiModel == 4) ? RASPI_4_UART_IRQ : RASPI_23_UART_IRQ;
    free_irq(UartIrq, NULL);

    misc_deregister(&misc);
    unregister_chrdev(MajorNumber, DEVICE_NAME);

    MajorNumber = 0;
}


// ===============================================================================================
//
//                                module_init()      module_exit()
//
// ===============================================================================================
// Description:
//        Before Linux 2.4, the init and cleanup functions have to be named init_module() and
//        cleanup_module() exactly.
//        As of Linux 2.4, there are two macros, module_init() and module_exit() defined in
//        linux/init.h that allow us to use any name for the init and cleanup we want.
//        Note that the functions must be defined *before* calling the macros, otherwise you'll
//        get compilation errors.
//
// ===============================================================================================
module_init(ttyUart0_register);
module_exit(ttyUart0_unregister);
