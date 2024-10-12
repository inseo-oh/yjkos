#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/dev/ps2.h>
#include <kernel/dev/ps2kbd.h>
#include <kernel/io/iodev.h>
#include <kernel/io/kbd.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static bool const CONFIG_COMMDEBUG = false;

typedef enum {
    STATE_DEFAULT,
    STATE_WAITING_RESPONSEDATA,
    STATE_WAITING_E0EXT,
    STATE_WAITING_E1EXT1,
    STATE_WAITING_E1EXT2,
    STATE_WAITING_NORMALRELEASE,
    STATE_WAITING_E0RELEASE,
    STATE_WAITING_E1RELEASE1,
    STATE_WAITING_E1RELEASE2,
} keyboardstate_t;

enum {
    MAX_RESEND_COUNT = 3,

    CMD_SETLEDS      = 0xed,
    CMD_ECHO         = 0xee,
    CMD_SCANCODESET  = 0xf0,

    RESPONSE_ECHO    = 0xee,
};

typedef enum {
    CMDSTATE_QUEUED,
    CMDSTATE_WAITINGRESPONSE,
    CMDSTATE_SUCCESS,
    CMDSTATE_FAILED,
} cmdstate_t;

struct cmdcontext {
    _Atomic(cmdstate_t) state;
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
    keyboardstate_t state;
    struct ps2port *port;
    struct list cmdqueue;
    uint8_t flags, keybytes[8], nextkeybyteindex;
};

static void cmdfinished(struct ps2port *port, cmdstate_t final_state) {
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    struct list_node *cmdnode = list_removeback(&ctx->cmdqueue);
    assert(cmdnode);
    struct cmdcontext *cmd = cmdnode->data;
    cmd->state = final_state;
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
    status_t status = stream_putchar(&port->stream, cmd->cmdbyte);
    if (status != OK) {
        iodev_printf(&ctx->device.iodev, "failed to send command %#x from queue\n", cmd->cmdbyte);
        cmd->state = CMDSTATE_FAILED;
    }

}

enum {
    KEYMAP_FLAG_SHIFT    = 1 << 0,
    KEYMAP_FLAG_CAPSLOCK = 1 << 1,
    KEYMAP_FLAG_NUMLOCK  = 1 << 2,
};

static kbd_key_t const KEYMAP_DEFAULT[256] = {
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

static kbd_key_t const KEYMAP_E0[256] = {
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
            iodev_printf(&ctx->device.iodev, "%s %#x\n", MESSAGE_HEADER, ctx->keybytes[0]);
            break;
        case 2:
            iodev_printf(&ctx->device.iodev, "%s %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1]);
            break;
        case 3:
            iodev_printf(&ctx->device.iodev, "%s %#x %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2]);
            break;
        case 4:
            iodev_printf(&ctx->device.iodev, "%s %#x %#x %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2], ctx->keybytes[3]);
            break;
        case 5:
            iodev_printf(&ctx->device.iodev, "%s %#x %#x %#x %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2], ctx->keybytes[3], ctx->keybytes[4]);
            break;
        case 6:
            iodev_printf(&ctx->device.iodev, "%s %#x %#x %#x %#x %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2], ctx->keybytes[3], ctx->keybytes[4], ctx->keybytes[5]);
            break;
        case 7:
            iodev_printf(&ctx->device.iodev, "%s %#x %#x %#x %#x %#x %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2], ctx->keybytes[3], ctx->keybytes[4], ctx->keybytes[5], ctx->keybytes[6]);
            break;
        case 8:
            iodev_printf(&ctx->device.iodev, "%s %#x %#x %#x %#x %#x %#x %#x %#x\n", MESSAGE_HEADER, ctx->keybytes[0], ctx->keybytes[1], ctx->keybytes[2], ctx->keybytes[3], ctx->keybytes[4], ctx->keybytes[5], ctx->keybytes[6], ctx->keybytes[7]);
            break;
    }
}

static void keypressed(struct ps2port *port, kbd_key_t key) {
    if (key == KBD_KEY_INVALID) {
        reportbadscancode(port);
        return;
    }
    kbd_keypressed(key);
}

static void keyreleased(struct ps2port *port, kbd_key_t key) {
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

static FAILABLE_FUNCTION ps2_op_bytereceived(struct ps2port *port, uint8_t byte) {
FAILABLE_PROLOGUE
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    if (ctx->state == STATE_DEFAULT) {
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
                    ctx->state = STATE_WAITING_RESPONSEDATA;
                } else {
                    if (CONFIG_COMMDEBUG) {
                        iodev_printf(&ctx->device.iodev, "command %#x finished\n", cmd->cmdbyte);
                    }
                    cmdfinished(port, CMDSTATE_SUCCESS);
                }
            }
            goto FAILABLE_EPILOGUE_LABEL;
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
                    status_t status = stream_putchar(&port->stream, cmd->cmdbyte);
                    if (status != OK) {
                        iodev_printf(&ctx->device.iodev, "failed to resend command %#x\n", cmd->cmdbyte);
                        cmd->state = CMDSTATE_FAILED;
                    }
                } else {
                    iodev_printf(&ctx->device.iodev, "command %#x failed\n", cmd->cmdbyte);
                    cmdfinished(port, CMDSTATE_FAILED);
                }
            }
            goto FAILABLE_EPILOGUE_LABEL;
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
            goto FAILABLE_EPILOGUE_LABEL;
        }
    }
    switch(ctx->state) {
        case STATE_WAITING_RESPONSEDATA: {
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
            ctx->state = STATE_DEFAULT;
            break;
        }
        case STATE_WAITING_E0EXT: {
            ctx->state = STATE_DEFAULT;
            switch (byte) {
                case 0xf0:
                    ctx->state = STATE_WAITING_E0RELEASE;
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
        case STATE_WAITING_E0RELEASE: {
            ctx->state = STATE_DEFAULT;
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
        case STATE_WAITING_E1EXT1: {
            ctx->state = STATE_WAITING_E1EXT2;
            switch (byte) {
                case 0xf0:
                    ctx->state = STATE_WAITING_E1RELEASE1;
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
        case STATE_WAITING_E1RELEASE1: {
            ctx->state = STATE_WAITING_E1EXT2;
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
        case STATE_WAITING_E1EXT2: {
            ctx->state = STATE_DEFAULT;
            switch (byte) {
                case 0xf0:
                    ctx->state = STATE_WAITING_E1RELEASE2;
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
        case STATE_WAITING_E1RELEASE2: {
            ctx->state = STATE_DEFAULT;
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
        case STATE_WAITING_NORMALRELEASE: {
            ctx->state = STATE_DEFAULT;
            keyreleased(port, KEYMAP_DEFAULT[byte]);
            break;
        }
        case STATE_DEFAULT: {
            switch(byte) {
                case 0xe0:
                    ctx->state = STATE_WAITING_E0EXT;
                    break;
                case 0xe1:
                    ctx->state = STATE_WAITING_E1EXT1;
                    break;
                case 0xf0: 
                    ctx->state = STATE_WAITING_NORMALRELEASE;
                    break;
                default:
                    keypressed(port, KEYMAP_DEFAULT[byte]);
                    break;
            }
            break;
        }
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

// If command sends back additional result bytes, set `result_out` non-NULL value where the result will be stored.
static FAILABLE_FUNCTION requestcmd(uint8_t *result_out, struct ps2port *port, uint8_t cmdbyte, bool noretry, bool async) {
FAILABLE_PROLOGUE
    struct kbdcontext *ctx = port->devicedata;
    assert(ctx);
    assert(!async || (async && !result_out)); // async mode cannot be used to receive values
    struct cmdcontext *cmd = heap_alloc(sizeof(*cmd), HEAP_FLAG_ZEROMEMORY);
    if (!cmd) {
        THROW(ERR_NOMEM);
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
        TRY(stream_putchar(&port->stream, cmdbyte));
    }
    if (!async) {
        while ((cmd->state != CMDSTATE_SUCCESS) && (cmd->state != CMDSTATE_FAILED)) {
        }
        if (cmd->state == CMDSTATE_FAILED) {
            THROW(ERR_IO);
        }
        if (cmd->needdata) {
            *result_out = cmd->responsedata;
        }
        heap_free(cmd);
    }

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION echo(struct ps2port *port, bool async) {
FAILABLE_PROLOGUE
    TRY(requestcmd(NULL, port, CMD_ECHO, false, async));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static uint8_t const LED_SCROLL = 1 << 0;
static uint8_t const LED_NUM    = 1 << 1;
static uint8_t const LED_CAPS   = 1 << 2;

static FAILABLE_FUNCTION setledstate(struct ps2port *port, uint8_t leds, bool async) {
FAILABLE_PROLOGUE
    TRY(requestcmd(NULL, port, CMD_SETLEDS, false, async));
    TRY(requestcmd(NULL, port, leds, true, async));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION getscancodeset(uint8_t *result_out, struct ps2port *port) {
FAILABLE_PROLOGUE
    assert(result_out);
    TRY(requestcmd(NULL, port, CMD_SCANCODESET, false, false));
    TRY(requestcmd(result_out, port, 0, true, false));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION setscancodeset(struct ps2port *port, uint8_t set, bool async) {
FAILABLE_PROLOGUE
    assert(set != 0);
    TRY(requestcmd(NULL, port, CMD_SCANCODESET, false, async));
    TRY(requestcmd(NULL, port, set, true, async));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION kbd_op_updateleds(struct kbddev *kbd, bool scroll, bool caps, bool num) {
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

static struct kbddev_ops const KEYBOARD_CALLBACKS = {
    .updateleds = kbd_op_updateleds,
};

static struct ps2port_ops const PS2_OPS = {
    .bytereceived = ps2_op_bytereceived,
};

FAILABLE_FUNCTION ps2kbd_init(struct ps2port *port) {
FAILABLE_PROLOGUE
    struct kbdcontext *ctx = heap_alloc(sizeof(*ctx), HEAP_FLAG_ZEROMEMORY);
    if (ctx == NULL) {
        THROW(ERR_NOMEM);
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DEFAULT;
    ctx->port = port;
    list_init(&ctx->cmdqueue);
    port->devicedata = ctx;
    port->ops = &PS2_OPS;

    iodev_printf(&port->device, "ps2kbd: testing echo\n");
    TRY(echo(port, false));
    iodev_printf(&port->device, "ps2kbd: resetting leds\n");
    TRY(setledstate(port, 0, false));

    bool scancodessupported[3] = {false, false, false};
    for (uint8_t set = 1; set <= 3; set++) {
        scancodessupported[set - 1] = false;
        status_t status = setscancodeset(port, set, false);
        if (status != OK) {
            iodev_printf(&port->device, "ps2kbd: scancode set %u test failed: set command error\n", set);
            continue;
        }
        uint8_t current_set;
        status = getscancodeset(&current_set, port);
        if (status != OK) {
            iodev_printf(&port->device, "ps2kbd: scancode set %u test failed: get command error\n", set);
            continue;
        }
        if (current_set != set) {
            iodev_printf(&port->device, "ps2kbd: scancode set %u test failed: got scancode set %u\n", set, current_set);
        } else {
            scancodessupported[set - 1] = true;
        }
    }
    if (!scancodessupported[1]) {
        iodev_printf(&port->device, "ps2kbd: scancode set 2 does not seem to be supported. keyboard may not work properly!\n");
    }
    iodev_printf(&port->device, "ps2kbd: configuring scancode set\n");
    {
        status_t status = setscancodeset(port, 2, false);
        if (status != OK) {
            iodev_printf(&port->device, "ps2kbd: couldn't set scancode set to 2. keyboard may not work properly!\n");
        }
    }
    TRY(kbd_register(&ctx->device, &KEYBOARD_CALLBACKS, ctx));
    // We can't undo kbd_register as of writing this code, so no further failable action can happen.

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}
