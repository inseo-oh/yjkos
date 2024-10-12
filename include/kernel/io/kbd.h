#pragma once 
#include <kernel/io/iodev.h>
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stdbool.h>
#include <stdint.h>

// Lock key flags
static uint16_t const KBD_FLAG_LOCK_CAPS   = 1 << 0;
static uint16_t const KBD_FLAG_LOCK_NUM    = 1 << 1;
static uint16_t const KBD_FLAG_LOCK_SCROLL = 1 << 2;

// Modifier key flags
static uint16_t const KBD_FLAG_MOD_LSHIFT = 1 << 8;
static uint16_t const KBD_FLAG_MOD_RSHIFT = 1 << 9;
static uint16_t const KBD_FLAG_MOD_LCTRL  = 1 << 10;
static uint16_t const KBD_FLAG_MOD_RCTRL  = 1 << 11;
static uint16_t const KBD_FLAG_MOD_LALT   = 1 << 12;
static uint16_t const KBD_FLAG_MOD_RALT   = 1 << 13;
static uint16_t const KBD_FLAG_MOD_LSUPER = 1 << 14;
static uint16_t const KBD_FLAG_MOD_RSUPER = 1 << 15;

#define KBD_FLAG_MOD_SHIFT (KBD_FLAG_MOD_LSHIFT | KBD_FLAG_MOD_RSHIFT)
#define KBD_FLAG_MOD_CTRL  (KBD_FLAG_MOD_LCTRL  | KBD_FLAG_MOD_RCTRL )
#define KBD_FLAG_MOD_ALT   (KBD_FLAG_MOD_LALT   | KBD_FLAG_MOD_RALT  )
#define KBD_FLAG_MOD_SUPER (KBD_FLAG_MOD_LSUPER | KBD_FLAG_MOD_RSUPER)

enum kbd_key {
    KBD_KEY_INVALID = 0,

    KBD_KEY_ESCAPE,
    KBD_KEY_F1,
    KBD_KEY_F2,
    KBD_KEY_F3,
    KBD_KEY_F4,
    KBD_KEY_F5,
    KBD_KEY_F6,
    KBD_KEY_F7,
    KBD_KEY_F8,
    KBD_KEY_F9,
    KBD_KEY_F10,
    KBD_KEY_F11,
    KBD_KEY_F12,
    KBD_KEY_PRINT_SCREEN,
    KBD_KEY_SCROLL_LOCK,
    KBD_KEY_PAUSE,

    KBD_KEY_BACK_TICK,
    KBD_KEY_1,
    KBD_KEY_2,
    KBD_KEY_3,
    KBD_KEY_4,
    KBD_KEY_5,
    KBD_KEY_6,
    KBD_KEY_7,
    KBD_KEY_8,
    KBD_KEY_9,
    KBD_KEY_0,
    KBD_KEY_MINUS,
    KBD_KEY_EQUALS,
    KBD_KEY_BACKSPACE,

    KBD_KEY_TAB,
    KBD_KEY_Q,
    KBD_KEY_W,
    KBD_KEY_E,
    KBD_KEY_R,
    KBD_KEY_T,
    KBD_KEY_Y,
    KBD_KEY_U,
    KBD_KEY_I,
    KBD_KEY_O,
    KBD_KEY_P,
    KBD_KEY_OPEN_BRACKET,
    KBD_KEY_CLOSE_BRACKET,
    KBD_KEY_BASKSLASH,

    KBD_KEY_CAPS_LOCK,
    KBD_KEY_A,
    KBD_KEY_S,
    KBD_KEY_D,
    KBD_KEY_F,
    KBD_KEY_G,
    KBD_KEY_H,
    KBD_KEY_J,
    KBD_KEY_K,
    KBD_KEY_L,
    KBD_KEY_SEMICOLON,
    KBD_KEY_QUOTE,
    KBD_KEY_ENTER,

    KBD_KEY_LSHIFT,
    KBD_KEY_Z,
    KBD_KEY_X,
    KBD_KEY_C,
    KBD_KEY_V,
    KBD_KEY_B,
    KBD_KEY_N,
    KBD_KEY_M,
    KBD_KEY_COMMA,
    KBD_KEY_DOT,
    KBD_KEY_SLASH,
    KBD_KEY_RSHIFT,

    KBD_KEY_LCTRL,
    KBD_KEY_LSUPER,
    KBD_KEY_LALT,
    KBD_KEY_SPACE,
    KBD_KEY_RALT,
    KBD_KEY_RSUPER,
    KBD_KEY_MENU,
    KBD_KEY_RCTRL,

    KBD_KEY_INSERT,
    KBD_KEY_DELETE,
    KBD_KEY_HOME,
    KBD_KEY_END,
    KBD_KEY_PAGE_UP,
    KBD_KEY_PAGE_DOWN,
    KBD_KEY_UP,
    KBD_KEY_DOWN,
    KBD_KEY_LEFT,
    KBD_KEY_RIGHT,

    KBD_KEY_NUM_LOCK,
    KBD_KEY_NUMPAD_DIV,
    KBD_KEY_NUMPAD_MUL,
    KBD_KEY_NUMPAD_SUB,
    KBD_KEY_NUMPAD_7,
    KBD_KEY_NUMPAD_8,
    KBD_KEY_NUMPAD_9,
    KBD_KEY_NUMPAD_4,
    KBD_KEY_NUMPAD_5,
    KBD_KEY_NUMPAD_6,
    KBD_KEY_NUMPAD_ADD,
    KBD_KEY_NUMPAD_1,
    KBD_KEY_NUMPAD_2,
    KBD_KEY_NUMPAD_3,
    KBD_KEY_NUMPAD_0,
    KBD_KEY_NUMPAD_POINT,
    KBD_KEY_NUMPAD_ENTER,

    KBD_KEY_COUNT
};

struct kbd_keyevent {
    enum kbd_key key;
    char chr;
    bool is_down : 1;
};

struct kbddev;
struct kbddev_ops {
    FAILABLE_FUNCTION (*updateleds)(struct kbddev *self, bool scroll, bool caps, bool num);
};
struct kbddev {
    struct list_node node;
    struct iodev iodev;
    void *data;
    struct kbddev_ops const *ops;
};

bool kbd_pullevent(struct kbd_keyevent *out);
void kbd_keypressed(enum kbd_key key);
void kbd_keyreleased(enum kbd_key key);
FAILABLE_FUNCTION kbd_register(struct kbddev *dev_out, struct kbddev_ops const *ops, void *data);

