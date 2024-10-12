#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/iodelay.h>
#include <kernel/dev/ps2.h>
#include <kernel/dev/ps2kbd.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <kernel/lib/queue.h>
#include <kernel/status.h>
#include <kernel/ticktime.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct list s_ports;

FAILABLE_FUNCTION ps2port_stream_op_read(size_t *size_out, struct stream *self, void *buf, size_t size) {
FAILABLE_PROLOGUE
    struct ps2port *port = self->data;
    size_t writtensize = 0;
    ticktime_t starttime = g_ticktime;
    while(writtensize == 0) {
        if (PS2_TIMEOUT <= (g_ticktime - starttime)) {
            THROW(ERR_IO);
        }
        ////////////////////////////////////////////////////////////////////////
        bool previnterrupts = arch_interrupts_disable();
        for (size_t idx = 0; idx < size; idx++) {
            bool ok = QUEUE_DEQUEUE(&((uint8_t *)buf)[idx], &port->recvqueue);
            if (!ok) {
                break;
            }
            writtensize++;
        }
        interrupts_restore(previnterrupts);
        ////////////////////////////////////////////////////////////////////////
        if (writtensize == 0) {
            arch_iodelay();
        }
    }
    if (writtensize == 0) {
        THROW(ERR_IO);
    }
    *size_out = writtensize;
FAILABLE_EPILOGUE_BEGIN 
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION sendandwaitack(struct ps2port *port, uint8_t cmd) {
FAILABLE_PROLOGUE
    uint8_t res;
    TRY(stream_putchar(&port->stream, cmd));
    TRY(stream_waitchar((char *)&res, &port->stream, PS2_TIMEOUT));
    if (res != PS2_RESPONSE_ACK) {
        iodev_printf(&port->device, "command %#x - expected ACK(0xfa), got %#x\n", cmd, res);
        THROW(ERR_IO);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

enum devtype {
    DEVTYPE_KBD,
    DEVTYPE_MOUSE,
};

static FAILABLE_FUNCTION identifydevice(enum devtype *out, struct ps2port *port) {
FAILABLE_PROLOGUE
    TRY(sendandwaitack(port, PS2_CMD_DISABLESCANNING));
    TRY(sendandwaitack(port, PS2_CMD_IDENTIFY));
    size_t identlen = 0;
    uint8_t ident[2];
    for (size_t i = 0; i < sizeof(ident)/sizeof(*ident); i++) {
        status_t status = stream_waitchar((char *)&ident[i], &port->stream, PS2_TIMEOUT);
        if (status == ERR_IO) {
            break;
        } else if (status != OK) {
            THROW(status);
        }
        identlen++;
    }
    enum devtype type = DEVTYPE_KBD;
    switch(identlen) {
        case 0:
            break;
        case 1:
            switch(ident[0]) {
                case 0x00:
                case 0x03:
                case 0x04:
                    type = DEVTYPE_MOUSE;
                    break;
                default:
                    iodev_printf(&port->device, "unknown device ID %#x. assuming it's keyboard.\n", ident[0]);
            }
            break;
        default:
            assert(identlen == 2);
            if (ident[0] != 0xab) {
                iodev_printf(&port->device, "unknown device ID %#x %#x. assuming it's keyboard.\n", ident[0], ident[1]);
            }
            break;
    }
    switch (type) {
        case DEVTYPE_KBD:
            iodev_printf(&port->device, "device is keyboard\n");
            break;
        case DEVTYPE_MOUSE:
            iodev_printf(&port->device, "device is mouse\n");
            break;
    }
    *out = type;
FAILABLE_EPILOGUE_BEGIN
    {
        status_t status;
        status = sendandwaitack(port, PS2_CMD_ENABLESCANNING);
        if (status != OK) {
            iodev_printf(&port->device, "scanning couldn't be enabled(error %d). device may not work!\n", status);
        }
    }
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION initdev(struct ps2port *port) {
FAILABLE_PROLOGUE
    // Send reset command
    bool resetack = false;
    bool resetaa = false;
    uint8_t res;
    bool badresponse = false;
    TRY(stream_putchar(&port->stream, (uint8_t)PS2_CMD_RESET));
    // After successful reset, we should receive 0xfa, 0xaa response
    for (size_t i = 0; i < 2; i++) {
        TRY(stream_waitchar((char *)&res, &port->stream, PS2_TIMEOUT));
        if (res == PS2_RESPONSE_ACK) {
            if (resetack) {
                badresponse = true;
                break;
            }
            resetack = true;
        }
        if (res == 0xaa) {
            if (resetaa) {
                badresponse = true;
                break;
            }
            resetaa = true;
        }
    }
    if (badresponse) {
        iodev_printf(&port->device, "device did not respond to reset command properly\n", port);
        THROW(ERR_IO);
    }
    // Some devices also send its ID when it resets. We throw that away here.
    while (1) {
        status_t status = stream_waitchar((char *)&res, &port->stream, PS2_TIMEOUT);
        if (status == ERR_IO) {
            break;
        } else if (status != OK) {
            THROW(status);
        }
    }
    // Identify device
    enum devtype type = DEVTYPE_KBD;
    {
        status_t status = identifydevice(&type, port);
        if (status == ERR_IO) {
            type = DEVTYPE_KBD;
            iodev_printf(&port->device, "identification failed due to I/O error. assuming it's keyboard.\n", port);
        } else if (status != OK) {
            THROW(status);
        }
    }
    switch (type) {
        case DEVTYPE_KBD:
            TRY(ps2kbd_init(port));
            break;
        case DEVTYPE_MOUSE:
            iodev_printf(&port->device, "mouse is not supported yet\n");
            break;
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION ps2port_register(struct ps2port *port_out, struct stream_ops const *ops, void *data) {
FAILABLE_PROLOGUE
    TRY(iodev_register(&port_out->device, IODEV_TYPE_PS2PORT, port_out));
    port_out->node.data = port_out;
    port_out->ops = NULL;
    port_out->stream.ops = ops;
    port_out->stream.data = data;
    assert(port_out->stream.ops->read == ps2port_stream_op_read);
    QUEUE_INIT_FOR_ARRAY(&port_out->recvqueue, port_out->recvqueuebuf);
    list_insertback(&s_ports, &port_out->node, port_out);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

void ps2_initdevices(void) {
    // XXX: Use iodev to enumerate devices instead.
    for (struct list_node *devicenode = s_ports.front; devicenode != NULL; devicenode = devicenode->next) {
        struct ps2port *port = devicenode->data;
        status_t status = initdev(port); 
        if (status != OK) {
            iodev_printf(&port->device, "failed to initialize (error %d)\n", status);
        }
    }
}

void ps2port_receivedbyte(struct ps2port *port, uint8_t byte) {
    if (port->ops == NULL) {
        status_t status = QUEUE_ENQUEUE(&port->recvqueue, &byte);
        if (status != OK) {
            iodev_printf(&port->device, "failed to enqueue data from the device (error %d)\n", status);
        }
    } else {
        status_t status = port->ops->bytereceived(port, byte);
        if (status != OK) {
            iodev_printf(&port->device, "error occured while processing received data from the device (error %d)\n", status);
        }
    }

}
