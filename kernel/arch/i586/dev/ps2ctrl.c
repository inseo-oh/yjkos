#include "../ioport.h"
#include "../pic.h"
#include "ps2ctrl.h"
#include <assert.h>
#include <kernel/dev/ps2.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/io/co.h>
#include <kernel/mem/heap.h>
#include <kernel/ticktime.h>
#include <kernel/trapmanager.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

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
static uint8_t const CONFIG_FLAG_SYS           = 1 << 2; 
static uint8_t const CONFIG_FLAG_PORT0_CLK_OFF = 1 << 4;
static uint8_t const CONFIG_FLAG_PORT1_CLK_OFF = 1 << 5;
static uint8_t const CONFIG_FLAG_PORT0_TRANS   = 1 << 6;

// PS/2 controller status
static uint8_t const STATUS_FLAG_OUTBUF_FULL = 1 << 0;
static uint8_t const STATUS_FLAG_INBUF_FULL  = 1 << 1;
static uint8_t const STATUS_FLAG_SYS         = 1 << 2;
static uint8_t const STATUS_FLAG_CMD_DATA    = 1 << 3;
static uint8_t const STATUS_TIMEOUT_ERR      = 1 << 6;
static uint8_t const STATUS_PARITY_ERR       = 1 << 7;


struct portcontext {
    struct ps2port ps2port;
    uint8_t portidx;
    struct archi586_pic_irqhandler irqhandler;
};

static WARN_UNUSED_RESULT int waitforrecv(void) {
    ticktime oldtime = g_ticktime;
    bool timeout = true;
    while ((g_ticktime - oldtime) < PS2_TIMEOUT) {
        uint8_t ctrl_status = archi586_in8(STATUS_PORT);
        if (ctrl_status & STATUS_FLAG_OUTBUF_FULL) {
            timeout = false;
            break;
        }
    }
    if (timeout) {
        co_printf("ps2: receive wait timeout\n");
        return -EIO;
    }
    return 0;
}

static WARN_UNUSED_RESULT int waitforsend(void) {
    ticktime oldtime = g_ticktime;
    bool timeout = true;
    while ((g_ticktime - oldtime) < PS2_TIMEOUT) {
        uint8_t ctrl_status = archi586_in8(STATUS_PORT);
        if (!(ctrl_status & STATUS_FLAG_INBUF_FULL)) {
            timeout = false;
            break;
        }
    }
    if (timeout) {
        co_printf("ps2: send wait timeout\n");
        return -EIO;
    }
    return 0;
}

static int recvfromctrl(uint8_t *out) {
    if (CONFIG_COMM_DEBUG) {
        co_printf("ps2: receive data from controller\n");
    }
    int ret = waitforrecv();
    if (ret < 0) {
        return ret;
    }
    *out = archi586_in8(DATA_PORT);
    if (CONFIG_COMM_DEBUG) {
        co_printf("ps2: recevied data from controller: %#x\n", *out);
    }
    return 0;
}

static int sendtoctrl(uint8_t cmd) {
    if (CONFIG_COMM_DEBUG) {
        co_printf("ps2: send command %#x to controller\n", cmd);
    }
    int ret = waitforsend();
    if (ret < 0) {
        return ret;
    }
    archi586_out8(CMD_PORT, cmd);
    return 0;
}

static int senddatatoctrl(uint8_t data) {
    if (CONFIG_COMM_DEBUG) {
        co_printf("ps2: send data %#x to controller\n", data);
    }
    int ret = waitforsend();
    if (ret < 0) {
        return ret;
    }
    archi586_out8(DATA_PORT, data);
    return 0;
}

static ssize_t stream_op_write(struct stream *self, void *data, size_t size) {
    assert(size < STREAM_MAX_TRANSFER_SIZE);
    struct portcontext *port = self->data;

    for (size_t idx = 0; idx < size; idx++) {
        uint8_t c = ((uint8_t *)data)[idx];
        assert(port->portidx < 2);
        if (port->portidx == 1) {
            int ret = sendtoctrl(CMD_WRITEPORT1);
            if (ret < 0) {
                return ret;
            }
        }
        int ret = senddatatoctrl(c);
        if (ret < 0) {
            return ret;
        }
    }
    return size;
}

static void irqhandler(int irqnum, void *data) {
    struct portcontext *port = data;
    uint8_t value = archi586_in8(DATA_PORT);
    if (CONFIG_COMM_DEBUG) {
        co_printf("ps2: irq on port %u - data %#x\n", port->portidx, value);
    }
    ps2port_receivedbyte(&port->ps2port, value);
    archi586_pic_sendeoi(irqnum);
}

static struct stream_ops const OPS = {
    PS2_COMMON_STREAM_CALLBACKS,
    .write = stream_op_write,
};

static WARN_UNUSED_RESULT int discoveredport(size_t portindex) {
    assert(portindex < 2);
    int result = 0;
    struct portcontext *port = heap_alloc(sizeof(*port), HEAP_FLAG_ZEROMEMORY);
    if (port == NULL) {
        goto fail;
    }
    port->portidx = portindex;
    int irq;
    if (portindex == 0) {
        irq = IRQ_PORT0;
    } else {
        irq = IRQ_PORT1;
    }
    archi586_pic_registerhandler(&port->irqhandler, irq, irqhandler, port);
    archi586_pic_unmaskirq(irq);
    result = ps2port_register(&port->ps2port, &OPS, port);
    if (result < 0) {
        goto fail;
    }
    /* 
     * We can't undo ps2port_register as of writing this code, so no further
     * errors are allowed.
     */
    goto out;
fail:
    heap_free(port);
out:
    return result;
}

void archi586_ps2ctrl_init(void) {
    int result;
    bool singleport = false;
    // https://wiki.osdev.org/%228042%22_PS/2_Controller#Initialising_the_PS/2_Controller

    // Disable interrupts
    archi586_pic_maskirq(IRQ_PORT0);
    archi586_pic_maskirq(IRQ_PORT1);

    // Disable PS/2 devices
    result = sendtoctrl(CMD_DISABLEPORT0);
    if (result < 0) {
        goto fail;
    }
    result = sendtoctrl(CMD_DISABLEPORT1);
    if (result < 0) {
        goto fail;
    }

    // Empty the output buffer
    while(archi586_in8(STATUS_PORT) & STATUS_FLAG_OUTBUF_FULL) {
        archi586_in8(DATA_PORT);
    }

    // Reconfigure controller (Only configure port 0)
    result = sendtoctrl(CMD_READCTRLCONFIG);
    if (result < 0) {
        goto fail;
    }
    uint8_t ctrlconfig;
    result = recvfromctrl(&ctrlconfig);
    if (result < 0) {
        goto fail;
    }
    ctrlconfig &= ~(CONFIG_FLAG_PORT0_INT | CONFIG_FLAG_PORT0_TRANS | CONFIG_FLAG_PORT0_CLK_OFF);
    result = sendtoctrl(CMD_WRITECTRLCONFIG);
    if (result < 0) {
        goto fail;
    }
    result = senddatatoctrl(ctrlconfig);
    if (result < 0) {
        goto fail;
    }

    // Run self-test
    result = sendtoctrl(CMD_TESTCTRL);
    if (result < 0) {
        goto fail;
    }
    uint8_t response;
    result = recvfromctrl(&response);
    if (result < 0) {
        goto fail;
    }
    if (response != 0x55) {
        co_printf(
            "ps2: controller self test failed(response: %#x)\n",
            response);
        result = -EIO;
        goto fail;
    }

    /*
     * Self-test may have resetted the controller, so we reconfigure
     * Port 0 again. 
     */
    result = sendtoctrl(CMD_READCTRLCONFIG);
    if (result < 0) {
        goto fail;
    }
    result = recvfromctrl(&ctrlconfig);
    if (result < 0) {
        goto fail;
    }
    ctrlconfig &= ~(
        CONFIG_FLAG_PORT0_INT | CONFIG_FLAG_PORT0_TRANS |
        CONFIG_FLAG_PORT0_CLK_OFF);
    result = sendtoctrl(CMD_WRITECTRLCONFIG);
    if (result < 0) {
        goto fail;
    }
    result = senddatatoctrl(ctrlconfig);
    if (result < 0) {
        goto fail;
    }

    // Check if it's dual-port controller.
    result = sendtoctrl(CMD_ENABLEPORT1);
    if (result < 0) {
        goto fail;
    }
    result = sendtoctrl(CMD_READCTRLCONFIG);
    if (result < 0) {
        goto fail;
    }
    result = recvfromctrl(&ctrlconfig);
    if (result < 0) {
        goto fail;
    }
    if (ctrlconfig & CONFIG_FLAG_PORT1_CLK_OFF) {
        singleport = true;
    } else {
        // If it is, configure second port as well.
        result = sendtoctrl(CMD_DISABLEPORT1);
        if (result < 0) {
            goto fail;
        }
        result = sendtoctrl(CMD_READCTRLCONFIG);
        if (result < 0) {
            goto fail;
        }
        result = recvfromctrl(&ctrlconfig);
        if (result < 0) {
            goto fail;
        }
        ctrlconfig &= ~(CONFIG_FLAG_PORT1_INT | CONFIG_FLAG_PORT1_CLK_OFF);
        result = sendtoctrl(CMD_WRITECTRLCONFIG);
        if (result < 0) {
            goto fail;
        }
        result = senddatatoctrl(ctrlconfig);
        if (result < 0) {
            goto fail;
        }
    }
    co_printf(
        "ps2: detected as %s-port controller\n",
        singleport ? "single" : "dual");

    // Test each port
    bool port0ok, port1ok;
    result = sendtoctrl(CMD_TESTPORT0);
    if (result < 0) {
        goto fail;
    }
    result = recvfromctrl(&response);
    if (result < 0) {
        goto fail;
    }
    if (response != 0x00) {
        co_printf(
            "ps2: port 0 self test failed(response: %#x)\n", response);
        port0ok = false;
    } else {
        port0ok = true;
    }
    if (!singleport) {
        result = sendtoctrl(CMD_TESTPORT1);
        if (result < 0) {
            goto fail;
        }
        result = recvfromctrl(&response);
        if (result < 0) {
            goto fail;
        }
        if (response != 0x00) {
            co_printf(
                "ps2: port 1 self test failed(response: %#x)\n",
                response);
            port1ok = false;
        } else {
            port1ok = true;
        }
    } else {
        port1ok = false;
    }
    if (!port0ok && !port1ok) {
        result = -EIO;
        goto fail;
    }

    // Enable interrupts
    result = sendtoctrl(CMD_READCTRLCONFIG);
    if (result < 0) {
        goto fail;
    }
    result = recvfromctrl(&ctrlconfig);
    if (result < 0) {
        goto fail;
    }
    if (port0ok) {
        ctrlconfig |= CONFIG_FLAG_PORT0_INT;
    }
    if (port1ok) {
        ctrlconfig |= CONFIG_FLAG_PORT1_INT;
    }
    result = sendtoctrl(CMD_WRITECTRLCONFIG);
    if (result < 0) {
        goto fail;
    }
    result = senddatatoctrl(ctrlconfig);
    if (result < 0) {
        goto fail;
    }

    // Register port
    if (port0ok) {
        result = sendtoctrl(CMD_ENABLEPORT0);
        if (result < 0) {
            goto fail;
        }
        int ret = discoveredport(0);
        if (ret < 0) {
            co_printf("ps2: failed to register port 0\n");
        }
    }
    if (port1ok) {
        result = sendtoctrl(CMD_ENABLEPORT1);
        if (result < 0) {
            goto fail;
        }
        int ret = discoveredport(1);
        if (ret < 0) {
            co_printf("ps2: failed to register port 1\n");
        }
    }
    return;
fail:
    co_printf(
        "ps2: error %d occured. aborting controller initialization\n",
        result);
}
