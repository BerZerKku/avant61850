# ifndef TTY_UART1_H__
# define TTY_UART1_H__

// The device will appear at /dev/$DEVICE_NAME
#define DEVICE_NAME "ttyUart1"           
#define BAUD_RATE 38400
#define RW_MAX_DELAY_US 10

// RASPI mini UART defines
// =======================
#define FIFO_RX_SIZE 8
#define FIFO_TX_SIZE 8

// RASPI I/O Base address 
// ======================
#define RASPI_3_PERI_BASE    0x3F000000         
#define RASPI_4_PERI_BASE    0xFE000000              

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

#define GPIO_PULL           (GpioAddr+0x94)  // Pull up/pull down
#define GPIO_PULLCLK0       (GpioAddr+0x98)  // Pull up/pull down clock
#define GPIO_PULLCLK1       (GpioAddr+0x9C)  // Pull up/pull down clock
#define GPIO_BANK           (Gpio >> 5)
#define GPIO_BIT            (1 << (Gpio & 0x1F))

#define GPIO_PULL_OFF       0
#define GPIO_PULL_DOWN      1
#define GPIO_PULL_UP        2

#define CLOCK 250000000UL

#define RASPI_UART1_IRQ 29

// 16550 mini UART register (16C650 type)
// ======================================
#define AUXIRQ            (UartAddr+0x00)
#define AUXENB            (UartAddr+0x04)
#define AUX_MU_IO_REG     (UartAddr+0x40)
#define AUX_MU_IER_REG    (UartAddr+0x44)
#define AUX_MU_IIR_REG    (UartAddr+0x48)
#define AUX_MU_LCR_REG    (UartAddr+0x4C)
#define AUX_MU_MCR_REG    (UartAddr+0x50)
#define AUX_MU_LSR_REG    (UartAddr+0x54)
#define AUX_MU_MSR_REG    (UartAddr+0x58)
#define AUX_MU_SCRATCH    (UartAddr+0x5C)
#define AUX_MU_CNTL_REG   (UartAddr+0x60)
#define AUX_MU_STAT_REG   (UartAddr+0x64)
#define AUX_MU_BAUD       (UartAddr+0x68)  

// AUXENB Register  
// ===============
#define UART_AUXENB_ENABLE      (1 << 0)

// AUX_MU_IO_REG Register
// ======================

// UART Interrupt Enable Register
// ==============================
#define UART_IER_RX_INT_ENABLE  (5 << 0)
#define UART_IER_TX_INT_ENABLE  (1 << 1)

// UART Interrupt Identification Register
// ======================================
#define UART_IIR_ID             (3 << 1)
#define UART_IIR_ID_TX          (1 << 1)
#define UART_IIR_FIFO_RX_CLR    (1 << 1)
#define UART_IIR_ID_RX          (2 << 1)
#define UART_IIR_FIFO_TX_CLR    (2 << 1)

// UART Line Control Register
// ==========================
#define UART_LCR_DATA_SIZE      (3 << 0)
#define UART_LCR_BREAK          (1 << 6)
#define UART_LCR_DLAB_ACCESS    (1 << 7)

// ==========================

// UART Modem Control Register
// ===========================

// UART Line Status Register
// =========================
#define UART_LSR_DATA_READY     (1 << 0)
#define UART_LSR_RX_OVERRUN     (1 << 1)
#define UART_LSR_TX_EMPTY       (1 << 5)

// UART Modem Status Register
// ==========================

// UART Scratch Register
// =====================

// UART Control Register
// ======================
#define UART_CNTL_RX_ENABLE     (1 << 0)
#define UART_CNTL_TX_ENABLE     (1 << 1)

// UART Status Register
// ====================
#define UART_STAT_RX_SYMB_AVL  (1 << 0)
#define UART_STAT_TX_SPACE_AVL (1 << 1)


#endif