#pragma once
#include <kernel/io/iodev.h>
#include <kernel/io/kbd.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/queue.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PS2_TIMEOUT 200

#define PS2_CMD_IDENTIFY 0xf2
#define PS2_CMD_ENABLESCANNING 0xf4
#define PS2_CMD_DISABLESCANNING 0xf5
#define PS2_CMD_RESET 0xff

#define PS2_RESPONSE_ACK 0xfa
#define PS2_RESPONSE_RESEND 0xfe

struct Ps2Port;
struct Ps2PortOps {
    int (*byte_received)(struct Ps2Port *port, uint8_t byte);
};
struct Ps2Port {
    struct IoDev device;
    struct Stream stream;
    struct List_Node node;
    /*
     * Bytes received from a PS/2 device goes to either:
     * - When ops is set(=Device-specific driver is ready), it goes to the
     *   bytereceived callback.
     * - Otherwise it goes into internal queue, which then can be read using
     *   `stream` field and kernel's stream API.
     */
    struct Ps2PortOps const *ops;
    struct Queue recvqueue;
    uint8_t recv_queue_buf[127];
    void *device_data;
};

/* Put this macro at the beginning of stream ops for the device: */
#define PS2_COMMON_STREAM_CALLBACKS \
    .Read = Ps2Port_StreamOpRead

[[nodiscard]] ssize_t Ps2Port_StreamOpRead(struct Stream *self, void *buf, size_t size);
/* Note that `device->file`'s read callback must be set to Ps2Port_StreamOpRead. */
[[nodiscard]] int Ps2Port_Register(struct Ps2Port *port_out, struct StreamOps const *ops, void *data);
void Ps2Port_ReceivedByte(struct Ps2Port *port, uint8_t byte);
void Ps2_InitDevices(void);
