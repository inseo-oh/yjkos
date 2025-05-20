#include "ps2ctrl.h"
#include "../ioport.h"
#include "../pic.h"
#include <assert.h>
#include <errno.h>
#include <kernel/dev/ps2.h>
#include <kernel/io/co.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/heap.h>
#include <kernel/ticktime.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/******************************** Configuration *******************************/

/*
 * Show communication between OS and PS/2 controller?
 */
static bool const CONFIG_COMM_DEBUG = false;

/******************************************************************************/

#define DATA_PORT 0x60
#define STATUS_PORT 0x64 /* Read only */
#define CMD_PORT 0x64    /* Write only */

/* PS/2 controller commands */
#define CMD_READ_CTRL_CONFIG 0x20
#define CMD_WRITE_CTRL_CONFIG 0x60
#define CMD_DISABLE_PORT1 0xa7
#define CMD_ENABLE_PORT1 0xa8
#define CMD_TEST_PORT1 0xa9
#define CMD_TEST_CTRL 0xaa
#define CMD_TEST_PORT0 0xab
#define CMD_DISABLE_PORT0 0xad
#define CMD_ENABLE_PORT0 0xae
#define CMD_WRITE_PORT1 0xd4

#define IRQ_PORT0 1
#define IRQ_PORT1 12

/* PS/2 controller configuration */
#define CONFIG_FLAG_PORT0_INT (1U << 0)
#define CONFIG_FLAG_PORT1_INT (1U << 1)
#define CONFIG_FLAG_SYS (1U << 2)
#define CONFIG_FLAG_PORT0_CLK_OFF (1U << 4)
#define CONFIG_FLAG_PORT1_CLK_OFF (1U << 5)
#define CONFIG_FLAG_PORT0_TRANS (1U << 6)

/* PS/2 controller status */
#define STATUS_FLAG_OUTBUF_FULL (1U << 0)
#define STATUS_FLAG_INBUF_FULL (1U << 1)
#define STATUS_FLAG_SYS (1U << 2)
#define STATUS_FLAG_CMD_DATA (1U << 3)
#define STATUS_TIMEOUT_ERR (1U << 6)
#define STATUS_PARITY_ERR (1U << 7)

struct portcontext {
    struct ps2port ps2port;
    uint8_t portidx;
    struct archi586_pic_irq_handler irqhandler;
};

[[nodiscard]] static int waitforrecv(void) {
    TICKTIME oldtime = g_ticktime;
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

[[nodiscard]] static int waitforsend(void) {
    TICKTIME oldtime = g_ticktime;
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

static int recv_from_ctrl(uint8_t *out) {
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

static int send_to_ctrl(uint8_t cmd) {
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

static int send_data_to_ctrl(uint8_t data) {
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
            int ret = send_to_ctrl(CMD_WRITE_PORT1);
            if (ret < 0) {
                return ret;
            }
        }
        int ret = send_data_to_ctrl(c);
        if (ret < 0) {
            return ret;
        }
    }
    return (ssize_t)size;
}

static void irqhandler(int irqnum, void *data) {
    struct portcontext *port = data;
    uint8_t value = archi586_in8(DATA_PORT);
    if (CONFIG_COMM_DEBUG) {
        co_printf("ps2: irq on port %u - data %#x\n", port->portidx, value);
    }
    ps2port_received_byte(&port->ps2port, value);
    archi586_pic_send_eoi(irqnum);
}

static struct stream_ops const OPS = {
    PS2_COMMON_STREAM_CALLBACKS,
    .write = stream_op_write,
};

[[nodiscard]] static int discovered_port(size_t port_index) {
    assert(port_index < 2);
    int ret = 0;
    struct portcontext *port = heap_alloc(sizeof(*port), HEAP_FLAG_ZEROMEMORY);
    if (port == NULL) {
        goto fail;
    }
    port->portidx = port_index;
    int irq;
    uint8_t enable_cmd;
    if (port_index == 0) {
        irq = IRQ_PORT0;
        enable_cmd = CMD_ENABLE_PORT0;
    } else {
        irq = IRQ_PORT1;
        enable_cmd = CMD_ENABLE_PORT1;
    }
    ret = send_to_ctrl(enable_cmd);
    if (ret < 0) {
        goto fail;
    }
    archi586_pic_register_handler(&port->irqhandler, irq, irqhandler, port);
    archi586_pic_unmask_irq(irq);
    ret = ps2port_register(&port->ps2port, &OPS, port);
    if (ret < 0) {
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
    return ret;
}

[[nodiscard]] static int disable_all(void) {
    int ret = 0;

    ret = send_to_ctrl(CMD_DISABLE_PORT0);
    if (ret < 0) {
        return ret;
    }
    ret = send_to_ctrl(CMD_DISABLE_PORT1);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static void empty_output_buffer(void) {
    while (archi586_in8(STATUS_PORT) & STATUS_FLAG_OUTBUF_FULL) {
        archi586_in8(DATA_PORT);
    }
}

[[nodiscard]] static int read_ctrl_config(uint8_t *out) {
    int ret = 0;
    uint8_t ctrl_config = 0;
    ret = send_to_ctrl(CMD_READ_CTRL_CONFIG);
    if (ret < 0) {
        goto out;
    }
    ret = recv_from_ctrl(&ctrl_config);
    if (ret < 0) {
        goto out;
    }
out:
    *out = ctrl_config;
    return ret;
}

static int write_ctrl_config(uint8_t ctrl_config) {
    int ret = 0;
    ret = send_to_ctrl(CMD_WRITE_CTRL_CONFIG);
    if (ret < 0) {
        goto out;
    }
    ret = send_data_to_ctrl(ctrl_config);
    if (ret < 0) {
        goto out;
    }
out:
    return ret;
}

static int init_port0_config(void) {
    int ret;
    uint8_t ctrl_config;
    ret = read_ctrl_config(&ctrl_config);
    if (ret < 0) {
        return ret;
    }
    ctrl_config &= ~(CONFIG_FLAG_PORT0_INT | CONFIG_FLAG_PORT0_TRANS | CONFIG_FLAG_PORT0_CLK_OFF);
    write_ctrl_config(ctrl_config);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static int init_port1_config(void) {
    int ret;
    uint8_t ctrl_config;

    ret = send_to_ctrl(CMD_ENABLE_PORT1);
    if (ret < 0) {
        return ret;
    }
    ret = read_ctrl_config(&ctrl_config);
    if (ret < 0) {
        return ret;
    }
    if (ctrl_config & CONFIG_FLAG_PORT1_CLK_OFF) {
        return -ENODEV;
    }

    ret = send_to_ctrl(CMD_DISABLE_PORT1);
    if (ret < 0) {
        return ret;
    }
    ret = read_ctrl_config(&ctrl_config);
    if (ret < 0) {
        return ret;
    }
    ctrl_config &= ~(CONFIG_FLAG_PORT1_INT | CONFIG_FLAG_PORT1_CLK_OFF);
    write_ctrl_config(ctrl_config);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static int ctrl_self_test(void) {
    int ret = 0;
    ret = send_to_ctrl(CMD_TEST_CTRL);
    if (ret < 0) {
        return ret;
    }
    uint8_t response;
    ret = recv_from_ctrl(&response);
    if (ret < 0) {
        return ret;
    }
    if (response != 0x55) {
        co_printf("ps2: controller self test failed(response: %#x)\n", response);
        ret = -EIO;
        return ret;
    }
    return 0;
}

static int port_self_test(int port) {
    uint8_t cmd = 0;
    uint8_t response;
    int ret;

    switch (port) {
    case 0:
        cmd = CMD_TEST_PORT0;
        break;
    case 1:
        cmd = CMD_TEST_PORT1;
        break;
    default:
        assert(false);
    }
    ret = send_to_ctrl(cmd);
    if (ret < 0) {
        return ret;
    }
    ret = recv_from_ctrl(&response);
    if (ret < 0) {
        return ret;
    }
    if (response != 0x00) {
        co_printf("ps2: port %d self test failed(response: %#x)\n", port, response);
        return -EIO;
    }
    return 0;
}

void archi586_ps2ctrl_init(void) {
    int ret;
    /* https://wiki.osdev.org/%228042%22_PS/2_Controller#Initialising_the_PS/2_Controller */

    /* Disable interrupts *****************************************************/
    archi586_pic_mask_irq(IRQ_PORT0);
    archi586_pic_mask_irq(IRQ_PORT1);

    /* Disable PS/2 devices ***************************************************/
    ret = disable_all();
    if (ret < 0) {
        goto fail;
    }
    /* Empty the output buffer ************************************************/
    empty_output_buffer();

    /* Reconfigure controller (Only configure port 0) *************************/
    uint8_t ctrl_config;
    ret = init_port0_config();
    if (ret < 0) {
        goto fail;
    }

    /* Run self-test **********************************************************/
    ret = ctrl_self_test();
    if (ret < 0) {
        goto fail;
    }

    /* Self-test may have resetted the controller, so we reconfigure Port 0 again. */
    ret = init_port0_config();
    if (ret < 0) {
        goto fail;
    }

    /* Check if it's dual-port controller. ************************************/
    int port_count = 2;
    ret = init_port1_config();
    if (ret < 0) {
        co_printf("ps2: failed to configure second port(error %d)\n", ret);
        port_count = 1;
    }
    co_printf("ps2: detected as %d-port controller\n", port_count);

    /* Test each port *********************************************************/
    bool port_ok[] = {true, true};
    bool any_ok = false;
    for (int i = 0; i < port_count; i++) {
        ret = port_self_test(0);
        if (ret < 0) {
            co_printf("ps2: port %d self test failed(error %d)\n", i, ret);
            port_ok[i] = false;
        }
        any_ok = true;
    }
    if (!any_ok) {
        ret = -EIO;
        goto fail;
    }

    /* Enable interrupts ******************************************************/
    ret = read_ctrl_config(&ctrl_config);
    if (ret < 0) {
        goto fail;
    }
    if (port_ok[0]) {
        ctrl_config |= CONFIG_FLAG_PORT0_INT;
    }
    if (port_ok[1]) {
        ctrl_config |= CONFIG_FLAG_PORT1_INT;
    }
    write_ctrl_config(ctrl_config);
    if (ret < 0) {
        goto fail;
    }

    /* Register ports *********************************************************/
    for (int i = 0; i < port_count; i++) {
        if (!port_ok[i]) {
            continue;
        }
        int ret = discovered_port(i);
        if (ret < 0) {
            co_printf("ps2: failed to register port %d\n", i);
        }
    }
    return;
fail:
    co_printf("ps2: error %d occured. aborting controller initialization\n", ret);
}
