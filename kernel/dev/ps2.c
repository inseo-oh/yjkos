#include <assert.h>
#include <errno.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/iodelay.h>
#include <kernel/dev/ps2.h>
#include <kernel/dev/ps2kbd.h>
#include <kernel/io/co.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/queue.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

static struct list s_ports;

NODISCARD ssize_t ps2port_stream_op_read(struct stream *self, void *buf, size_t size) {
    struct ps2port *port = self->data;
    size_t writtensize = 0;
    ////////////////////////////////////////////////////////////////////////
    bool prev_interrupts = arch_interrupts_disable();
    arch_iodelay();
    for (size_t idx = 0; idx < size; idx++) {
        bool ok = QUEUE_DEQUEUE(&((uint8_t *)buf)[idx], &port->recvqueue);
        if (!ok) {
            break;
        }
        writtensize++;
    }
    interrupts_restore(prev_interrupts);
    ////////////////////////////////////////////////////////////////////////
    return (ssize_t)writtensize;
}

/*
 * Returns 0, or IOERROR_~ value on failure.
 */
NODISCARD static int send_and_wait_ack(struct ps2port *port, uint8_t cmd) {
    int ret = stream_putchar(&port->stream, cmd);
    if (ret < 0) {
        return ret;
    }
    ret = stream_waitchar(&port->stream, PS2_TIMEOUT);
    if (ret < 0) {
        return ret;
    }
    if (ret != PS2_RESPONSE_ACK) {
        iodev_printf(&port->device, "command %#x - expected ACK(0xfa), got %#x\n", cmd, ret);
        return -EIO;
    }
    return 0;
}

#define DEVICETYPE_KEYBOARD 'K'
#define DEVICETYPE_MOUSE 'M'

/*
 * Returns DEVICETYPE_~ values, or IOERROR_~ value on failure.
 */
static int identify_device(struct ps2port *port) {
    int ret;
    ret = send_and_wait_ack(port, PS2_CMD_DISABLESCANNING);
    if (ret < 0) {
        goto out;
    }
    ret = send_and_wait_ack(port, PS2_CMD_IDENTIFY);
    int result = DEVICETYPE_KEYBOARD;
    size_t identlen = 0;
    uint8_t ident[2];
    for (size_t i = 0; i < sizeof(ident) / sizeof(*ident); i++) {
        int ret = stream_waitchar(&port->stream, PS2_TIMEOUT);
        if (ret == STREAM_EOF) {
            break;
        }
        if (ret < 0) {
            goto out;
        }
        ident[i] = ret;
        identlen++;
    }
    switch (identlen) {
    case 0:
        break;
    case 1:
        switch (ident[0]) {
        case 0x00:
        case 0x03:
        case 0x04:
            result = DEVICETYPE_MOUSE;
            break;
        default:
            iodev_printf(&port->device, "unknown device ID %#x - assuming it's keyboard\n", ident[0]);
        }
        break;
    default:
        assert(identlen == 2);
        if (ident[0] != 0xab) {
            iodev_printf(&port->device, "unknown device ID %#x %#x - assuming it's keyboard\n", ident[0], ident[1]);
        }
        break;
    }
    ret = result;
out: {
    int ret = send_and_wait_ack(port, PS2_CMD_ENABLESCANNING);
    if (ret != 0) {
        iodev_printf(&port->device, "scanning couldn't be enabled(error %d)\n", ret);
    }
}
    return ret;
}

static int reset_device(struct ps2port *port) {
    // Send reset command
    bool reset_ack = false;
    bool reset_aa = false;
    bool bad_response = false;
    int ret = 0;
    ret = stream_putchar(&port->stream, PS2_CMD_RESET);
    if (ret < 0) {
        goto fail_bad_ret;
    }
    // After successful reset, we should receive 0xfa, 0xaa response
    for (size_t i = 0; i < 2; i++) {
        ret = stream_waitchar(&port->stream, PS2_TIMEOUT);
        if (ret < 0) {
            goto fail_bad_ret;
        }
        if (ret == PS2_RESPONSE_ACK) {
            if (reset_ack) {
                bad_response = true;
                break;
            }
            reset_ack = true;
        }
        if (ret == 0xaa) {
            if (reset_aa) {
                bad_response = true;
                break;
            }
            reset_aa = true;
        }
    }
    if (bad_response) {
        iodev_printf(&port->device, "device did not respond to reset command properly\n");
        goto fail_io;
    }
fail_bad_ret:
    goto out;
fail_io:
    ret = -EIO;
out:
    return ret;
}

/*
 * Returns 0, or IOERROR_~ value on failure.
 */
static int init_device(struct ps2port *port) {
    // Send reset command
    int ret = 0;
    ret = reset_device(port);
    if (ret < 0) {
        goto fail_bad_ret;
    }
    // Some devices also send its ID when it resets. We throw that away here.
    while (1) {
        int ret = stream_waitchar(&port->stream, PS2_TIMEOUT);
        if (ret == STREAM_EOF) {
            break;
        }
        if (ret < 0) {
            return ret;
        }
    }
    // Identify device
    int type = DEVICETYPE_KEYBOARD;
    {
        ret = identify_device(port);
        if (ret == -EIO) {
            ret = DEVICETYPE_KEYBOARD;
            iodev_printf(&port->device, "identification failed due to I/O error. assuming it's keyboard.\n", port);
        } else if (ret < 0) {
            return ret;
        } else {
            type = ret;
        }
    }
    switch (type) {
    case DEVICETYPE_KEYBOARD:
        ret = ps2kbd_init(port);
        if (ret < 0) {
            goto fail_bad_ret;
        }
        break;
    case DEVICETYPE_MOUSE:
        iodev_printf(&port->device, "mouse is not supported yet\n");
        break;
    default:
        assert(false);
    }
    goto out;
fail_bad_ret:
    goto out;
out:
    return ret;
}

NODISCARD int ps2port_register(struct ps2port *port_out, struct stream_ops const *ops, void *data) {
    port_out->ops = NULL;
    port_out->stream.ops = ops;
    port_out->stream.data = data;
    assert(port_out->stream.ops->read == ps2port_stream_op_read);
    QUEUE_INIT_FOR_ARRAY(&port_out->recvqueue, port_out->recvqueuebuf);
    list_insertback(&s_ports, &port_out->node, port_out);
    return iodev_register(&port_out->device, IODEV_TYPE_PS2PORT, port_out);
}

void ps2_initdevices(void) {
    // XXX: Use iodev to enumerate devices instead.
    LIST_FOREACH(&s_ports, devicenode) {
        struct ps2port *port = devicenode->data;
        int ret = init_device(port);
        if (ret < 0) {
            iodev_printf(&port->device, "failed to initialize (error %d)\n", ret);
        }
    }
}

void ps2port_receivedbyte(struct ps2port *port, uint8_t byte) {
    if (port->ops == NULL) {
        int ret = QUEUE_ENQUEUE(&port->recvqueue, &byte);
        if (ret < 0) {
            iodev_printf(&port->device, "failed to enqueue data from the device (error %d)\n", ret);
        }
    } else {
        int ret = port->ops->bytereceived(port, byte);
        if (ret < 0) {
            iodev_printf(&port->device, "error occured while processing received data from the device (error %d)\n", ret);
        }
    }
}
