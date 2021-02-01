//===============================================================================================================
//
// ttyUart1 - real time linux kernel module for the mini UART on a Rasperry Pi
//
//==========================================================================================================
//
// Revision history:
// 2017-12-12   V1.00   Initial release 
//
//===============================================================================================================

#include <linux/fs.h>               // file stuff
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

#include "ttyUart1.h"

// #define DEBUG
// #define IRQDEBUG


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
static int ttyUart1_open(struct inode* inode, struct file* file);
static int ttyUart1_close(struct inode* inode, struct file* file);
static unsigned int ttyUart1_poll(struct file* file_ptr, poll_table* wait);
static ssize_t ttyUart1_read(struct file* file_ptr, char __user* user_buffer, 
                              size_t count, loff_t* offset);
static ssize_t ttyUart1_write(struct file* file_ptr, 
                              const char __user* user_buffer, 
                              size_t count, loff_t* offset);
static long ttyUart1_ioctl(struct file* fp, unsigned int cmd, unsigned long arg);
static unsigned int getRaspiModel(void);

static int init_gpio(bool enable);
static void set_gpio_mode(unsigned int Gpio, unsigned int Function);
static void set_gpio_pullupdown(unsigned int Gpio, unsigned int pud);

static void do_irq_rx(void);
static void do_irq_tx(void);
static unsigned int send_data_to_tx_fifo(void);

static void uartEnable(bool enable);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bear");
MODULE_DESCRIPTION("Kernel module for the minu UART");
MODULE_VERSION("1.00");

static char connectBVP[] = "BVP";
static char connectBSP[] = "BSP";
static char *connect = connectBSP;
module_param(connect, charp, S_IRUGO);
MODULE_PARM_DESC(connect, " Connect " DEVICE_NAME " to 'BSP' or 'BVP'");

// file operations with this kernel module
static struct file_operations ttyUart1_fops = {
	.owner          = THIS_MODULE,
	.open           = ttyUart1_open,
	.release        = ttyUart1_close,
	.poll           = ttyUart1_poll,
	.read           = ttyUart1_read,
	.write          = ttyUart1_write,
	.unlocked_ioctl = ttyUart1_ioctl
};

static struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &ttyUart1_fops,
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
 * 
 */ 
static inline void delay(int32_t count) {
	asm volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
			: "=r"(count): [count]"0"(count) : "cc");
}

/**
 * 
 */ 
static irqreturn_t ttyUart1_irq_handler(int irq, void* dev_id) {
	unsigned int IntMask;
	unsigned int IntStatus;
	unsigned int NumBytes;

	P_IRQ_DEBUG("IRQ %d called, RxHead=%d, RxTail=%d, TxHead=%d, TxTail=%d\n", 
					IrqCounter, RxHead, RxTail, TxHead, TxTail); 

	IntStatus = ioread32(AUX_MU_IIR_REG) & UART_IIR_ID;

	// Receiver
	if (IntStatus & UART_IIR_ID_RX) {
		do_irq_rx();
	}

	// Transmitter
	if (IntStatus & UART_IIR_ID_TX) {
		do_irq_tx();
	}

	P_IRQ_DEBUG("IRQ %d exit. RxHead=%d, RxTail=%d, TxHead=%d, TxTail=%d\n", 
					IrqCounter++, RxHead, RxTail, TxHead, TxTail);

	return IRQ_HANDLED;
}

/**
 * 
 */ 
void do_irq_rx(void) {
	unsigned int DataWord;
	unsigned int RxNext;
	unsigned int Counter = FIFO_RX_SIZE;

	do {
		DataWord = ioread32(AUX_MU_IO_REG);

		spin_lock(&SpinLock);
		RxNext = RxHead + 1;
		if (RxNext >= RX_BUFF_SIZE)
			RxNext = 0;
		
		if (RxNext != RxTail) {
			RxBuff[RxHead] = DataWord;
			RxHead = RxNext;
			P_IRQ_DEBUG("IRQ: One byte received. RxHead=%d, RxTail=%d", 
							RxHead, RxTail);
		} else {
			// buffer overrun. do nothing. just discard the data.
			// TODO if someone needs to know, we can throw an error here
			P_IRQ_DEBUG("IRQ: Buffer overrun. RxHead=%d, RxTail=%d", 
							RxHead, RxTail);
		}
		spin_unlock(&SpinLock);

		if (Counter-- == 0) {
			break;
		}	
	} while (ioread32(AUX_MU_LSR_REG) & UART_LSR_DATA_READY);

	wake_up(&WaitQueue);
}

/** 
 * 
 */ 
void do_irq_tx(void) {
	unsigned int IntMask;

	P_IRQ_DEBUG("IRQ: Transmitting  byte. TxHead=%d, TxTail=%d\n", 
					TxHead, TxTail);

	spin_lock(&SpinLock);
	TxWork = send_data_to_tx_fifo();
	if (TxWork == 0) {
		IntMask = ioread32(AUX_MU_IER_REG);
		iowrite32(IntMask & ~UART_IER_TX_INT_ENABLE, AUX_MU_IER_REG);

		P_IRQ_DEBUG("IRQ: Stopping Tx Interrupt. TxHead=%d, TxTail=%d\n", 
						TxHead, TxTail);
	}
	spin_unlock(&SpinLock);
}


/**
 * 
 */ 
unsigned int send_data_to_tx_fifo(void) {
	unsigned int DataWord;
	unsigned int Count = 0;

	while (	(TxTail < TxHead) && 
			(ioread32(AUX_MU_STAT_REG) & UART_STAT_TX_SPACE_AVL)) {
		DataWord = TxBuff[TxTail++];
    	iowrite32(DataWord, AUX_MU_IO_REG);
		Count++;
	}

	P_IRQ_DEBUG(KERN_WARNING DEVICE_NAME " : send %d bytes\n", Count);
	return Count;
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
    } else if (strcmp(connect, connectBSP) == 0) {
        gpioTx = 14;
        gpioRx = 15;
        function = GPIO_ALT_5;
    } else if (strcmp(connect, connectBVP) == 0) {
        gpioTx = 32;
        gpioRx = 33;
        function = GPIO_ALT_5;
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
static unsigned int ttyUart1_poll(struct file* file_ptr, poll_table* wait) {
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
 * 
 */ 
static ssize_t ttyUart1_read(struct file* file_ptr, 
    char __user* user_buffer, size_t Count, loff_t* offset) {
  
    unsigned int NumBytes;
    unsigned int result;
    unsigned long Flags;
    unsigned long jiffies;
    enum { BUFFER_SIZE = 512 };
    char buffer[BUFFER_SIZE];

    P_DEBUG("Read request with offset=%d and count=%u\n", 
                (int)*offset, (unsigned int)Count);

    jiffies = usecs_to_jiffies(RW_MAX_DELAY_US);
    result = wait_event_timeout(WaitQueue, RxTail != RxHead, jiffies);

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
 * 
 */ 
static ssize_t ttyUart1_write(struct file* file_ptr, 
    const char __user* user_buffer, size_t Count, loff_t* offset) {

    unsigned int result;
    int Timeout;
    unsigned long Flags;
    unsigned int DataWord;
    unsigned int IntMask;

    P_DEBUG("Write request with offset=%d and count=%u\n", 
            	(int)*offset, (unsigned int)Count);

    P_IRQ_DEBUG("Write request. TxHead=%d, TxTail=%d\n", 
                	TxHead, TxTail);

    Timeout = 1;
    while (TxWork > 0) {
        if (--Timeout < 0) {
			printk(KERN_WARNING DEVICE_NAME " : Device is busy\n");
        	return -EBUSY;
		}
        udelay(RW_MAX_DELAY_US);
    }

    if (Count > TX_BUFF_SIZE) {
        Count = TX_BUFF_SIZE;
	}

    result = copy_from_user(TxBuff, user_buffer, Count);
    if (result > 0) {
		P_DEBUG("%d bytes not copied\n", result);
        return -EFAULT;
    }

    spin_lock_irqsave(&SpinLock, Flags);
	TxTail = 0;
    TxHead = Count;
	TxWork = send_data_to_tx_fifo();

    IntMask = ioread32(AUX_MU_IER_REG);
    iowrite32(IntMask | UART_IER_TX_INT_ENABLE, AUX_MU_IER_REG);
    spin_unlock_irqrestore(&SpinLock, Flags);

    P_DEBUG("Write exit with %d bytes written\n", Count);

    return Count;
}
    
    
/**
 * 
 */ 
static int ttyUart1_open(struct inode* inode, struct file* file) {
    unsigned int UartCtrl;
    unsigned int UartEnable;
    unsigned int baudreg;

    P_DEBUG("Open at at major %d  minor %d\n", imajor(inode), iminor(inode));

    if (DeviceOpen)
        return -EBUSY;
    DeviceOpen++;

    RxTail = RxHead = 0;
    TxTail = TxHead = TX_BUFF_SIZE;

    // Disable receive & transfer part of UART
    UartCtrl = ioread32(AUX_MU_CNTL_REG);
    UartCtrl &= ~(UART_CNTL_RX_ENABLE | UART_CNTL_TX_ENABLE);
    iowrite32(UartCtrl, AUX_MU_CNTL_REG);

    // Reset DLAB bit
    UartCtrl = ioread32(AUX_MU_LCR_REG);
    iowrite32(UartCtrl & ~UART_LCR_DLAB_ACCESS, AUX_MU_LCR_REG);

    // Set Baudrate
    baudreg = (CLOCK / (8 * BAUD_RATE)) - 1;
    iowrite32(baudreg, AUX_MU_BAUD);
    
    // Clear FIFO
    iowrite32(UART_IIR_FIFO_RX_CLR | UART_IIR_FIFO_TX_CLR, AUX_MU_IIR_REG);

    // 8-bit mode
    iowrite32(UART_LCR_DATA_SIZE, AUX_MU_LCR_REG);

    // Enable receiver interrupt
    iowrite32(UART_IER_RX_INT_ENABLE, AUX_MU_IER_REG);

    // Enable receive & transfer part of UART
    UartCtrl = UART_CNTL_RX_ENABLE | UART_CNTL_TX_ENABLE;
    iowrite32(UartCtrl, AUX_MU_CNTL_REG);

    P_DEBUG("Open exit\n");

    return 0;
}


// =============================================================================
static int ttyUart1_close(struct inode *inode, struct file *file) {
    unsigned int UartCtrl;
    unsigned int UartEnable;

    P_DEBUG("Close at at major %d  minor %d\n", imajor(inode), iminor(inode));

    DeviceOpen--;

    // Disable UART1
    iowrite32(UART_IIR_FIFO_RX_CLR | UART_IIR_FIFO_TX_CLR, AUX_MU_IIR_REG);
    UartCtrl = ioread32(AUX_MU_CNTL_REG);
    UartCtrl &= ~(UART_CNTL_RX_ENABLE | UART_CNTL_TX_ENABLE);
    iowrite32(UartCtrl, AUX_MU_CNTL_REG);

    P_DEBUG("ttyUart1: Close exit\n");

    return 0;
}


// =============================================================================
static long ttyUart1_ioctl(struct file *fp, unsigned int cmd, 
                            unsigned long arg) {

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


// =============================================================================
unsigned int getRaspiModel(void) {
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


// =============================================================================
int ttyUart1_register(void) {
	int result;
	unsigned int PeriBase;
	unsigned int UartIrq;

    P_DEBUG("register_device() is called\n");

    model = getRaspiModel();
    if (model < 3 || model > 4) {
        printk(KERN_NOTICE DEVICE_NAME " : Unknown RASPI model %d\n", model);
        result = -EFAULT;
        goto err_model;
    }
    printk(KERN_NOTICE DEVICE_NAME " : Found RASPI model %d\n", model);

    MajorNumber = register_chrdev(0, DEVICE_NAME, &ttyUart1_fops);
    if (MajorNumber < 0) {
        printk(KERN_WARNING DEVICE_NAME 
				" : can\'t register character device with errorcode = %i\n", MajorNumber);
        result = MajorNumber;
        goto err_model;
    }

    P_DEBUG("registered character device with major number = %i and minor numbers 0...255\n", MajorNumber);

    result = misc_register(&misc);
    if (result) {
        unregister_chrdev(MajorNumber, DEVICE_NAME);
        printk(KERN_ALERT DEVICE_NAME " : Failed to create the device\n");
        goto err_misc;
    }

    PeriBase = (model == 3) ?  RASPI_3_PERI_BASE : RASPI_4_PERI_BASE;
    GpioAddr = ioremap(PeriBase + GPIO_BASE, SZ_4K);
    UartAddr = ioremap(PeriBase + UART1_BASE, SZ_4K);

    if (init_gpio(true) != 0) {
        printk(KERN_ALERT "ttyUart0: Invalid value of parameter 'connect': %s\n", connect);
        result = -EINVAL;
        goto err_gpio;
    }

    init_waitqueue_head(&WaitQueue);

    spin_lock_init(&SpinLock);

    UartIrq = RASPI_UART1_IRQ;
    if (model == 4) {
        result = request_irq(UartIrq, ttyUart1_irq_handler, IRQF_SHARED, 
								"ttyUart1_irq_handler", DEVICE_NAME);
    } else {
        result = request_irq(UartIrq, ttyUart1_irq_handler, 0, 
								"ttyUart1_irq_handler", NULL);
    }

    if (result) {
        unregister_chrdev(MajorNumber, DEVICE_NAME);
        printk(KERN_ALERT DEVICE_NAME " : Failed to request IRQ %d\n", UartIrq);
        goto err_irq;
    }
    printk(KERN_INFO DEVICE_NAME " : Successfully requested IRQ %d\n", UartIrq);

    DeviceOpen = 0;

    uartEnable(true);

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
err_model:

    return result;
}
    
    
// =============================================================================
void ttyUart1_unregister(void) {
    unsigned int UartIrq;

    printk(KERN_NOTICE DEVICE_NAME " : unregister_device()");
    
    uartEnable(false);
    init_gpio(false);

    if (GpioAddr)
        iounmap(GpioAddr);
    GpioAddr = 0;  

    if (UartAddr)
        iounmap(UartAddr);  
    UartAddr = 0;

    UartIrq = RASPI_UART1_IRQ;
    free_irq(UartIrq, NULL);

    misc_deregister(&misc);
    unregister_chrdev(MajorNumber, DEVICE_NAME);

    MajorNumber = 0;
}

// ============================================================================= 
void uartEnable(bool enable) {
    unsigned int UartEnable;
    unsigned int UartCntl;

    UartEnable = ioread32(AUXENB);

    UartCntl = ioread32(AUX_MU_CNTL_REG);
    UartCntl &= ~(UART_CNTL_RX_ENABLE | UART_CNTL_TX_ENABLE);

    if (enable) {
        iowrite32(UartEnable | UART_AUXENB_ENABLE, AUXENB);
        iowrite32(UartCntl, AUX_MU_CNTL_REG);
    } else {
        iowrite32(UartCntl, AUX_MU_CNTL_REG);
        iowrite32(UartEnable & ~UART_AUXENB_ENABLE, AUXENB);
    }
}

module_init(ttyUart1_register);
module_exit(ttyUart1_unregister);
