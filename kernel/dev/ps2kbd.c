#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/dev/ps2.h>
#include <kernel/dev/ps2kbd.h>
#include <kernel/io/iodev.h>
#include <kernel/io/kbd.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static bool const CONFIG_COMMDEBUG = false;

enum inputstate {
    INPUTSTATE_DEFAULT,
    INPUTSTATE_WAITING_RESPONSEDATA,
    INPUTSTATE_WAITING_E0EXT,
    INPUTSTATE_WAITING_E1EXT1,
    INPUTSTATE_WAITING_E1EXT2,
    INPUTSTATE_WAITING_NORMALRELEASE,
    INPUTSTATE_WAITING_E0RELEASE,
    INPUTSTATE_WAITING_E1RELEASE1,
    INPUTSTATE_WAITING_E1RELEASE2,
};

enum {
    MAX_RESEND_COUNT = 3,

    CMD_SETLEDS      = 0xed,
    CMD_ECHO         = 0xee,
    CMD_SCANCODESET  = 0xf0,

    RESPONSE_ECHO    = 0xee,
};

enum cmdstate {
    CMDSTATE_QUEUED,
    CMDSTATE_WAITINGRESPONSE,
    CMDSTATE_SUCCESS,
    CMDSTATE_FAILED,
};

struct cmdcontext {
    _Atomic(enum cmdstate) state;
    struct list_node node;
    bool noretry, async;
    uint8_t responsedata;
    uint8_t cmdbyte, resendcount, needdata;
};

// Print Screen key related flags
static uint8_t const FLAG_FAKELSHIFT_DOWN     = 1 << 0;
static uint8_t const FLAG_FAKENUMPADMUL_DOWN = 1 << 1;
// Pause key related flags
static uint8_t const FLAG_FAKELCTRL_DOWN   = 1 << 2;
static uint8_t const FLAG_FAKENUMLOCK_DOWN = 1 << 3;

struct kbdcontext {
    struct kbddev device;
    enum inputstate state;
    struct ps2port *port;
    struct list cmdqueue;
    uint8_t flags, keybytes[8], nextkeybyteindex;
};

static void cmdfinished(struct ps2port *port, enum cmdstate finalstate) {
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    struct list_node *cmdnode = list_removeback(&ctx->cmdqueue);
    assert(cmdnode);
    struct cmdcontext *cmd = cmdnode->data;
    cmd->state = finalstate;
    if (cmd->async) {
        heap_free(cmd);
    }
    // Send next command
    cmdnode = ctx->cmdqueue.back;
    if (!cmdnode) {
        return;
    }
    cmd = cmdnode->data;
    cmd->state = CMDSTATE_WAITINGRESPONSE;
    if (CONFIG_COMMDEBUG) {
        iodev_printf(&ctx->device.iodev, "sending command %#x from queue\n", cmd->cmdbyte);
    }
    int ret = stream_putchar(&port->stream, cmd->cmdbyte);
    if (ret < 0) {
        iodev_printf(
            &ctx->device.iodev,
            "failed to send command %#x from queue\n", cmd->cmdbyte);
        cmd->state = CMDSTATE_FAILED;
    }

}

enum {
    KEYMAP_FLAG_SHIFT    = 1 << 0,
    KEYMAP_FLAG_CAPSLOCK = 1 << 1,
    KEYMAP_FLAG_NUMLOCK  = 1 << 2,
};

static enum kbd_key const KEYMAP_DEFAULT[256] = {
    [0x76] = KBD_KEY_ESCAPE,
    [0x05] = KBD_KEY_F1,
    [0x06] = KBD_KEY_F2,
    [0x04] = KBD_KEY_F3,
    [0x0c] = KBD_KEY_F4,
    [0x03] = KBD_KEY_F5,
    [0x0b] = KBD_KEY_F6,
    [0x83] = KBD_KEY_F7,
    [0x0a] = KBD_KEY_F8,
    [0x01] = KBD_KEY_F9,
    [0x09] = KBD_KEY_F10,
    [0x78] = KBD_KEY_F11,
    [0x07] = KBD_KEY_F12,

    [0x7e] = KBD_KEY_SCROLL_LOCK,

    [0x0e] = KBD_KEY_BACK_TICK,
    [0x16] = KBD_KEY_1,
    [0x1e] = KBD_KEY_2,
    [0x26] = KBD_KEY_3,
    [0x25] = KBD_KEY_4,
    [0x2e] = KBD_KEY_5,
    [0x36] = KBD_KEY_6,
    [0x3d] = KBD_KEY_7,
    [0x3e] = KBD_KEY_8,
    [0x46] = KBD_KEY_9,
    [0x45] = KBD_KEY_0,
    [0x4e] = KBD_KEY_MINUS,
    [0x55] = KBD_KEY_EQUALS,
    [0x66] = KBD_KEY_BACKSPACE,

    [0x0d] = KBD_KEY_TAB,
    [0x15] = KBD_KEY_Q,
    [0x1d] = KBD_KEY_W,
    [0x24] = KBD_KEY_E,
    [0x2d] = KBD_KEY_R,
    [0x2c] = KBD_KEY_T,
    [0x35] = KBD_KEY_Y,
    [0x3c] = KBD_KEY_U,
    [0x43] = KBD_KEY_I,
    [0x44] = KBD_KEY_O,
    [0x4d] = KBD_KEY_P,
    [0x54] = KBD_KEY_OPEN_BRACKET,
    [0x5b] = KBD_KEY_CLOSE_BRACKET,
    [0x5d] = KBD_KEY_BASKSLASH,

    [0x58] = KBD_KEY_CAPS_LOCK,
    [0x1c] = KBD_KEY_A,
    [0x1b] = KBD_KEY_S,
    [0x23] = KBD_KEY_D,
    [0x2b] = KBD_KEY_F,
    [0x34] = KBD_KEY_G,
    [0x33] = KBD_KEY_H,
    [0x3b] = KBD_KEY_J,
    [0x42] = KBD_KEY_K,
    [0x4b] = KBD_KEY_L,
    [0x4c] = KBD_KEY_SEMICOLON,
    [0x52] = KBD_KEY_QUOTE,
    [0x5a] = KBD_KEY_ENTER,

    [0x12] = KBD_KEY_LSHIFT,
    [0x1a] = KBD_KEY_Z,
    [0x22] = KBD_KEY_X,
    [0x21] = KBD_KEY_C,
    [0x2a] = KBD_KEY_V,
    [0x32] = KBD_KEY_B,
    [0x31] = KBD_KEY_N,
    [0x3a] = KBD_KEY_M,
    [0x41] = KBD_KEY_COMMA,
    [0x49] = KBD_KEY_DOT,
    [0x4a] = KBD_KEY_SLASH,
    [0x59] = KBD_KEY_RSHIFT,
    
    [0x14] = KBD_KEY_LCTRL,
    [0x11] = KBD_KEY_LALT,
    [0x29] = KBD_KEY_SPACE,

    [0x77] = KBD_KEY_NUM_LOCK,
    [0x7c] = KBD_KEY_NUMPAD_MUL,
    [0x7b] = KBD_KEY_NUMPAD_SUB,
    [0x6c] = KBD_KEY_NUMPAD_7,
    [0x75] = KBD_KEY_NUMPAD_8,
    [0x7d] = KBD_KEY_NUMPAD_9,
    [0x6b] = KBD_KEY_NUMPAD_4,
    [0x73] = KBD_KEY_NUMPAD_5,
    [0x74] = KBD_KEY_NUMPAD_6,
    [0x79] = KBD_KEY_NUMPAD_ADD,
    [0x69] = KBD_KEY_NUMPAD_1,
    [0x72] = KBD_KEY_NUMPAD_2,
    [0x7a] = KBD_KEY_NUMPAD_3,
    [0x70] = KBD_KEY_NUMPAD_0,
    [0x71] = KBD_KEY_NUMPAD_POINT,
};

static enum kbd_key const KEYMAP_E0[256] = {
    [0x1f] = KBD_KEY_LSUPER,
    [0x11] = KBD_KEY_RALT,
    [0x27] = KBD_KEY_RSUPER,
    [0x2f] = KBD_KEY_MENU,
    [0x14] = KBD_KEY_RCTRL,

    [0x70] = KBD_KEY_INSERT,
    [0x71] = KBD_KEY_DELETE,
    [0x6c] = KBD_KEY_HOME,
    [0x69] = KBD_KEY_END,
    [0x7d] = KBD_KEY_PAGE_UP,
    [0x7a] = KBD_KEY_PAGE_DOWN,
    [0x75] = KBD_KEY_UP,
    [0x72] = KBD_KEY_DOWN,
    [0x6b] = KBD_KEY_LEFT,
    [0x74] = KBD_KEY_RIGHT,

    [0x4a] = KBD_KEY_NUMPAD_DIV,
    [0x5a] = KBD_KEY_NUMPAD_ENTER,
};

// TODO:
// - Multimedia keys

#undef KEY_NORMAL
#undef KEY_WITH_SHIFTED
#undef KEY_WITH_NUMLOCK

static void reportbadscancode(struct ps2port *port) {
    static char const *MESSAGE_HEADER = "bad scancode sequence";

    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    // TODO: Find better ways to print than below mess
    switch(ctx->nextkeybyteindex) {
        case 1:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x\n", MESSAGE_HEADER,
                ctx->keybytes[0]);
            break;
        case 2:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1]);
            break;
        case 3:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], 
                ctx->keybytes[1], ctx->keybytes[2]);
            break;
        case 4:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x %#x %#x %#x\n", MESSAGE_HEADER,
                ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2], 
                ctx->keybytes[3]);
            break;
        case 5:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x %#x %#x %#x %#x\n", MESSAGE_HEADER,
                ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2],
                ctx->keybytes[3], ctx->keybytes[4]);
            break;
        case 6:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x %#x %#x %#x %#x %#x\n", MESSAGE_HEADER,
                ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2],
                ctx->keybytes[3], ctx->keybytes[4], ctx->keybytes[5]);
            break;
        case 7:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x %#x %#x %#x %#x %#x %#x\n", MESSAGE_HEADER,
                ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2],
                ctx->keybytes[3], ctx->keybytes[4], ctx->keybytes[5],
                ctx->keybytes[6]);
            break;
        case 8:
            iodev_printf(
                &ctx->device.iodev,
                "%s %#x %#x %#x %#x %#x %#x %#x %#x\n", MESSAGE_HEADER,
                ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2],
                ctx->keybytes[3], ctx->keybytes[4], ctx->keybytes[5],
                ctx->keybytes[6], ctx->keybytes[7]);
            break;
    }
}

static void keypressed(struct ps2port *port, enum kbd_key key) {
    if (key == KBD_KEY_INVALID) {
        reportbadscancode(port);
        return;
    }
    kbd_keypressed(key);
}

static void keyreleased(struct ps2port *port, enum kbd_key key) {
    if (key == KBD_KEY_INVALID) {
        reportbadscancode(port);
        return;
    }
    kbd_keyreleased(key);

}

static void checkprintscreen_press(struct ps2port *port) {
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    uint8_t flagmask = FLAG_FAKELSHIFT_DOWN | FLAG_FAKENUMPADMUL_DOWN;

    if ((ctx->flags & flagmask) == flagmask) {
        keypressed(port, KBD_KEY_PRINT_SCREEN);
    }
}

static void checkprintscreen_release(struct ps2port *port) {
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    uint8_t flagmask = FLAG_FAKELSHIFT_DOWN | FLAG_FAKENUMPADMUL_DOWN;

    if ((ctx->flags & flagmask) == 0) {
        keyreleased(port, KBD_KEY_PRINT_SCREEN);
    }
}

static void checkpause_press(struct ps2port *port) {
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    uint8_t flagmask = FLAG_FAKELCTRL_DOWN | FLAG_FAKENUMLOCK_DOWN;

    if ((ctx->flags & flagmask) == flagmask) {
        keypressed(port, KBD_KEY_PAUSE);
    }
}

static void checkpause_release(struct ps2port *port) {
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    uint8_t flagmask = FLAG_FAKELCTRL_DOWN | FLAG_FAKENUMLOCK_DOWN;

    if ((ctx->flags & flagmask) == 0) {
        keyreleased(port, KBD_KEY_PAUSE);
    }
}

static WARN_UNUSED_RESULT int ps2_op_bytereceived(
    struct ps2port *port, uint8_t byte)
{
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    if (ctx->state == INPUTSTATE_DEFAULT) {
        ctx->nextkeybyteindex = 0;
    }
    ctx->keybytes[ctx->nextkeybyteindex] = byte;
    ctx->nextkeybyteindex++;

    switch(byte) {
        case PS2_RESPONSE_ACK: {
            struct list_node *cmdnode = ctx->cmdqueue.back;
            if (cmdnode == NULL) {
                iodev_printf(&ctx->device.iodev, "received ACK, but there are no commands\n");
                break;
            }
            struct cmdcontext *cmd = cmdnode->data;
            if (cmd->state != CMDSTATE_WAITINGRESPONSE) {
                iodev_printf(&ctx->device.iodev, "received ACK on command %#x with incorrect state(expected %d, got %d)\n", CMDSTATE_WAITINGRESPONSE, cmd->state);
            } else {
                if (cmd->needdata) {
                    if (CONFIG_COMMDEBUG) {
                        iodev_printf(&ctx->device.iodev, "command %#x ACKed. waiting for more data...\n", cmd->cmdbyte);
                    }
                    ctx->state = INPUTSTATE_WAITING_RESPONSEDATA;
                } else {
                    if (CONFIG_COMMDEBUG) {
                        iodev_printf(&ctx->device.iodev, "command %#x finished\n", cmd->cmdbyte);
                    }
                    cmdfinished(port, CMDSTATE_SUCCESS);
                }
            }
            goto out;
        }
        case PS2_RESPONSE_RESEND: {
            struct list_node *cmdnode = ctx->cmdqueue.back;
            if (cmdnode == NULL) {
                iodev_printf(&ctx->device.iodev, "received RESEND, but there are no commands\n");
                break;
            }
            struct cmdcontext *cmd = cmdnode->data;
            if (cmd->state != CMDSTATE_WAITINGRESPONSE) {
                iodev_printf(&ctx->device.iodev, "received RESEND on command %#x with incorrect state(expected %d, got %d)\n", cmd->cmdbyte, CMDSTATE_WAITINGRESPONSE, cmd->state);
            } else {
                if ((0 < cmd->resendcount) && !cmd->noretry) {
                    iodev_printf(&ctx->device.iodev, "received RESEND for command %#x (remaining retries: %d)\n", cmd->cmdbyte, cmd->resendcount);
                    cmd->resendcount--;
                    int ret = stream_putchar(&port->stream, cmd->cmdbyte);
                    if (ret < 0) {
                        iodev_printf(
                            &ctx->device.iodev,
                            "failed to resend command %#x\n", cmd->cmdbyte);
                        cmd->state = CMDSTATE_FAILED;
                    }
                } else {
                    iodev_printf(
                        &ctx->device.iodev,
                        "command %#x failed\n", cmd->cmdbyte);
                    cmdfinished(port, CMDSTATE_FAILED);
                }
            }
            goto out;
        }
        case CMD_ECHO: {
            struct list_node *cmdnode = ctx->cmdqueue.back;
            if (cmdnode == NULL) {
                iodev_printf(&ctx->device.iodev, "received ECHO, but there are no commands\n");
                break;
            }
            struct cmdcontext *cmd = cmdnode->data;
            if (cmd->state != CMDSTATE_WAITINGRESPONSE) {
                iodev_printf(&ctx->device.iodev, "received ECHO on command %#x with incorrect state(expected %d, got %d)\n", cmd->cmdbyte, CMDSTATE_WAITINGRESPONSE, cmd->state);
            } else if (cmd->cmdbyte != CMD_ECHO) {
                iodev_printf(&ctx->device.iodev, "received ECHO on command %#x which isn't ECHO\n", cmd->cmdbyte);
            } else {
                if (CONFIG_COMMDEBUG) {
                    iodev_printf(&ctx->device.iodev, "command %#x finished\n", cmd->cmdbyte);
                }
                cmdfinished(port, CMDSTATE_SUCCESS);
            }
            goto out;
        }
    }
    switch(ctx->state) {
        case INPUTSTATE_WAITING_RESPONSEDATA: {
            struct list_node *cmdnode = ctx->cmdqueue.back;
            if (!cmdnode) {
                iodev_printf(&ctx->device.iodev, "received response data, but there are no commands\n");
                break;
            }
            struct cmdcontext *cmd = cmdnode->data;
            if (cmd->state != CMDSTATE_WAITINGRESPONSE) {
                iodev_printf(&ctx->device.iodev, "received response data on command %#x with incorrect state(expected %d, got %d)\n", cmd->cmdbyte, CMDSTATE_WAITINGRESPONSE, cmd->state);
            } else {
                if (CONFIG_COMMDEBUG) {
                    iodev_printf(&ctx->device.iodev, "command %#x finished\n", cmd->cmdbyte);
                }
                cmd->responsedata = byte;
                cmdfinished(port, CMDSTATE_SUCCESS);
            }
            ctx->state = INPUTSTATE_DEFAULT;
            break;
        }
        case INPUTSTATE_WAITING_E0EXT: {
            ctx->state = INPUTSTATE_DEFAULT;
            switch (byte) {
                case 0xf0:
                    ctx->state = INPUTSTATE_WAITING_E0RELEASE;
                    break;
                case 0x12:
                    ctx->flags |= FLAG_FAKELSHIFT_DOWN;
                    checkprintscreen_press(port);
                    break;
                case 0x7c:
                    ctx->flags |= FLAG_FAKENUMPADMUL_DOWN;
                    checkprintscreen_press(port);
                    break;
                default:
                    keypressed(port, KEYMAP_E0[byte]);
                    break;
            }
            break;
        }
        case INPUTSTATE_WAITING_E0RELEASE: {
            ctx->state = INPUTSTATE_DEFAULT;
            switch (byte) {
                case 0x12:
                    ctx->flags &= ~FLAG_FAKELSHIFT_DOWN;
                    checkprintscreen_release(port);
                    break;
                case 0x7c:
                    ctx->flags &= ~FLAG_FAKENUMPADMUL_DOWN;
                    checkprintscreen_release(port);
                    break;
                default:
                    keyreleased(port, KEYMAP_E0[byte]);
                    break;
            }
            break;
        }
        case INPUTSTATE_WAITING_E1EXT1: {
            ctx->state = INPUTSTATE_WAITING_E1EXT2;
            switch (byte) {
                case 0xf0:
                    ctx->state = INPUTSTATE_WAITING_E1RELEASE1;
                    break;
                case 0x14:
                    ctx->flags |= FLAG_FAKELCTRL_DOWN;
                    break;
                case 0x77:
                    ctx->flags |= FLAG_FAKENUMLOCK_DOWN;
                    break;
                default:
                    // Unknown key
                    break;
            }
            break;
        }
        case INPUTSTATE_WAITING_E1RELEASE1: {
            ctx->state = INPUTSTATE_WAITING_E1EXT2;
            switch (byte) {
                case 0x14:
                    ctx->flags &= ~FLAG_FAKELCTRL_DOWN;
                    break;
                case 0x77:
                    ctx->flags &= ~FLAG_FAKENUMLOCK_DOWN;
                    break;
                default:
                    // Unknown key
                    break;
            }
            break;
        }
        case INPUTSTATE_WAITING_E1EXT2: {
            ctx->state = INPUTSTATE_DEFAULT;
            switch (byte) {
                case 0xf0:
                    ctx->state = INPUTSTATE_WAITING_E1RELEASE2;
                    break;
                case 0x14:
                    ctx->flags |= FLAG_FAKELCTRL_DOWN;
                    checkpause_press(port);
                    break;
                case 0x77:
                    ctx->flags |= FLAG_FAKENUMLOCK_DOWN;
                    checkpause_press(port);
                    break;
                default:
                    reportbadscancode(port);
                    break;
            }
            break;
        }
        case INPUTSTATE_WAITING_E1RELEASE2: {
            ctx->state = INPUTSTATE_DEFAULT;
            switch (byte) {
                case 0x14:
                    ctx->flags &= ~FLAG_FAKELCTRL_DOWN;
                    checkpause_release(port);
                    break;
                case 0x77:
                    ctx->flags &= ~FLAG_FAKENUMLOCK_DOWN;
                    checkpause_release(port);
                    break;
                default:
                    reportbadscancode(port);
                    break;
            }
            break;
        }
        case INPUTSTATE_WAITING_NORMALRELEASE: {
            ctx->state = INPUTSTATE_DEFAULT;
            keyreleased(port, KEYMAP_DEFAULT[byte]);
            break;
        }
        case INPUTSTATE_DEFAULT: {
            switch(byte) {
                case 0xe0:
                    ctx->state = INPUTSTATE_WAITING_E0EXT;
                    break;
                case 0xe1:
                    ctx->state = INPUTSTATE_WAITING_E1EXT1;
                    break;
                case 0xf0: 
                    ctx->state = INPUTSTATE_WAITING_NORMALRELEASE;
                    break;
                default:
                    keypressed(port, KEYMAP_DEFAULT[byte]);
                    break;
            }
            break;
        }
    }
out:
    return 0;
}

/* 
 * If the command sends back additional result bytes, set `result_out` non-NULL
 * value where the result will be stored.
 */
static WARN_UNUSED_RESULT int requestcmd(
    uint8_t *result_out, struct ps2port *port, uint8_t cmdbyte, bool noretry,
    bool async)
{
    int ret = 0;
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    // async mode cannot be used to receive values
    assert(!async || (async && !result_out));
    struct cmdcontext *cmd = heap_alloc(sizeof(*cmd), HEAP_FLAG_ZEROMEMORY);
    if (cmd == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    cmd->cmdbyte = cmdbyte;
    cmd->resendcount = MAX_RESEND_COUNT;
    cmd->noretry = noretry;
    cmd->async = async;
    bool isqueueempty = ctx->cmdqueue.front == NULL;
    cmd->state = CMDSTATE_QUEUED;
    if (result_out) {
        cmd->needdata = true;
    }
    if (isqueueempty) {
        cmd->state = CMDSTATE_WAITINGRESPONSE;
        if (CONFIG_COMMDEBUG) {
            iodev_printf(&ctx->device.iodev, "executing command %#x\n", cmd->cmdbyte);
        }
    } else {
        if (CONFIG_COMMDEBUG) {
            iodev_printf(&ctx->device.iodev, "adding command %#x to Queue\n", cmd->cmdbyte);
        }
    }
    ////////////////////////////////////////////////////////////////////////////
    bool previnterrupts = arch_interrupts_disable();
    list_insertfront(&ctx->cmdqueue, &cmd->node, cmd);
    interrupts_restore(previnterrupts);
    ////////////////////////////////////////////////////////////////////////////
    if (isqueueempty) {
        ret = stream_putchar(&port->stream, cmdbyte);
        if (ret < 0) {
            return ret;
        }
    }
    if (!async) {
        while ((cmd->state != CMDSTATE_SUCCESS) && (cmd->state != CMDSTATE_FAILED)) {
        }
        if (cmd->state == CMDSTATE_FAILED) {
            ret = -EIO;
            goto fail;
        }
        if (cmd->needdata) {
            *result_out = cmd->responsedata;
        }
        heap_free(cmd);
    }
    goto out;
fail:
    heap_free(cmd);
out:
    return ret;
}

static WARN_UNUSED_RESULT int echo(struct ps2port *port, bool async) {
    return requestcmd(NULL, port, CMD_ECHO, false, async);
}

static uint8_t const LED_SCROLL = 1 << 0;
static uint8_t const LED_NUM    = 1 << 1;
static uint8_t const LED_CAPS   = 1 << 2;

static WARN_UNUSED_RESULT int setledstate(
    struct ps2port *port, uint8_t leds, bool async)
{
    int ret = 0;
    ret = requestcmd(
        NULL, port, CMD_SETLEDS, false, async);
    if (ret < 0) {
        goto fail;
    }
    ret = requestcmd(NULL, port, leds, true, async);
    if (ret < 0) {
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int getscancodeset(
    uint8_t *result_out, struct ps2port *port)
{
    assert(result_out != NULL);
    int ret = 0;
    ret = requestcmd(NULL, port, CMD_SCANCODESET, false, false);
    if (ret < 0) {
        goto fail;
    }
    ret = requestcmd(result_out, port, 0, true, false);
    if (ret < 0) {
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int setscancodeset(
    struct ps2port *port, uint8_t set, bool async)
{
    assert(set != 0);
    int ret = 0;
    ret = requestcmd(NULL, port, CMD_SCANCODESET, false, async);
    if (ret < 0) {
        goto fail;
    }
    ret = requestcmd(NULL, port, set, true, async);
    if (ret < 0) {
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int kbd_op_updateleds(
    struct kbddev *kbd, bool scroll, bool caps, bool num)
{
    struct kbdcontext *ctx = kbd->data;
    uint8_t led_state = 0;
    if (scroll) {
        led_state |= LED_SCROLL;
    }
    if (caps) {
        led_state |= LED_CAPS;
    }
    if (num) {
        led_state |= LED_NUM;
    }
    return setledstate(ctx->port, led_state, true);
}

static struct kbddev_ops const KEYBOARD_OPS = {
    .updateleds = kbd_op_updateleds,
};

static struct ps2port_ops const PS2_OPS = {
    .bytereceived = ps2_op_bytereceived,
};

WARN_UNUSED_RESULT int ps2kbd_init(struct ps2port *port) {
    int ret = 0;
    struct kbdcontext *ctx = heap_alloc(
        sizeof(*ctx), HEAP_FLAG_ZEROMEMORY);
    if (ctx == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    memset(ctx, 0, sizeof(*ctx));
    // Setup fake iodev for logging
    ctx->state = INPUTSTATE_DEFAULT;
    ctx->port = port;
    list_init(&ctx->cmdqueue);
    port->devicedata = ctx;
    port->ops = &PS2_OPS;

    iodev_printf(&port->device, "ps2kbd: testing echo\n");
    ret = echo(port, false);
    if (ret < 0) {
        goto fail;
    }
    iodev_printf(&port->device, "ps2kbd: resetting leds\n");
    ret = setledstate(port, 0, false);
    if (ret < 0) {
        goto fail;
    }

    bool scancodessupported[3] = {false, false, false};
    for (uint8_t set = 1; set <= 3; set++) {
        scancodessupported[set - 1] = false;
        int ret = setscancodeset(port, set, false);
        if (ret < 0) {
            iodev_printf(
                &port->device,
                "ps2kbd: scancode set %u test failed: set command error\n",
                set);
            continue;
        }
        uint8_t currentset;
        ret = getscancodeset(&currentset, port);
        if (ret < 0) {
            iodev_printf(
                &port->device,
                "ps2kbd: scancode set %u test failed: get command error\n",
                set);
            continue;
        }
        if (currentset != set) {
            iodev_printf(
                &port->device,
                "ps2kbd: scancode set %u test failed: got scancode set %u\n",
                set, currentset);
        } else {
            scancodessupported[set - 1] = true;
        }
    }
    if (!scancodessupported[1]) {
        iodev_printf(
            &port->device,
            "ps2kbd: WARNING: scancode set 2 does not seem to be supported.\n");
    }
    iodev_printf(
        &port->device, "ps2kbd: configuring scancode set\n");
    {
        int ret = setscancodeset(port, 2, false);
        if (ret < 0) {
            iodev_printf(
                &port->device,
                "ps2kbd: WARNING: couldn't set scancode set to 2.\n");
        }
    }
    ret = kbd_register(
        &ctx->device, &KEYBOARD_OPS, ctx);
    if (ret < 0) {
        goto fail;
    }
    /*
     * We can't undo kbd_register as of writing this code, so no further errors
     * are allowed.
     */
    goto out;
fail:
    heap_free(ctx);
out:
    return ret;
}
