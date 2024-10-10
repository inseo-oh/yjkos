#pragma once
#include <kernel/io/stream.h>
#include <kernel/io/iodev.h>
#include <kernel/io/kbd.h>
#include <kernel/status.h>
#include <kernel/lib/queue.h>
#include <kernel/lib/list.h>
#include <stdbool.h>
#include <stdint.h>

enum {
    PS2_TIMEOUT = 200,

    PS2_CMD_IDENTIFY         = 0xf2,
    PS2_CMD_ENABLESCANNING   = 0xf4,
    PS2_CMD_DISABLESCANNING  = 0xf5,
    PS2_CMD_RESET            = 0xff,

    PS2_RESPONSE_ACK    = 0xfa,
    PS2_RESPONSE_RESEND = 0xfe,
};

typedef struct ps2port ps2port_t;
typedef struct ps2port_ops ps2port_ops_t;
struct ps2port_ops {
    FAILABLE_FUNCTION (*bytereceived)(ps2port_t *port, uint8_t byte);
};
struct ps2port {
    iodev_t device;
    stream_t stream;
    list_node_t node;
    // Bytes received from a PS/2 device goes to either:
    // - When ops is set(=Device-specific driver is ready), it goes to that bytereceived callback.
    // - Otherwise it goes into internal queue, which then can be read using `file` field and kernel's file API.
    ps2port_ops_t const *ops;
    queue_t recvqueue;
    uint8_t recvqueuebuf[127];
    void *devicedata;
};

// Put this macro at the beginning of stream ops for the device:
#define PS2_COMMON_STREAM_CALLBACKS \
    .read = ps2port_stream_op_read

FAILABLE_FUNCTION ps2port_stream_op_read(size_t *size_out, stream_t *self, void *buf, size_t size);
// Note that `device->file`'s read callback must be set to ps2port_op_fileread.
FAILABLE_FUNCTION ps2port_register(ps2port_t *port_out, stream_ops_t const *ops, void *data);
void ps2port_receivedbyte(ps2port_t *port, uint8_t byte);
void ps2_initdevices(void);

