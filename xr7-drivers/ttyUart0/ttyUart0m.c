//
//  ttyUart0 - real time linux kernel module for the PL011 UART on a Rasperry Pi
//
//  Revision history:
//      2021-02-04      V1.00   Initial release
//

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

#include "ttyUart0.h"

// #define DEBUG
// #define IRQDEBUG

// connects the Tx output to the Rx input of the UART. For testing only.
// #define LOOPBACK 1

#ifdef DEBUG
#define P_DEBUG(fmt...)	printk(KERN_NOTICE DEVICE_NAME ": " fmt)
#else
#define P_DEBUG(fmt...)	do { } while (0)
#endif

#ifdef IRQDEBUG
#define P_IRQ_DEBUG(fmt...)	printk(KERN_NOTICE DEVICE_NAME ": " fmt)
#else
#define P_IRQ_DEBUG(fmt...)	do { } while (0)
#endif

// prototypes
static int ttyUart0_open(struct inode* inode, struct file* file);
static int ttyUart0_close(struct inode* inode, struct file* file);
static unsigned int ttyUart0_poll(struct file* file_ptr, poll_table* wait);
static ssize_t ttyUart0_read(struct file* file_ptr, char __user* user_buffer,
                                size_t count, loff_t* offset);
static ssize_t ttyUart0_write(struct file* file_ptr,
                                const char __user* user_buffer,
                                size_t count, loff_t* offset);
static long ttyUart0_ioctl(struct file* fp, unsigned int cmd,
                            unsigned long arg);
static unsigned int getRaspiModel(void);

static int init_gpio(bool enable);
static void set_gpio_mode(unsigned int Gpio, unsigned int Function);
static void set_gpio_pullupdown(unsigned int Gpio, unsigned int pud);

static void do_irq_rx(void);
static void do_irq_tx(void);
static unsigned int send_data_to_tx_fifo(void);

#define DEVICE_NAME         "ttyUart0"           // The device will appear at /dev/ttyUart0
#define BAUD_RATE 38400

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bear");
MODULE_DESCRIPTION("Kernel module for the PL011 UART");
MODULE_VERSION("1.00");

static char connectBVP[] = "BVP";
static char connectBSP[] = "BSP";
static char *connect = connectBVP;
module_param(connect, charp, S_IRUGO);
MODULE_PARM_DESC(connect, " Connect " DEVICE_NAME " to 'BSP' or 'BVP'");


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

static unsigned int model;
static unsigned int MajorNumber;
static void* GpioAddr;
static void* UartAddr;
static unsigned int DeviceOpen;
static wait_queue_head_t WaitQueue;
static spinlock_t SpinLock;

// ring buffer used for receiving data
enum { RX_BUFF_SIZE = 32 };
static volatile unsigned int RxTail = 0;
static volatile unsigned int RxHead = 0;
static unsigned int RxBuff[RX_BUFF_SIZE];

// linear buffer used for transmitting data
enum { TX_BUFF_SIZE = 32 };
static volatile unsigned int TxTail = TX_BUFF_SIZE;
static volatile unsigned int TxHead = TX_BUFF_SIZE;
static unsigned char TxBuff[TX_BUFF_SIZE];
static unsigned int TxWork = 0;

#ifdef IRQDEBUG
static int IrqCounter = 0;
#endif

/**
 * @brief delay
 * @param count
 */
static inline void delay(int32_t count)
{
	asm volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
	    : "=r"(count): [count]"0"(count) : "cc");
}

/**
 * @brief ttyUart0_irq_handler
 * @param irq
 * @param dev_id
 * @return
 */
static irqreturn_t ttyUart0_irq_handler(int irq, void* dev_id)
{
    unsigned int IntStatus;

    P_IRQ_DEBUG("IRQ %d called, RxHead=%d, RxTail=%d, TxHead=%d, TxTail=%d\n",
                    IrqCounter, RxHead, RxTail, TxHead, TxTail);

    IntStatus = ioread32(UART_INT_STAT);

    // Receiver
    if (IntStatus & INT_RX) {
        do_irq_rx();
    }

    // Transmitter
    if (IntStatus & INT_TX) {
        do_irq_tx();
    }

    P_IRQ_DEBUG("IRQ %d exit. RxHead=%d, RxTail=%d, TxHead=%d, TxTail=%d\n",
                IrqCounter++, RxHead, RxTail, TxHead, TxTail);

    return IRQ_HANDLED;
}


/**
 * @brief do_irq_rx
 */
void do_irq_rx(void)
{
    unsigned int DataWord;
    unsigned int RxNext;

    iowrite32(INT_RX, UART_INT_CLR);

    DataWord = ioread32(UART_DATA);

    spin_lock(&SpinLock);
    RxNext = RxHead + 1;
    if (RxNext >= RX_BUFF_SIZE)
        RxNext = 0;

    if (RxNext != RxTail) {
        RxBuff[RxHead] = DataWord;
        RxHead = RxNext;
        P_IRQ_DEBUG("IRQ: One byte received. RxHead=%d, RxTail=%d\n",
                    RxHead, RxTail);
    }

    else {
        // buffer overrun. do nothing. just discard the data.
        // TODO if someone needs to know, we can throw an error here
        P_IRQ_DEBUG("ttyUart0: IRQ: Buffer overrun. RxHead=%d, RxTail=%d\n",
                    RxHead, RxTail);
    }
    spin_unlock(&SpinLock);

    iowrite32(0, UART_RX_ERR);

    wake_up(&WaitQueue);
}


/**
 *
 */
void do_irq_tx(void)
{
    unsigned int IntMask;
    unsigned int DataWord;

    iowrite32(INT_TX, UART_INT_CLR);

    spin_lock(&SpinLock);
    if (TxTail < TxHead) {
        P_IRQ_DEBUG("IRQ: Transmitting one byte. TxHead=%d, TxTail=%d\n",
                    TxHead, TxTail);

        DataWord = TxBuff[TxTail++];
        iowrite32(DataWord, UART_DATA);
    }
    else {
        P_IRQ_DEBUG("IRQ: Stopping Tx Interrupt. TxHead=%d, TxTail=%d\n",
                    TxHead, TxTail);

        IntMask = ioread32(UART_INT_MASK);
        iowrite32(IntMask & ~INT_TX, UART_INT_MASK);
    }
    spin_unlock(&SpinLock);
}


/**
 * @brief init_gpio
 * @param enable
 * @return
 */
int init_gpio(bool enable)
{
    static unsigned int gpioTx = 0;
    static unsigned int gpioRx = 0;

    unsigned int function = 0;
    unsigned int pull = GPIO_PULL_UP;

    if (!enable) {
        pull = GPIO_PULL_OFF;
        function = GPIO_INPUT;
    } else if (strcmp(connect, connectBSP) == 0) {
        gpioTx = 14;
        gpioRx = 15;
        function = GPIO_ALT_0;
    } else if (strcmp(connect, connectBVP) == 0) {
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

    printk(KERN_NOTICE DEVICE_NAME ": Connect to %s\n", connect);

    return 0;
}


/**
 * @brief set_gpio_mode
 * @param Gpio
 * @param Function
 */
void set_gpio_mode(unsigned int Gpio, unsigned int Function)
{
    unsigned int RegOffset = (Gpio / 10) << 2;
    unsigned int Bit = (Gpio % 10) * 3;

    volatile unsigned int Value = ioread32(GpioAddr + RegOffset);

    Value = (Value & ~(0x7 << Bit)) | ((Function & 0x7) << Bit);
    iowrite32(Value, GpioAddr + RegOffset);
}


/**
 * @brief set_gpio_pullupdown
 * @param Gpio
 * @param pud
 */
void set_gpio_pullupdown(unsigned int Gpio, unsigned int pud)
{
    iowrite32(pud, GPIO_PULL);
    delay(150);     // provide hold time for the control signal

    iowrite32(GPIO_BIT, GPIO_PULLCLK0 + GPIO_BANK);
    delay(150);     // provide hold time for the control signal

    iowrite32(0, GPIO_PULL);
    iowrite32(0, GPIO_PULLCLK0 + GPIO_BANK);
}

/**
 * @brief ttyUart0_poll
 * @param file_ptr
 * @param wait
 * @return
 */
static unsigned int ttyUart0_poll(struct file* file_ptr, poll_table* wait)
{
    P_DEBUG("Poll request\n");

    poll_wait(file_ptr, &WaitQueue, wait);
    if (RxTail != RxHead) {
        P_DEBUG("Poll succeeded. RxHead=%d, RxTail=%d\n", RxHead, RxTail);
        return POLLIN | POLLRDNORM;
    }

    P_DEBUG("Poll timeout\n");
    return 0;
}


/**
 * @brief ttyUart0_read
 * @param file_ptr
 * @param user_buffer
 * @param Count
 * @param offset
 * @return
 */
static ssize_t ttyUart0_read(struct file* file_ptr,
                             char __user* user_buffer,
                             size_t Count,
                             loff_t* offset)
{
    unsigned int NumBytes;
    unsigned int result;
    unsigned long Flags;
    enum { BUFFER_SIZE = 512 };
    char buffer[BUFFER_SIZE];

    P_DEBUG("Read request with offset=%d and count=%u\n",
            (int)*offset, (unsigned int)Count);

    result = wait_event_timeout(WaitQueue, RxTail != RxHead, usecs_to_jiffies(10));
    if (result == 0) {
        printk(KERN_WARNING DEVICE_NAME " : Read timeout\n");
		return -EBUSY; // timeout
    }

    P_IRQ_DEBUG("Read event. RxHead=%d, RxTail=%d\n", RxHead, RxTail);

    NumBytes = 0;
    spin_lock_irqsave(&SpinLock, Flags);
    while (RxTail != RxHead && NumBytes < Count) {
        buffer[NumBytes++] = RxBuff[RxTail++];
        if (RxTail >= RX_BUFF_SIZE)
            RxTail = 0;
    }
    spin_unlock_irqrestore(&SpinLock, Flags);

    if (copy_to_user(user_buffer, buffer, Count) != 0)
        return -EFAULT;

    P_DEBUG("Read exit with %d bytes read\n", NumBytes);

    return NumBytes;
}
    

/**
 * @brief ttyUart0_write
 * @param file_ptr
 * @param user_buffer
 * @param Count
 * @param offset
 * @return
 */
static ssize_t ttyUart0_write(struct file* file_ptr,
                              const char __user* user_buffer,
                              size_t Count,
                              loff_t* offset)
{
    unsigned int result;
    int Timeout;
    unsigned long Flags;
    unsigned int DataWord;
    unsigned int IntMask;
    unsigned int FreeSize;

    P_DEBUG("Write request with offset=%d and count=%u\n",
            (int)*offset, (unsigned int)Count);

    P_IRQ_DEBUG("Write request. TxHead=%d, TxTail=%d\n",
                TxHead, TxTail);

    Timeout = 1;
    while (TxTail < TxHead) {
        if (--Timeout < 0) {
            break;
        }
        udelay(RW_MAX_DELAY_US);
    }

    FreeSize = (TxHead >= TxTail) ? TX_BUFF_SIZE - TxHead + TxTail - 1 :
                                    TxTail - TxHead - 1;

    if (Count > FreeSize)
        Count = FreeSize;

    if (Count > FreeSize) {
        result = copy_from_user(TxBuff, user_buffer, Count);
        if (result > 0) {
            P_DEBUG("%d bytes not copied\n", result);
            return -EFAULT;
        }

        spin_lock_irqsave(&SpinLock, Flags);
        DataWord = TxBuff[0];
        TxTail = 1;
        TxHead = Count;
        iowrite32(DataWord, UART_DATA);

        IntMask = ioread32(UART_INT_MASK);
        iowrite32(IntMask | INT_TX, UART_INT_MASK);
        spin_unlock_irqrestore(&SpinLock, Flags);
    } else {
        P_DEBUG("Transmitter buffer free size %d, tx bytes %d\n",
                FreeSize, Count);
    }

    P_DEBUG("Write exit with %d bytes written\n", Count);

    return Count;        // the number of bytes actually transmitted
}


/**
 * @brief ttyUart0_open
 * @param inode
 * @param file
 * @return
 */
static int ttyUart0_open(struct inode* inode, struct file* file)
{
    unsigned int UartCtrl;

    P_DEBUG("Open at at major %d  minor %d\n", imajor(inode), iminor(inode));

	if (DeviceOpen)
		return -EBUSY;
	DeviceOpen++;

	// Disable UART0
	iowrite32(0, UART_CTRL);

    RxTail = RxHead = 0;
    TxTail = TxHead = TX_BUFF_SIZE;

	iowrite32(0x7FF, UART_INT_CLR);

    // Set Baudrate
	iowrite32(3000000 / BAUD_RATE, UART_INT_BAUD);
	iowrite32(0, UART_FRAC_BAUD);
 
	// Disable FIFO & 8 bit (1 stop bit, no parity)
	iowrite32(UART_LCR_8_BITS, UART_LINE_CTRL);

    // Enable receiver interrupt
    iowrite32(INT_RX, UART_INT_MASK);

    // Read data register to clear overflow error bit.
    // In addition, clear any other receiver error
    ioread32(UART_DATA);
    iowrite32(0, UART_RX_ERR);

	// Enable UART0, receive & transfer part of UART
    UartCtrl = UARTCR_UART_ENABLE | UARTCR_TX_ENABLE |
            UARTCR_RX_ENABLE | UARTCR_RTS;
#ifdef LOOPBACK
    UartCtrl |= UARTCR_LOOPBACK;
#endif
	iowrite32(UartCtrl, UART_CTRL);

    P_DEBUG("Open exit\n");

	return 0;
}


/**
 * @brief ttyUart0_close
 * @param inode
 * @param file
 * @return
 */
static int ttyUart0_close(struct inode *inode, struct file *file)
{
    P_DEBUG("Close at major %d  minor %d\n", imajor(inode), iminor(inode));

	DeviceOpen--;

	// Disable UART0
	iowrite32(0, UART_CTRL);

    P_DEBUG("Close exit\n");

	return 0;
}


/**
 * @brief ttyUart0_ioctl
 * @param fp
 * @param cmd
 * @param arg
 * @return
 */
static long ttyUart0_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
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

        ret = put_user(numbytes, (unsigned int __user *) arg);
    default:
        break;    
    }

    return ret;
}


/**
 * @brief getRaspiModel
 * @return
 */
unsigned int getRaspiModel(void)
{
    struct file* filp = NULL;
    char buf[32];
    unsigned int NumBytes = 0;
    unsigned int Version = 0;

    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);

    filp = filp_open("/sys/firmware/devicetree/base/model", O_RDONLY, 0);
    if (filp == NULL) {
        set_fs(old_fs);
        return 0;
    }

    NumBytes = filp->f_op->read(filp, buf, sizeof(buf), &filp->f_pos);
    set_fs(old_fs);

    filp_close(filp, NULL);

    if ((NumBytes >= 29) && (buf[28] == '3')) {
        Version = 3;
    } else if (NumBytes >= 13) {
        switch(buf[12]) {
        case '3' : Version = 3; break;
        case '4' : Version = 4; break;
        }
    }

    return Version;
}


/**
 * @brief ttyUart0_register
 * @return
 */
int ttyUart0_register(void)
{
    int result;
    unsigned int PeriBase;
    unsigned int UartIrq;

    P_DEBUG("register_device() is called\n");

    model = getRaspiModel();
    if (model < 1 || model > 4) {
        printk(KERN_NOTICE DEVICE_NAME " : Unknown RASPI model %d\n", model);
        result = -EFAULT;
        goto err_model;
    }
    printk(KERN_NOTICE DEVICE_NAME " : Found RASPI model %d\n", model);

    MajorNumber = register_chrdev(0, DEVICE_NAME, &ttyUart0_fops);
    if (MajorNumber < 0) {
        printk(KERN_WARNING DEVICE_NAME
               " : can\'t register character device with errorcode = %i\n",
               MajorNumber);
        result = MajorNumber;
        goto err_model;
    }

    P_DEBUG("registered character device with major number = %i and minor numbers 0...255\n", MajorNumber);

    result = misc_register(&misc);
    if (result) {
        printk(KERN_ALERT DEVICE_NAME " : Failed to create the device\n");
        goto err_misc;
    }

    PeriBase = (model == 1) ? RASPI_1_PERI_BASE :
                              (model == 4) ? RASPI_4_PERI_BASE :
                                             RASPI_23_PERI_BASE;
    GpioAddr = ioremap(PeriBase + GPIO_BASE, SZ_4K);
    UartAddr = ioremap(PeriBase + UART0_BASE, SZ_4K);

    if (init_gpio(true) != 0) {
        printk(KERN_ALERT DEVICE_NAME " : Invalid value of parameter 'connect': %s\n", connect);
        result = -EINVAL;
        goto err_gpio;
    }

    init_waitqueue_head(&WaitQueue);

    spin_lock_init(&SpinLock);

    UartIrq = (model == 1) ? RASPI_1_UART_IRQ :
                             (model == 4) ? RASPI_4_UART_IRQ :
                                            RASPI_23_UART_IRQ;
    if (model == 4)
        result = request_irq(UartIrq, ttyUart0_irq_handler, IRQF_SHARED,
                             "ttyUart0_irq_handler", DEVICE_NAME);
    else
        result = request_irq(UartIrq, ttyUart0_irq_handler, 0,
                             "ttyUart0_irq_handler", NULL);
    
    if (result) {
        printk(KERN_ALERT DEVICE_NAME " : Failed to request IRQ %d\n", UartIrq);
        goto err_irq;
    }
    printk(KERN_INFO DEVICE_NAME " : Successfully requested IRQ %d\n", UartIrq);

    DeviceOpen = 0;

    P_DEBUG("device created correctly\n");

    return 0;

err_irq:
    init_gpio(false);
err_gpio:
    iounmap(GpioAddr);
    iounmap(UartAddr);
    misc_deregister(&misc);
err_misc:
    unregister_chrdev(MajorNumber, DEVICE_NAME);
    MajorNumber = 0;
err_model:

    return result;
}
    

/**
 * @brief ttyUart0_unregister
 */
void ttyUart0_unregister(void)
{
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

    UartIrq = (model == 1) ? RASPI_1_UART_IRQ :
                             (model == 4) ? RASPI_4_UART_IRQ :
                                            RASPI_23_UART_IRQ;
    free_irq(UartIrq, NULL);

    misc_deregister(&misc);
    unregister_chrdev(MajorNumber, DEVICE_NAME);

    MajorNumber = 0;
}

module_init(ttyUart0_register);
module_exit(ttyUart0_unregister);
