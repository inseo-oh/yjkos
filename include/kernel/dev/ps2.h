#pragma once
#include <kernel/io/stream.h>
#include <kernel/io/iodev.h>
#include <kernel/io/kbd.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/queue.h>
#include <kernel/lib/list.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

enum {
    PS2_TIMEOUT = 200,

    PS2_CMD_IDENTIFY         = 0xf2,
    PS2_CMD_ENABLESCANNING   = 0xf4,
    PS2_CMD_DISABLESCANNING  = 0xf5,
    PS2_CMD_RESET            = 0xff,

    PS2_RESPONSE_ACK    = 0xfa,
    PS2_RESPONSE_RESEND = 0xfe,
};

struct ps2port;
struct ps2port_ops {
    WARN_UNUSED_RESULT int (*bytereceived)(struct ps2port *port, uint8_t byte);
};
struct ps2port {
    struct iodev device;
    struct stream stream;
    struct list_node node;
    /*
     * Bytes received from a PS/2 device goes to either:
     * - When ops is set(=Device-specific driver is ready), it goes to the
     *   bytereceived callback.
     * - Otherwise it goes into internal queue, which then can be read using
     *   `stream` field and kernel's stream API.
     */
    struct ps2port_ops const *ops;
    struct queue recvqueue;
    uint8_t recvqueuebuf[127];
    void *devicedata;
};

// Put this macro at the beginning of stream ops for the device:
#define PS2_COMMON_STREAM_CALLBACKS \
    .read = ps2port_stream_op_read

WARN_UNUSED_RESULT ssize_t ps2port_stream_op_read(
    struct stream *self, void *buf, size_t size);
// Note that `device->file`'s read callback must be set to ps2port_op_fileread.
WARN_UNUSED_RESULT int ps2port_register(
    struct ps2port *port_out, struct stream_ops const *ops, void *data);
void ps2port_receivedbyte(struct ps2port *port, uint8_t byte);
void ps2_initdevices(void);

