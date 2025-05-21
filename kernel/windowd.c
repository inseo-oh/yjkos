#include "windowd.h"
#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/tasks/sched.h>
#include <kernel/tasks/thread.h>
#include <kernel/version.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BYTEORDER_MSB_FIRST,
    BYTEORDER_LSB_FIRST,
} PROTO_BYTEORDER;

struct connection {
    struct stream *stream;
    PROTO_BYTEORDER byteorder;
};

/*******************************************************************************
 * Protocol handling
 ******************************************************************************/

static size_t proto_pad(size_t len) {
    return (4 - (len % 4)) % 4;
}

static void proto_send_unused(struct connection *conn, size_t count) {
    for (size_t i = 0; i < count; i++) {
        int ret = stream_put_char(conn->stream, 0);
        if (ret < 0) {
            co_printf("windowd: Failed to send data to the client\n");
            arch_hcf();
        }
    }
}

static void proto_send_card8(struct connection *conn, uint8_t val) {
    int ret = stream_put_char(conn->stream, val);
    if (ret < 0) {
        co_printf("windowd: Failed to send data to the client\n");
        arch_hcf();
    }
}

static void proto_send_card16(struct connection *conn, uint16_t val) {
    uint8_t msb = val >> 8;
    uint8_t lsb = val;
    switch (conn->byteorder) {
    case BYTEORDER_MSB_FIRST:
        proto_send_card8(conn, msb);
        proto_send_card8(conn, lsb);
        break;
    case BYTEORDER_LSB_FIRST:
        proto_send_card8(conn, lsb);
        proto_send_card8(conn, msb);
        break;
    }
}

static void proto_send_card32(struct connection *conn, uint32_t val) {
    uint16_t msb = val >> 16;
    uint16_t lsb = val;
    switch (conn->byteorder) {
    case BYTEORDER_MSB_FIRST:
        proto_send_card16(conn, msb);
        proto_send_card16(conn, lsb);
        break;
    case BYTEORDER_LSB_FIRST:
        proto_send_card16(conn, lsb);
        proto_send_card16(conn, msb);
        break;
    }
}

static void proto_send_string8(struct connection *conn, char const *str, size_t len) {
    assert(strlen(str) <= len);
    for (size_t i = 0; i < len; i++) {
        proto_send_card8(conn, str[i]);
    }
}

static void proto_recv_unused(struct connection *conn, size_t count) {
    for (size_t i = 0; i < count; i++) {
        stream_get_char(conn->stream);
    }
}

static uint8_t proto_recv_card8(struct connection *conn) {
    return stream_get_char(conn->stream);
}

static uint16_t proto_recv_card16(struct connection *conn) {
    uint8_t val0 = proto_recv_card8(conn);
    uint8_t val1 = proto_recv_card8(conn);
    switch (conn->byteorder) {
    case BYTEORDER_MSB_FIRST:
        return ((uint32_t)val0 << 8) | val1;
    case BYTEORDER_LSB_FIRST:
        return ((uint32_t)val1 << 8) | val0;
    }
    assert(false);
}

static uint32_t proto_recv_card32(struct connection *conn) {
    uint16_t val0 = proto_recv_card16(conn);
    uint16_t val1 = proto_recv_card16(conn);
    switch (conn->byteorder) {
    case BYTEORDER_MSB_FIRST:
        return ((uint32_t)val0 << 16) | val1;
    case BYTEORDER_LSB_FIRST:
        return ((uint32_t)val1 << 16) | val0;
    }
    assert(false);
}

static char *proto_recv_string8(struct connection *conn, size_t len) {
    char *str = heap_calloc(sizeof(char), len + 1, HEAP_FLAG_ZEROMEMORY);
    if (str == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        str[i] = (char)proto_recv_card8(conn);
    }
    str[len] = '\0';
    return str;
}

struct proto_connection_setup {
    PROTO_BYTEORDER byteorder;
    uint16_t protocol_major_version;
    uint16_t protocol_minor_version;
    char *authorization_protocol_name;
    char *authorization_protocol_data;
};

[[nodiscard]] static int proto_recv_connection_setup(struct proto_connection_setup *out, struct stream *client) {
    struct connection conn;
    memset(out, 0, sizeof(*out));
    memset(&conn, 0, sizeof(conn));
    int byte_order_raw = stream_get_char(client);
    switch (byte_order_raw) {
    case 'B':
        out->byteorder = BYTEORDER_MSB_FIRST;
        break;
    case 'l':
        out->byteorder = BYTEORDER_LSB_FIRST;
        break;
    default:
        co_printf("windowd: WARNING: bad byteorder byte %u - assuming LSB first\n", byte_order_raw);
        out->byteorder = BYTEORDER_LSB_FIRST;
    }
    conn.stream = client;
    conn.byteorder = out->byteorder;
    proto_recv_unused(&conn, 1);
    out->protocol_major_version = proto_recv_card16(&conn);
    out->protocol_minor_version = proto_recv_card16(&conn);
    size_t n = proto_recv_card16(&conn);
    size_t d = proto_recv_card16(&conn);
    proto_recv_unused(&conn, 2);
    out->authorization_protocol_name = proto_recv_string8(&conn, n);
    proto_recv_unused(&conn, proto_pad(n));
    out->authorization_protocol_data = proto_recv_string8(&conn, d);
    proto_recv_unused(&conn, proto_pad(d));
    if ((out->authorization_protocol_name == NULL) || (out->authorization_protocol_data == NULL)) {
        goto oom;
    }
    return 0;
oom:
    heap_free(out->authorization_protocol_name);
    heap_free(out->authorization_protocol_data);
    return -ENOMEM;
}

static void proto_send_connection_refuse(struct connection *conn, char const *reason, uint16_t protocol_major_version, uint16_t protocol_minor_version) {
    size_t n = strlen(reason);
    assert(n <= 255);
    size_t p = proto_pad(n);
    proto_send_card8(conn, 0); /* Failed */
    proto_send_card8(conn, n);
    proto_send_card16(conn, protocol_major_version);
    proto_send_card16(conn, protocol_minor_version);
    proto_send_card16(conn, (n + p) / 4);
    proto_send_string8(conn, reason, n);
    proto_send_unused(conn, p);
}

static void proto_send_connection_accept(struct connection *conn, uint16_t protocol_major_version, uint16_t protocol_minor_version, uint32_t release_number, size_t formats_len, char const *vendor, size_t screens_len) {
    size_t n = formats_len;
    size_t v = strlen(vendor);
    size_t m = screens_len;
    size_t p = proto_pad(v);
    proto_send_card8(conn, 1); /* Success */
    proto_send_unused(conn, 1);
    proto_send_card16(conn, protocol_major_version);
    proto_send_card16(conn, protocol_minor_version);
    proto_send_card16(conn, 8 + (2 * n) + ((v + p + m) / 4));
    proto_send_card32(conn, release_number);
    /* TODO: This thing is incomplete */
}

#define REASON_FOOTER \
    "\n[YJK Operating System " YJKOS_RELEASE "-" YJKOS_VERSION "]\n"

[[nodiscard]] static int proto_handle_connection_setup(struct connection *out, struct stream *client) {
    memset(out, 0, sizeof(*out));
    int ret = 0;
    struct proto_connection_setup setup;
    ret = proto_recv_connection_setup(&setup, client);
    if (ret < 0) {
        return ret;
    }
    out->stream = client;
    out->byteorder = setup.byteorder;
    co_printf("windowd: protocol version %u.%u\n", setup.protocol_major_version, setup.protocol_minor_version);

    proto_send_connection_refuse(out, "I hate you" REASON_FOOTER, setup.protocol_major_version, setup.protocol_minor_version);

    ret = 0;
    return ret;
}

static void tmain(void *arg) {
    (void)arg;
    arch_irq_enable();
    struct list *devlst = iodev_get_list(IODEV_TYPE_TTY);
    if ((devlst == NULL) || (devlst->front == NULL)) {
        co_printf("windowd: no serial device available\n");
        return;
    }
    struct iodev *clientdev = devlst->front->data;
    struct tty *clienttty = clientdev->data;
    struct stream *client = tty_get_stream(clienttty);

    co_printf("windowd: listening commands on serial1\n");
    struct connection conn;
    int ret = proto_handle_connection_setup(&conn, client);
    if (ret < 0) {
        co_printf("windowd: failed to setup connection (error %d)\n", ret);
        arch_hcf();
    }
    co_printf("windowd: proto_handle_connection_setup complete\n");
    while (1) {
        co_printf("%x\n", stream_get_char(client));
    }
    arch_hcf();
}

void windowd_start(void) {
    bool thread_started = false;
    struct thread *thread = thread_create(THREAD_STACK_SIZE, tmain, NULL);
    if (thread == NULL) {
        co_printf("not enough memory to create thread\n");
        goto die;
    }
    int ret = sched_queue(thread);
    if (ret < 0) {
        co_printf("failed to queue thread (error %d)\n", ret);
        goto die;
    }
    thread_started = true;
    return;
die:
    if (thread_started) {
        thread->shutdown = true;
    }
}
