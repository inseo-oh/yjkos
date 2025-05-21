#include "serial.h"
#include "ioport.h"
#include "pic.h"
#include <errno.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/iodelay.h>
#include <kernel/io/co.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/strutil.h>
#include <stdint.h>
#include <sys/types.h>

#define REG_DATA 0 /* When LCR.DLAB=0 */
#define REG_IER 1  /* When LCR.DLAB=0 */
#define REG_DLL 0  /* When LCR.DLAB=1 */
#define REG_DLH 1  /* When LCR.DLAB=1 */
#define REG_IIR 2
#define REG_LCR 3
#define REG_MCR 4
#define REG_LSR 5
#define REG_MSR 6

/* IER (Interrupt enable) *****************************************************/
#define IER_FLAG_RX_AVAIL (1U << 0)
#define IER_FLAG_TX_EMPTY (1U << 1)
#define IER_FLAG_RX_STATUS (1U << 2)
#define IER_FLAG_MODEM_STATUS (1U << 3)

/* IIR (Interrupt identification) *********************************************/
#define IIR_FLAG_NO_INT_PENDING (1U << 0)
#define IIR_FLAG_MODEM_STATUS (0U << 1)
#define IIR_FLAG_TX_EMPTY (1U << 1)
#define IIR_FLAG_RX_AVAIL (2U << 1)
#define IIR_FLAG_RX_STATUS (3U << 1)

/** LSR (Line status) *********************************************************/
#define LSR_FLAG_DATA_READY (1U << 0)
#define LSR_FLAG_OVERRUN_ERR (1U << 1)
#define LSR_FLAG_PARITY_ERR (1U << 2)
#define LSR_FLAG_FRAMING_ERR (1U << 3)
#define LSR_FLAG_RECVED_BREAK (1U << 4)
#define LSR_FLAG_TX_HOLDING_REG_EMPTY (1U << 5)
#define LSR_FLAG_TX_SHIFT_REG_EMPTY (1U << 6)

/* MSR (Modem status) *********************************************************/
#define MSR_FLAG_CTS_DELTA (1U << 0)
#define MSR_FLAG_DSR_DELTA (1U << 1)
#define MSR_FLAG_RI_TRAILING_EDGE (1U << 2)
#define MSR_FLAG_DCD_DELTA (1U << 3)
#define MSR_FLAG_CTS (1U << 4)
#define MSR_FLAG_DSR (1U << 5)
#define MSR_FLAG_RI (1U << 6)
#define MSR_FLAG_DCD (1U << 7)

/* LCR (Line control) *********************************************************/
#define LCR_FLAG_WORD_LEN_FIVE (0U << 0)
#define LCR_FLAG_WORD_LEN_SIX (0U << 0)
#define LCR_FLAG_WORD_LEN_SEVEN (0U << 0)
#define LCR_FLAG_WORD_LEN_EIGHT (0U << 0)

/* When enabled: 5-bit -> 1.5 stop-bit, otherwise -> 2 stop-bit ***************/
#define LCR_FLAG_MULTI_STOP_BITS (1U << 2)
#define LCR_FLAG_PARITY_ENABLE (1U << 3)
#define LCR_FLAG_PARITY_EVEN (0U << 4)
#define LCR_FLAG_PARITY_ODD (1U << 4)

/* Even parity -> Parity is always 1, Odd parity -> Parity is always 0 ********/
#define LCR_FLAG_STICKY_PARITY (1U << 5)

/*
 * Enter break condition by pulling Tx low
 * (Receiving UART will see this as long stream of zeros)
 */
#define LCR_FLAG_SET_BREAK (1U << 6)
#define LCR_FLAG_DLAB (1U << 7) /* Divisor Latch Access Bit */

/* MCR (Modem control) ********************************************************/
#define MCR_FLAG_DTR (1U << 0)
#define MCR_FLAG_RTS (1U << 1)
#define MCR_FLAG_OUT1 (1U << 2)
#define MCR_FLAG_OUT2 (1U << 3)
#define MCR_FLAG_LOOPBACK (1U << 4)

[[nodiscard]] static int get_divisor(struct archi586_serial *self, int32_t baudrate) {
    for (int32_t divisor = 1; divisor <= 0xffff; ++divisor) {
        if ((self->masterclock / divisor) == baudrate) {
            return divisor;
        }
    }
    return -EINVAL;
}

static void write_reg(struct archi586_serial *self, uint8_t regidx, uint8_t val) {
    archi586_out8(self->baseaddr + regidx, val);
}

static uint8_t read_reg(struct archi586_serial *self, uint8_t regidx) {
    return archi586_in8(self->baseaddr + regidx);
}

static void setdlab(struct archi586_serial *self) {
    uint8_t val = archi586_in8(REG_LCR);
    val |= LCR_FLAG_DLAB;
    write_reg(self, REG_LCR, val);
}

static void clear_dlab(struct archi586_serial *self) {
    uint8_t val = archi586_in8(REG_LCR);
    val &= ~LCR_FLAG_DLAB;
    write_reg(self, REG_LCR, val);
}

static void write_ier(struct archi586_serial *self, uint8_t val) {
    clear_dlab(self);
    write_reg(self, REG_IER, val);
}

static void write_dl(struct archi586_serial *self, uint16_t dl) {
    setdlab(self);
    write_reg(self, REG_DLL, dl);
    write_reg(self, REG_DLH, dl >> 8);
}

static void write_data(struct archi586_serial *self, uint8_t val) {
    clear_dlab(self);
    write_reg(self, REG_DATA, val);
}

uint8_t readdata(struct archi586_serial *self) {
    clear_dlab(self);
    return read_reg(self, REG_DATA);
}

static void wait_ready_to_send(struct archi586_serial *self) {
    if (!self->useirq || !arch_irq_are_enabled()) {
        while (!(read_reg(self, REG_LSR) & LSR_FLAG_TX_HOLDING_REG_EMPTY)) {
        }
    } else {
        while (self->txint == false) {
        }
        self->txint = false;
    }
}

static void wait_ready_to_recv(struct archi586_serial *self) {
    if (!self->useirq || !arch_irq_are_enabled()) {
        while (!(read_reg(self, REG_LSR) & LSR_FLAG_DATA_READY)) {
        }
    } else {
        while (self->rxint == false) {
        }
        self->rxint = false;
    }
}

static int run_loopback_test(struct archi586_serial *self) {
    uint8_t oldmcr = read_reg(self, REG_MCR);
    write_reg(self, REG_MCR, oldmcr | MCR_FLAG_LOOPBACK);
    uint8_t new_mcr = read_reg(self, REG_MCR);
    if (!(new_mcr & MCR_FLAG_LOOPBACK)) {
        co_printf("serial: failed to write to MCR\n");
        return -EIO;
    }
    uint8_t expected = 0x69;
    write_data(self, expected);
    uint32_t waited_counter = 0;
    while (!(read_reg(self, REG_LSR) & LSR_FLAG_DATA_READY)) {
        if (1000000 < waited_counter) {
            co_printf("serial: loopback response timeout\n");
            return -EIO;
        }
        arch_iodelay();
        waited_counter += 2;
    }
    uint8_t got = readdata(self);
    bool test_ok = got == expected;
    if (!test_ok) {
        co_printf("serial: loopback test failed: expected %#x, got %#x\n", expected, got);
        write_reg(self, REG_MCR, oldmcr);
        return -EIO;
    }
    return 0;
}

[[nodiscard]] static ssize_t stream_op_write(struct stream *self, void *data, size_t size) {
    assert(size <= STREAM_MAX_TRANSFER_SIZE);
    struct archi586_serial *cport = (struct archi586_serial *)self->data;

    for (size_t idx = 0; idx < size; idx++) {
        uint8_t c = ((uint8_t *)data)[idx];

        if (cport->cr_to_crlf) {
            if (c == '\r') {
                cport->cr = true;
            }
            if ((c == '\n') && !(cport->cr)) {
                wait_ready_to_send(cport);
                write_data(cport, '\r');
                cport->cr = false;
            }
        }

        wait_ready_to_send(cport);
        write_data(cport, c);
    }
    return (ssize_t)size;
}

[[nodiscard]] static ssize_t stream_op_read(struct stream *self, void *buf, size_t size) {
    assert(size <= STREAM_MAX_TRANSFER_SIZE);
    for (size_t idx = 0; idx < size; idx++) {
        wait_ready_to_recv(self->data);
        ((uint8_t *)buf)[idx] = readdata(self->data);
    }
    return (ssize_t)size;
}

static struct stream_ops const OPS = {
    .read = stream_op_read,
    .write = stream_op_write,
};

static void irq_handler(int irqnum, void *data) {
    struct archi586_serial *self = data;
    uint8_t ier = read_reg(self, REG_IER);
    uint8_t iir = read_reg(self, REG_IIR);
    uint8_t lsr = read_reg(self, REG_LSR);
    (void)ier;
    (void)lsr;
    switch (iir & (0x3U << 1)) {
    case 0x1 << 1:
        self->txint = true;
        break;
    case 0x2 << 1:
        self->rxint = true;
        break;
    default:
        break;
    }
    archi586_pic_send_eoi(irqnum);
}

[[nodiscard]] int archi586_serial_init(struct archi586_serial *out, uint16_t baseaddr, int32_t masterclock, uint8_t irq) {
    vmemset(out, 0, sizeof(*out));
    out->tty.stream.data = out;
    out->tty.stream.ops = &OPS;
    out->baseaddr = baseaddr;
    out->masterclock = masterclock;
    out->irq = irq;
    int ret = run_loopback_test(out);
    if (ret < 0) {
        return ret;
    }
    write_ier(out, 0);
    write_reg(out, REG_MCR, MCR_FLAG_DTR | MCR_FLAG_RTS | MCR_FLAG_OUT1 | MCR_FLAG_OUT2);
    return 0;
}

[[nodiscard]] int archi586_serial_config(struct archi586_serial *self, int32_t baudrate) {
    int ret = get_divisor(self, baudrate);
    if (ret < 0) {
        return ret;
    }
    write_dl(self, ret);
    write_reg(self, REG_LCR, LCR_FLAG_WORD_LEN_EIGHT);
    return 0;
}

void archi586_serial_use_irq(struct archi586_serial *self) {
    archi586_pic_register_handler(&self->irqhandler, self->irq, irq_handler, self);
    archi586_pic_unmask_irq(self->irq);
    write_ier(self, 0x3); /* Transmit and Receive interrupts */
    /* MCR's OUT2 also needs to be set */
    uint8_t mcr = read_reg(self, REG_MCR);
    mcr |= MCR_FLAG_OUT2;
    write_reg(self, REG_MCR, mcr);
    self->useirq = true;
}

[[nodiscard]] int archi586_serial_init_iodev(struct archi586_serial *self) {
    return tty_register(&self->tty, self);
}
