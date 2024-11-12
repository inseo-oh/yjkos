#include "ioport.h"
#include "pic.h"
#include "serial.h"
#include <kernel/arch/interrupts.h>
#include <kernel/arch/iodelay.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/io/stream.h>
#include <kernel/io/co.h>
#include <kernel/io/tty.h>
#include <kernel/trapmanager.h>
#include <kernel/types.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

enum {
    REG_DATA = 0, // When LCR.DLAB=0
    REG_IER  = 1, // When LCR.DLAB=0
    REG_DLL  = 0, // When LCR.DLAB=1
    REG_DLH  = 1, // When LCR.DLAB=1
    REG_IIR  = 2,
    REG_LCR  = 3,
    REG_MCR  = 4,
    REG_LSR  = 5,
    REG_MSR  = 6,
};

// IER (Interrupt enable)
static uint8_t const IER_FLAG_RX_AVAIL     = 1 << 0;
static uint8_t const IER_FLAG_TX_EMPTY     = 1 << 1;
static uint8_t const IER_FLAG_RX_STATUS    = 1 << 2;
static uint8_t const IER_FLAG_MODEM_STATUS = 1 << 3;

// IIR (Interrupt identification)
static uint8_t const IIR_FLAG_NO_INT_PENDING = 1 << 0;
static uint8_t const IIR_FLAG_MODEM_STATUS   = 0 << 1;
static uint8_t const IIR_FLAG_TX_EMPTY       = 1 << 1;
static uint8_t const IIR_FLAG_RX_AVAIL       = 2 << 1;
static uint8_t const IIR_FLAG_RX_STATUS      = 3 << 1;

// LSR (Line status)
static uint8_t const LSR_FLAG_DATA_READY           = 1 << 0;
static uint8_t const LSR_FLAG_OVERRUN_ERR          = 1 << 1;
static uint8_t const LSR_FLAG_PARITY_ERR           = 1 << 2;
static uint8_t const LSR_FLAG_FRAMING_ERR          = 1 << 3;
static uint8_t const LSR_FLAG_RECVED_BREAK         = 1 << 4;
static uint8_t const LSR_FLAG_TX_HOLDING_REG_EMPTY = 1 << 5;
static uint8_t const LSR_FLAG_TX_SHIFT_REG_EMPTY   = 1 << 6;

// MSR (Modem status)
static uint8_t const MSR_FLAG_CTS_DELTA        = 1 << 0;
static uint8_t const MSR_FLAG_DSR_DELTA        = 1 << 1; 
static uint8_t const MSR_FLAG_RI_TRAILING_EDGE = 1 << 2;
static uint8_t const MSR_FLAG_DCD_DELTA        = 1 << 3; 
static uint8_t const MSR_FLAG_CTS              = 1 << 4;
static uint8_t const MSR_FLAG_DSR              = 1 << 5;
static uint8_t const MSR_FLAG_RI               = 1 << 6;
static uint8_t const MSR_FLAG_DCD              = 1 << 7;

// LCR (Line control)
static uint8_t const LCR_FLAG_WORD_LEN_FIVE   = 0 << 0;
static uint8_t const LCR_FLAG_WORD_LEN_SIX    = 1 << 0;
static uint8_t const LCR_FLAG_WORD_LEN_SEVEN  = 2 << 0;
static uint8_t const LCR_FLAG_WORD_LEN_EIGHT  = 3 << 0;
static uint8_t const LCR_FLAG_MULTI_STOP_BITS = 1 << 2; // When enabled: 5-bit -> 1.5 stop-bit, otherwise -> 2 stop-bit
static uint8_t const LCR_FLAG_PARITY_ENABLE   = 1 << 3;
static uint8_t const LCR_FLAG_PARITY_EVEN     = 0 << 4;
static uint8_t const LCR_FLAG_PARITY_ODD      = 1 << 4;
static uint8_t const LCR_FLAG_STICKY_PARITY   = 1 << 5; // Even parity -> Parity is always 1, Odd parity -> Parity is always 0
static uint8_t const LCR_FLAG_SET_BREAK       = 1 << 6; // Enter break condition by pulling Tx low
                                                                     // (Receiving UART will see this as long stream of zeros)
static uint8_t const LCR_FLAG_DLAB            = 1 << 7; // Divisor Latch Access Bit

// MCR (Modem control)
static uint8_t const  MCR_FLAG_DTR      = 1 << 0;
static uint8_t const  MCR_FLAG_RTS      = 1 << 1;
static uint8_t const  MCR_FLAG_OUT1     = 1 << 2;
static uint8_t const  MCR_FLAG_OUT2     = 1 << 3;
static uint8_t const  MCR_FLAG_LOOPBACK = 1 << 4;

static WARN_UNUSED_RESULT int getdivisor(
    struct archi586_serial *self, long baudrate)
{
    for (int32_t divisor = 1; divisor <= 0xffff; ++divisor) {
        if ((self->masterclock / divisor) == baudrate) {
            return divisor;
        }
    }
    return -EINVAL;
}


static void writereg(struct archi586_serial *self, uint8_t regidx, uint8_t val) {
    archi586_out8(self->baseaddr + regidx, val);    
}

static uint8_t readreg(struct archi586_serial *self, uint8_t regidx) {
    return archi586_in8(self->baseaddr + regidx);
}

static void setdlab(struct archi586_serial *self) {
    uint8_t val = archi586_in8(REG_LCR);
    val |= LCR_FLAG_DLAB;
    writereg(self, REG_LCR, val);
}

static void cleardlab(struct archi586_serial *self) {
    uint8_t val = archi586_in8(REG_LCR);
    val &= ~LCR_FLAG_DLAB;
    writereg(self, REG_LCR, val);
}

static void writeier(struct archi586_serial *self, uint8_t val) {
    cleardlab(self);
    writereg(self, REG_IER, val);
}

static void writedl(struct archi586_serial *self, uint16_t dl) {
    setdlab(self);
    writereg(self, REG_DLL, dl);
    writereg(self, REG_DLH, dl >> 8);
}

static void writedata(struct archi586_serial *self, uint8_t val) {
    cleardlab(self);
    writereg(self, REG_DATA, val);
}

uint8_t readdata(struct archi586_serial *self) {
    cleardlab(self);
    return readreg(self, REG_DATA);
}

static void waitreadytosend(struct archi586_serial *self) {
    if (!self->useirq || !arch_interrupts_areenabled()) {
        while (!(readreg(self, REG_LSR) & LSR_FLAG_TX_HOLDING_REG_EMPTY)) {}
    } else {
        while (self->txint == false) {}
        self->txint = false;
    }
}

static void waitreadytorecv(struct archi586_serial *self) {
    if (!self->useirq || !arch_interrupts_areenabled()) {
        while (!(readreg(self, REG_LSR) & LSR_FLAG_DATA_READY)) {}
    } else {
        while (self->rxint == false) {}
        self->rxint = false;
    }
}


static int runloopbacktest(struct archi586_serial *self) {
    uint8_t oldmcr = readreg(self, REG_MCR);
    writereg(self, REG_MCR, oldmcr | MCR_FLAG_LOOPBACK);
    uint8_t new_mcr = readreg(self, REG_MCR);
    if (!(new_mcr & MCR_FLAG_LOOPBACK)) {
        co_printf(
            "serial: failed to write to MCR\n");
        return -EIO;
    }
    uint8_t expected = 0x69;
    writedata(self, expected);
    uint32_t waited_counter = 0;
    while (!(readreg(self, REG_LSR) & LSR_FLAG_DATA_READY)) {
        if (1000000 < waited_counter) {
            co_printf(
                "serial: loopback response timeout\n");
            return -EIO;
        }
        arch_iodelay();
        waited_counter += 2;
    }
    uint8_t got = readdata(self);
    bool test_ok = got == expected;
    if (!test_ok) {
        co_printf(
            "serial: loopback test failed: expected %#x, got %#x\n",
            expected, got);
        writereg(self, REG_MCR, oldmcr);
        return -EIO;
    }
    return 0;
}

static WARN_UNUSED_RESULT ssize_t stream_op_write(
    struct stream *self, void *data, size_t size) {
    assert(size <= STREAM_MAX_TRANSFER_SIZE);
    struct archi586_serial *cport = (struct archi586_serial *)self->data;

    for (size_t idx = 0; idx < size; idx++) {
        uint8_t c = ((uint8_t *)data)[idx];

        if (cport->cr_to_crlf) {
            if (c == '\r') {
                cport->cr = true;
            }
            if ((c == '\n') && !(cport->cr)) {
                waitreadytosend(cport);
                writedata(cport, '\r');
                cport->cr = false;
            }
        }

        waitreadytosend(cport);
        writedata(cport, c);
    }
    return size;
}

static WARN_UNUSED_RESULT ssize_t stream_op_read(
    struct stream *self, void *buf, size_t size)
{
    assert(size <= STREAM_MAX_TRANSFER_SIZE);
    for (size_t idx = 0; idx < size; idx++) {
        waitreadytorecv(self->data);
        ((uint8_t *)buf)[idx] = readdata(self->data);
    }
    return size;
}

static struct stream_ops const OPS = {
    .read = stream_op_read,
    .write = stream_op_write,
};

static void irqhandler(int irqnum, void *data) {
    struct archi586_serial *self = data;
    uint8_t ier = readreg(self, REG_IER);
    uint8_t iir = readreg(self, REG_IIR);
    uint8_t lsr =readreg(self, REG_LSR);
    (void)ier;
    (void)lsr;
    switch (iir & (0x3 << 1)) {
        case 0x1 << 1:
            self->txint = true;
            break;
        case 0x2 << 1:
            self->rxint = true;
            break;
    }
    archi586_pic_sendeoi(irqnum);
}

WARN_UNUSED_RESULT int archi586_serial_init(
    struct archi586_serial *out, uint16_t baseaddr, int32_t masterclock,
    uint8_t irq)
{
    memset(out, 0, sizeof(*out));
    out->tty.stream.data = out;
    out->tty.stream.ops = &OPS;
    out->baseaddr = baseaddr;
    out->masterclock = masterclock;
    out->irq = irq;
    int ret = runloopbacktest(out);
    if (ret < 0) {
        return ret;
    }
    writeier(out, 0);
    writereg(
        out, REG_MCR,
        MCR_FLAG_DTR | MCR_FLAG_RTS | MCR_FLAG_OUT1 | MCR_FLAG_OUT2);
    return 0;
}

WARN_UNUSED_RESULT int archi586_serial_config(
    struct archi586_serial *self, uint32_t baudrate)
{
    int ret = getdivisor(self, baudrate);
    if (ret < 0) {
        return ret;
    }
    writedl(self, ret);
    writereg(self, REG_LCR, LCR_FLAG_WORD_LEN_EIGHT);
    return 0;
}

void archi586_serial_useirq(struct archi586_serial *self) {
    archi586_pic_registerhandler(&self->irqhandler, self->irq, irqhandler, self);
    archi586_pic_unmaskirq(self->irq);
    writeier(self, 0x3); // Transmit and Receive interrupts
    // MCR's OUT2 also needs to be set
    uint8_t mcr = readreg(self, REG_MCR);
    mcr |= MCR_FLAG_OUT2;
    writereg(self, REG_MCR, mcr);
    self->useirq = true;
}

WARN_UNUSED_RESULT int archi586_serial_initiodev(struct archi586_serial *self) {
    return tty_register(&self->tty, self);
}
