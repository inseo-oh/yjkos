#include "../ioport.h"
#include "../pic.h"
#include "ps2ctrl.h"
#include <assert.h>
#include <kernel/dev/ps2.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <kernel/ticktime.h>
#include <kernel/trapmanager.h>
#include <stddef.h>
#include <stdint.h>

//------------------------------- Configuration -------------------------------

// Show communication between OS and PS/2 controller?
static bool const CONFIG_COMM_DEBUG = false;

//-----------------------------------------------------------------------------


enum {
    DATA_PORT   = 0x60,
    STATUS_PORT = 0x64, // read only
    CMD_PORT    = 0x64, // write only

    // PS/2 controller commands
    CMD_READCTRLCONFIG   = 0x20,
    CMD_WRITECTRLCONFIG  = 0x60,
    CMD_DISABLEPORT1     = 0xa7,
    CMD_ENABLEPORT1      = 0xa8,
    CMD_TESTPORT1        = 0xa9,
    CMD_TESTCTRL         = 0xaa,
    CMD_TESTPORT0        = 0xab,
    CMD_DISABLEPORT0     = 0xad,
    CMD_ENABLEPORT0      = 0xae,
    CMD_WRITEPORT1       = 0xd4,

    IRQ_PORT0 = 1,
    IRQ_PORT1 = 12,
};

// PS/2 controller configuration
static uint8_t const CONFIG_FLAG_PORT0_INT     = 1 << 0;
static uint8_t const CONFIG_FLAG_PORT1_INT     = 1 << 1;
static uint8_t const CONFIG_FLAG_SYS           = 1 << 2; // Cleared on reset, set to 1 after POST.
static uint8_t const CONFIG_FLAG_PORT0_CLK_OFF = 1 << 4;
static uint8_t const CONFIG_FLAG_PORT1_CLK_OFF = 1 << 5;
static uint8_t const CONFIG_FLAG_PORT0_TRANS   = 1 << 6;

// PS/2 controller status
static uint8_t const STATUS_FLAG_OUTBUF_FULL = 1 << 0;
static uint8_t const STATUS_FLAG_INBUF_FULL  = 1 << 1;
static uint8_t const STATUS_FLAG_SYS         = 1 << 2;
static uint8_t const STATUS_FLAG_CMD_DATA    = 1 << 3; // Did written byte go to device(0) or controller(1)
static uint8_t const STATUS_TIMEOUT_ERR      = 1 << 6;
static uint8_t const STATUS_PARITY_ERR       = 1 << 7;


struct portcontext {
    struct ps2port ps2port;
    uint8_t portidx;
    struct archx86_pic_irqhandler irqhandler;
};

static FAILABLE_FUNCTION waitforrecv(void) {
FAILABLE_PROLOGUE
    ticktime_type oldtime = g_ticktime;
    bool timeout = true;
    while ((g_ticktime - oldtime) < PS2_TIMEOUT) {
        uint8_t ctrl_status = archx86_in8(STATUS_PORT);
        if (ctrl_status & STATUS_FLAG_OUTBUF_FULL) {
            timeout = false;
            break;
        }
    }
    if (timeout) {
        tty_printf("ps2: receive wait timeout\n");
        THROW(ERR_IO);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION waitforsend(void) {
FAILABLE_PROLOGUE
    ticktime_type oldtime = g_ticktime;
    bool timeout = true;
    while ((g_ticktime - oldtime) < PS2_TIMEOUT) {
        uint8_t ctrl_status = archx86_in8(STATUS_PORT);
        if (!(ctrl_status & STATUS_FLAG_INBUF_FULL)) {
            timeout = false;
            break;
        }
    }
    if (timeout) {
        tty_printf("ps2: send wait timeout\n");
        THROW(ERR_IO);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION recvfromctrl(uint8_t *out) {
FAILABLE_PROLOGUE
    if (CONFIG_COMM_DEBUG) {
        tty_printf("ps2: receive data from controller\n");
    }
    TRY(waitforrecv());
    *out = archx86_in8(DATA_PORT);
    if (CONFIG_COMM_DEBUG) {
        tty_printf("ps2: recevied data from controller: %#x\n", *out);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION sendtoctrl(uint8_t cmd) {
FAILABLE_PROLOGUE
    if (CONFIG_COMM_DEBUG) {
        tty_printf("ps2: send command %#x to controller\n", cmd);
    }
    TRY(waitforsend());
    archx86_out8(CMD_PORT, cmd);
    TRY(waitforsend());
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION senddatatoctrl(uint8_t data) {
FAILABLE_PROLOGUE
    if (CONFIG_COMM_DEBUG) {
        tty_printf("ps2: send data %#x to controller\n", data);
    }
    TRY(waitforsend());
    archx86_out8(DATA_PORT, data);
    TRY(waitforsend());
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION stream_op_write(struct stream *self, void *data, size_t size) {
FAILABLE_PROLOGUE
    struct portcontext *port = self->data;

    for (size_t idx = 0; idx < size; idx++) {
        uint8_t c = ((uint8_t *)data)[idx];
        assert(port->portidx < 2);
        if (port->portidx == 1) {
            TRY(sendtoctrl(CMD_WRITEPORT1));
        }
        TRY(senddatatoctrl(c));
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static void irqhandler(int irqnum, void *data) {
    struct portcontext *port = data;
    uint8_t value = archx86_in8(DATA_PORT);
    if (CONFIG_COMM_DEBUG) {
        tty_printf("ps2: irq on port %u - data %#x\n", port->portidx, value);
    }
    ps2port_receivedbyte(&port->ps2port, value);
    archx86_pic_sendeoi(irqnum);
}

static struct stream_ops const OPS = {
    PS2_COMMON_STREAM_CALLBACKS,
    .write = stream_op_write,
};

static void discoveredport(size_t portindex) {
    assert(portindex < 2);
    struct portcontext *port = heap_alloc(sizeof(*port), HEAP_FLAG_ZEROMEMORY);
    if (port == NULL) {
        tty_printf("ps2: not enough memory to register port %u\n", portindex);
        goto fail;
    }
    port->portidx = portindex;
    int irq;
    if (portindex == 0) {
        irq = IRQ_PORT0;
    } else {
        irq = IRQ_PORT1;
    }
    archx86_pic_registerhandler(&port->irqhandler, irq, irqhandler, port);
    archx86_pic_unmaskirq(irq);
    status_t status = ps2port_register(&port->ps2port, &OPS, port);
    if (status != OK) {
        tty_printf("ps2: failed to register port %u (error %d)\n", portindex, status);
        goto fail;
    }
    // We can't undo ps2port_register as of writing this code, so no further failable action can happen.
    iodev_printf(&port->ps2port.device, "registered ps/2 port %u(irq %u)\n", portindex, irq);
    return;
fail:
    if (port != NULL) {
        heap_free(port);
    }
}

static FAILABLE_FUNCTION doinit(void) {
FAILABLE_PROLOGUE
    bool singleport = false;
    // https://wiki.osdev.org/%228042%22_PS/2_Controller#Initialising_the_PS/2_Controller

    // Disable interrupts
    archx86_pic_maskirq(IRQ_PORT0);
    archx86_pic_maskirq(IRQ_PORT1);

    // Disable PS/2 devices
    TRY(sendtoctrl(CMD_DISABLEPORT0));
    TRY(sendtoctrl(CMD_DISABLEPORT1));

    // Empty the output buffer
    while(archx86_in8(STATUS_PORT) & STATUS_FLAG_OUTBUF_FULL) {
        archx86_in8(DATA_PORT);
    }

    // Reconfigure controller (Only configure port 0)
    TRY(sendtoctrl(CMD_READCTRLCONFIG));
    uint8_t ctrlconfig;
    TRY(recvfromctrl(&ctrlconfig));
    ctrlconfig &= ~(CONFIG_FLAG_PORT0_INT | CONFIG_FLAG_PORT0_TRANS | CONFIG_FLAG_PORT0_CLK_OFF);
    TRY(sendtoctrl(CMD_WRITECTRLCONFIG));
    TRY(senddatatoctrl(ctrlconfig));

    // Run self-test
    TRY(sendtoctrl(CMD_TESTCTRL));
    uint8_t response;
    TRY(recvfromctrl(&response));
    if (response != 0x55) {
        tty_printf("ps2: controller self test failed(response: %#x)\n", response);
        THROW(ERR_IO);
    }

    // Self-test may have resetted the controller, so we reconfigure Port 0 again. 
    TRY(sendtoctrl(CMD_READCTRLCONFIG));
    TRY(recvfromctrl(&ctrlconfig));
    ctrlconfig &= ~(CONFIG_FLAG_PORT0_INT | CONFIG_FLAG_PORT0_TRANS | CONFIG_FLAG_PORT0_CLK_OFF);
    TRY(sendtoctrl(CMD_WRITECTRLCONFIG));
    TRY(senddatatoctrl(ctrlconfig));

    // Check if it's dual-port controller.
    TRY(sendtoctrl(CMD_ENABLEPORT1));
    TRY(sendtoctrl(CMD_READCTRLCONFIG));
    TRY(recvfromctrl(&ctrlconfig));
    if (ctrlconfig & CONFIG_FLAG_PORT1_CLK_OFF) {
        singleport = true;
    } else {
        // If it is, configure second port as well.
        TRY(sendtoctrl(CMD_DISABLEPORT1));
        TRY(sendtoctrl(CMD_READCTRLCONFIG));
        TRY(recvfromctrl(&ctrlconfig));
        ctrlconfig &= ~(CONFIG_FLAG_PORT1_INT | CONFIG_FLAG_PORT1_CLK_OFF);
        TRY(sendtoctrl(CMD_WRITECTRLCONFIG));
        TRY(senddatatoctrl(ctrlconfig));
    }
    tty_printf("ps2: detected as %s-port controller\n", singleport ? "single" : "dual");

    // Test each port
    bool port0OK, port1OK;
    TRY(sendtoctrl(CMD_TESTPORT0));
    TRY(recvfromctrl(&response));
    if (response != 0x00) {
        tty_printf("ps2: port 0 self test failed(response: %#x)\n", response);
        port0OK = false;
    } else {
        port0OK = true;
    }
    if (!singleport) {
        TRY(sendtoctrl(CMD_TESTPORT1));
        TRY(recvfromctrl(&response));
        if (response != 0x00) {
            tty_printf("ps2: port 1 self test failed(response: %#x)\n", response);
            port1OK = false;
        } else {
            port1OK = true;
        }
    } else {
        port1OK = false;
    }
    if (!port0OK && !port1OK) {
        tty_printf("ps2: ***** No working PS/2 ports found *****\n");
        THROW(ERR_IO);
    }

    // Enable interrupts
    TRY(sendtoctrl(CMD_READCTRLCONFIG));
    TRY(recvfromctrl(&ctrlconfig));
    if (port0OK) {
        ctrlconfig |= CONFIG_FLAG_PORT0_INT;
    }
    if (port1OK) {
        ctrlconfig |= CONFIG_FLAG_PORT1_INT;
    }
    TRY(sendtoctrl(CMD_WRITECTRLCONFIG));
    TRY(senddatatoctrl(ctrlconfig));

    // Register port
    if (port0OK) {
        TRY(sendtoctrl(CMD_ENABLEPORT0));
        discoveredport(0);
    }
    if (port1OK) {
        TRY(sendtoctrl(CMD_ENABLEPORT1));
        discoveredport(1);
    }

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

void archx86_ps2ctrl_init(void) {
    status_t status = doinit();
    if (status != OK) {
        tty_printf("ps2: error %d occured. aborting controller initialization\n", status);
        archx86_pic_maskirq(IRQ_PORT0);
        archx86_pic_maskirq(IRQ_PORT1);
        return;
    }
    return;
}
