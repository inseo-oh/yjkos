#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/io/iodev.h>
#include <kernel/io/kbd.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/queue.h>
#include <kernel/lib/strutil.h>
#include <stdarg.h>
#include <stdint.h>

/******************************** Configuration *******************************/

/*
 * Print key info when pressing or releasing keys?
 */
static bool const CONFIG_PRINT_KEYS = false;

/******************************************************************************/

/* NOTE: Use event_queue() instead, which initializes the queue if it wasn't. */
static struct queue s_event_queue;
/* 500 should be more than enough */
static struct kbd_key_event s_event_queue_buf[500];
static struct list s_keyboard_list;
static uint16_t s_flags;
/* TODO: Use bitmap instead? */
static bool s_keysdown[KBD_KEY_COUNT];

#define KEYMAP_FLAG_SHIFT (1U << 0)
#define KEYMAP_FLAG_CAPSLOCK (1U << 1)
#define KEYMAP_FLAG_NUMLOCK (1U << 2)

struct keymapentry {
    KBD_KEY key_alt;
    char chr, chralt;
    uint8_t flags;
};

/* KEY_CHAR_WITH_CAPSLOCK implies KEY_CHAR_WITH_SHIFT as well. */
#define KEY_NOCHAR() \
    { .key_alt = 0, .chr = 0, .chralt = 0, .flags = 0 }
#define KEY_CHAR(_chr) \
    { .key_alt = 0, .chr = (_chr), .chralt = (_chr), .flags = 0 }
#define KEY_CHAR_WITH_SHIFT(_chr, _chralt)                                           \
    {                                                                                \
        .key_alt = 0, .chr = (_chr), .chralt = (_chralt), .flags = KEYMAP_FLAG_SHIFT \
    }
#define KEY_CHAR_WITH_CAPSLOCK(_chr, _chralt) \
    { .key_alt = 0, .chr = (_chr), .chralt = (_chralt), .flags = KEYMAP_FLAG_SHIFT | KEYMAP_FLAG_CAPSLOCK }
#define KEY_CHAR_WITH_NUMLOCK(_numlock_off_k, _chr) \
    { .key_alt = (_numlock_off_k), .chr = (_chr), .chralt = (_chr), .flags = KEYMAP_FLAG_NUMLOCK }

static struct keymapentry const KEYMAP[] = {
    [KBD_KEY_ESCAPE] = KEY_NOCHAR(),
    [KBD_KEY_F1] = KEY_NOCHAR(),
    [KBD_KEY_F2] = KEY_NOCHAR(),
    [KBD_KEY_F3] = KEY_NOCHAR(),
    [KBD_KEY_F4] = KEY_NOCHAR(),
    [KBD_KEY_F5] = KEY_NOCHAR(),
    [KBD_KEY_F6] = KEY_NOCHAR(),
    [KBD_KEY_F7] = KEY_NOCHAR(),
    [KBD_KEY_F8] = KEY_NOCHAR(),
    [KBD_KEY_F9] = KEY_NOCHAR(),
    [KBD_KEY_F10] = KEY_NOCHAR(),
    [KBD_KEY_F11] = KEY_NOCHAR(),
    [KBD_KEY_F12] = KEY_NOCHAR(),

    [KBD_KEY_PRINT_SCREEN] = KEY_NOCHAR(),
    [KBD_KEY_SCROLL_LOCK] = KEY_NOCHAR(),
    [KBD_KEY_PAUSE] = KEY_NOCHAR(),

    [KBD_KEY_BACK_TICK] = KEY_CHAR_WITH_SHIFT('`', '~'),
    [KBD_KEY_1] = KEY_CHAR_WITH_SHIFT('1', '!'),
    [KBD_KEY_2] = KEY_CHAR_WITH_SHIFT('2', '@'),
    [KBD_KEY_3] = KEY_CHAR_WITH_SHIFT('3', '#'),
    [KBD_KEY_4] = KEY_CHAR_WITH_SHIFT('4', '$'),
    [KBD_KEY_5] = KEY_CHAR_WITH_SHIFT('5', '%'),
    [KBD_KEY_6] = KEY_CHAR_WITH_SHIFT('6', '^'),
    [KBD_KEY_7] = KEY_CHAR_WITH_SHIFT('7', '&'),
    [KBD_KEY_8] = KEY_CHAR_WITH_SHIFT('8', '*'),
    [KBD_KEY_9] = KEY_CHAR_WITH_SHIFT('9', '('),
    [KBD_KEY_0] = KEY_CHAR_WITH_SHIFT('0', ')'),
    [KBD_KEY_MINUS] = KEY_CHAR_WITH_SHIFT('-', '_'),
    [KBD_KEY_EQUALS] = KEY_CHAR_WITH_SHIFT('=', '+'),
    [KBD_KEY_BACKSPACE] = KEY_CHAR('\b'),

    [KBD_KEY_TAB] = KEY_CHAR('\t'),
    [KBD_KEY_Q] = KEY_CHAR_WITH_CAPSLOCK('q', 'Q'),
    [KBD_KEY_W] = KEY_CHAR_WITH_CAPSLOCK('w', 'W'),
    [KBD_KEY_E] = KEY_CHAR_WITH_CAPSLOCK('e', 'E'),
    [KBD_KEY_R] = KEY_CHAR_WITH_CAPSLOCK('r', 'R'),
    [KBD_KEY_T] = KEY_CHAR_WITH_CAPSLOCK('t', 'T'),
    [KBD_KEY_Y] = KEY_CHAR_WITH_CAPSLOCK('y', 'Y'),
    [KBD_KEY_U] = KEY_CHAR_WITH_CAPSLOCK('u', 'U'),
    [KBD_KEY_I] = KEY_CHAR_WITH_CAPSLOCK('i', 'I'),
    [KBD_KEY_O] = KEY_CHAR_WITH_CAPSLOCK('o', 'O'),
    [KBD_KEY_P] = KEY_CHAR_WITH_CAPSLOCK('p', 'P'),
    [KBD_KEY_OPEN_BRACKET] = KEY_CHAR_WITH_SHIFT('[', '{'),
    [KBD_KEY_CLOSE_BRACKET] = KEY_CHAR_WITH_SHIFT(']', '}'),
    [KBD_KEY_BASKSLASH] = KEY_CHAR_WITH_SHIFT('\\', '|'),

    [KBD_KEY_CAPS_LOCK] = KEY_NOCHAR(),
    [KBD_KEY_A] = KEY_CHAR_WITH_CAPSLOCK('a', 'A'),
    [KBD_KEY_S] = KEY_CHAR_WITH_CAPSLOCK('s', 'S'),
    [KBD_KEY_D] = KEY_CHAR_WITH_CAPSLOCK('d', 'D'),
    [KBD_KEY_F] = KEY_CHAR_WITH_CAPSLOCK('f', 'F'),
    [KBD_KEY_G] = KEY_CHAR_WITH_CAPSLOCK('g', 'G'),
    [KBD_KEY_H] = KEY_CHAR_WITH_CAPSLOCK('h', 'H'),
    [KBD_KEY_J] = KEY_CHAR_WITH_CAPSLOCK('j', 'J'),
    [KBD_KEY_K] = KEY_CHAR_WITH_CAPSLOCK('k', 'K'),
    [KBD_KEY_L] = KEY_CHAR_WITH_CAPSLOCK('l', 'L'),
    [KBD_KEY_SEMICOLON] = KEY_CHAR_WITH_SHIFT(';', ':'),
    [KBD_KEY_QUOTE] = KEY_CHAR_WITH_SHIFT('\'', '"'),
    [KBD_KEY_ENTER] = KEY_CHAR('\n'),

    [KBD_KEY_LSHIFT] = KEY_NOCHAR(),
    [KBD_KEY_Z] = KEY_CHAR_WITH_CAPSLOCK('z', 'Z'),
    [KBD_KEY_X] = KEY_CHAR_WITH_CAPSLOCK('x', 'X'),
    [KBD_KEY_C] = KEY_CHAR_WITH_CAPSLOCK('c', 'C'),
    [KBD_KEY_V] = KEY_CHAR_WITH_CAPSLOCK('v', 'V'),
    [KBD_KEY_B] = KEY_CHAR_WITH_CAPSLOCK('b', 'B'),
    [KBD_KEY_N] = KEY_CHAR_WITH_CAPSLOCK('n', 'N'),
    [KBD_KEY_M] = KEY_CHAR_WITH_CAPSLOCK('m', 'M'),
    [KBD_KEY_COMMA] = KEY_CHAR_WITH_SHIFT(',', '<'),
    [KBD_KEY_DOT] = KEY_CHAR_WITH_SHIFT('.', '>'),
    [KBD_KEY_SLASH] = KEY_CHAR_WITH_SHIFT('/', '?'),
    [KBD_KEY_RSHIFT] = KEY_NOCHAR(),

    [KBD_KEY_LCTRL] = KEY_NOCHAR(),
    [KBD_KEY_LSUPER] = KEY_NOCHAR(),
    [KBD_KEY_LALT] = KEY_NOCHAR(),
    [KBD_KEY_SPACE] = KEY_CHAR(' '),
    [KBD_KEY_RALT] = KEY_NOCHAR(),
    [KBD_KEY_RSUPER] = KEY_NOCHAR(),
    [KBD_KEY_MENU] = KEY_NOCHAR(),
    [KBD_KEY_RCTRL] = KEY_NOCHAR(),

    [KBD_KEY_INSERT] = KEY_NOCHAR(),
    [KBD_KEY_DELETE] = KEY_NOCHAR(),
    [KBD_KEY_HOME] = KEY_NOCHAR(),
    [KBD_KEY_END] = KEY_NOCHAR(),
    [KBD_KEY_PAGE_UP] = KEY_NOCHAR(),
    [KBD_KEY_PAGE_DOWN] = KEY_NOCHAR(),
    [KBD_KEY_UP] = KEY_NOCHAR(),
    [KBD_KEY_DOWN] = KEY_NOCHAR(),
    [KBD_KEY_LEFT] = KEY_NOCHAR(),
    [KBD_KEY_RIGHT] = KEY_NOCHAR(),

    [KBD_KEY_NUM_LOCK] = KEY_NOCHAR(),
    [KBD_KEY_NUMPAD_MUL] = KEY_CHAR('*'),
    [KBD_KEY_NUMPAD_DIV] = KEY_CHAR('/'),
    [KBD_KEY_NUMPAD_SUB] = KEY_CHAR('-'),
    [KBD_KEY_NUMPAD_7] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_HOME, '7'),
    [KBD_KEY_NUMPAD_8] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_UP, '8'),
    [KBD_KEY_NUMPAD_9] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_PAGE_UP, '9'),
    [KBD_KEY_NUMPAD_4] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_LEFT, '4'),
    [KBD_KEY_NUMPAD_5] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_INVALID, '5'),
    [KBD_KEY_NUMPAD_6] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_RIGHT, '6'),
    [KBD_KEY_NUMPAD_ADD] = KEY_CHAR('+'),
    [KBD_KEY_NUMPAD_1] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_END, '1'),
    [KBD_KEY_NUMPAD_2] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_DOWN, '2'),
    [KBD_KEY_NUMPAD_3] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_PAGE_DOWN, '3'),
    [KBD_KEY_NUMPAD_0] = KEY_CHAR_WITH_NUMLOCK(KBD_KEY_INSERT, '0'),
    [KBD_KEY_NUMPAD_POINT] = KEY_CHAR('.'),
    [KBD_KEY_NUMPAD_ENTER] = KEY_CHAR('\n'),
};

#undef KEY_NOCHAR
#undef KEY_CHAR
#undef KEY_CHAR_WITH_SHIFT
#undef KEY_WITH_NUMLOCK
#undef KEY_CHAR_WITH_NUMLOCK

static void update_leds(void) {
    ASSERT_IRQ_DISABLED();
    bool scroll = s_flags & KBD_FLAG_LOCK_SCROLL;
    bool caps = s_flags & KBD_FLAG_LOCK_CAPS;
    bool num = s_flags & KBD_FLAG_LOCK_NUM;
    LIST_FOREACH(&s_keyboard_list, devnode) {
        struct kbd_dev *device = devnode->data;
        assert(device);
        int ret = device->ops->updateleds(device, scroll, caps, num);
        if (ret < 0) {
            iodev_printf(&device->iodev, "failed to set LED state (error %d)\n", ret);
        }
    }
}

static void release_all_keys_except(KBD_KEY except) {
    ASSERT_IRQ_DISABLED();
    for (size_t key = 1; key < KBD_KEY_COUNT; key++) {
        if (key == except) {
            continue;
        }
        if (s_keysdown[key]) {
            kbd_key_released(key);
        }
    }
}

static struct queue *event_queue(void) {
    if (s_event_queue.buf == NULL) {
        QUEUE_INIT_FOR_ARRAY(&s_event_queue, s_event_queue_buf);
    }
    return &s_event_queue;
}

static void enqueue_event(struct kbd_key_event const *event) {
    bool prev_interrupts = arch_irq_disable();
    int ret = QUEUE_ENQUEUE(event_queue(), event);
    arch_irq_restore(prev_interrupts);
    if (ret < 0) {
        co_printf("kbd: failed to enqueue key event (error %d)\n", ret);
    }
}

bool kbd_pull_event(struct kbd_key_event *out) {
    bool prev_interrupts = arch_irq_disable();
    bool result = QUEUE_DEQUEUE(out, event_queue());
    arch_irq_restore(prev_interrupts);
    return result;
}

void kbd_key_pressed(KBD_KEY key) {
    struct keymapentry const *entry = &KEYMAP[key];
    switch (key) {
    /* Modifier key except for lock keys, like Shift and Alt. */
    case KBD_KEY_LSHIFT:
        s_flags |= KBD_FLAG_MOD_LSHIFT;
        break;
    case KBD_KEY_RSHIFT:
        s_flags |= KBD_FLAG_MOD_RSHIFT;
        break;
    case KBD_KEY_LCTRL:
        s_flags |= KBD_FLAG_MOD_LCTRL;
        break;
    case KBD_KEY_RCTRL:
        s_flags |= KBD_FLAG_MOD_RCTRL;
        break;
    case KBD_KEY_LALT:
        s_flags |= KBD_FLAG_MOD_LALT;
        break;
    case KBD_KEY_RALT:
        s_flags |= KBD_FLAG_MOD_RALT;
        break;
    case KBD_KEY_LSUPER:
        s_flags |= KBD_FLAG_MOD_LSUPER;
        break;
    case KBD_KEY_RSUPER:
        s_flags |= KBD_FLAG_MOD_RSUPER;
        break;
    /* Lock keys(Capslock, Numlock...) */
    case KBD_KEY_CAPS_LOCK:
        s_flags ^= KBD_FLAG_LOCK_CAPS;
        update_leds();
        break;
    case KBD_KEY_NUM_LOCK:
        s_flags ^= KBD_FLAG_LOCK_NUM;
        update_leds();
        release_all_keys_except(KBD_KEY_NUM_LOCK);
        break;
    case KBD_KEY_SCROLL_LOCK:
        s_flags ^= KBD_FLAG_LOCK_SCROLL;
        update_leds();
        break;
    default:
        break;
    }
    KBD_KEY key_to_report = key;
    char chr = entry->chr;
    if ((entry->flags & KEYMAP_FLAG_NUMLOCK) && !(s_flags & KBD_FLAG_LOCK_NUM)) {
        key_to_report = entry->key_alt;
        chr = 0;
    } else if (
        ((entry->flags & KEYMAP_FLAG_CAPSLOCK) && (s_flags & KBD_FLAG_LOCK_CAPS)) ||
        ((entry->flags & KEYMAP_FLAG_SHIFT) && (s_flags & KBD_FLAG_MOD_SHIFT))) {
        chr = entry->chralt;
    }
    s_keysdown[key] = true;
    if (key_to_report != KBD_KEY_INVALID) {
        if (CONFIG_PRINT_KEYS) {
            co_printf("[KEY_DOWN] PKEY=%03d RKEY=%03d CHAR=[%c]\n", key, key_to_report, chr);
        }
        struct kbd_key_event event;
        event.chr = chr;
        event.is_down = true;
        event.key = key_to_report;
        enqueue_event(&event);
    }
}

void kbd_key_released(KBD_KEY key) {
    struct keymapentry const *entry = &KEYMAP[key];
    switch (key) {
    /* Modifier key except for lock keys, like Shift and Alt. */
    case KBD_KEY_LSHIFT:
        s_flags &= ~KBD_FLAG_MOD_LSHIFT;
        break;
    case KBD_KEY_RSHIFT:
        s_flags &= ~KBD_FLAG_MOD_RSHIFT;
        break;
    case KBD_KEY_LCTRL:
        s_flags &= ~KBD_FLAG_MOD_LCTRL;
        break;
    case KBD_KEY_RCTRL:
        s_flags &= ~KBD_FLAG_MOD_RCTRL;
        break;
    case KBD_KEY_LALT:
        s_flags &= ~KBD_FLAG_MOD_LALT;
        break;
    case KBD_KEY_RALT:
        s_flags &= ~KBD_FLAG_MOD_RALT;
        break;
    case KBD_KEY_LSUPER:
        s_flags &= ~KBD_FLAG_MOD_LSUPER;
        break;
    case KBD_KEY_RSUPER:
        s_flags &= ~KBD_FLAG_MOD_RSUPER;
        break;
    default:
        break;
    }
    KBD_KEY key_to_report = key;
    char chr = entry->chr;
    if ((entry->flags & KEYMAP_FLAG_NUMLOCK) && !(s_flags & KBD_FLAG_LOCK_NUM)) {
        key_to_report = entry->key_alt;
        chr = 0;
    } else if (
        ((entry->flags & KEYMAP_FLAG_CAPSLOCK) && (s_flags & KBD_FLAG_LOCK_CAPS)) ||
        ((entry->flags & KEYMAP_FLAG_SHIFT) && (s_flags & KBD_FLAG_MOD_SHIFT))) {
        chr = entry->chralt;
    }
    if (!s_keysdown[key]) {
        return;
    }
    s_keysdown[key] = false;
    if (key_to_report != KBD_KEY_INVALID) {
        if (CONFIG_PRINT_KEYS) {
            co_printf("[ KEY_UP ] PKEY=%03d RKEY=%03d CHAR=[%c]\n", key, key_to_report, chr);
        }
        struct kbd_key_event event;
        event.chr = chr;
        event.is_down = false;
        event.key = key_to_report;
        enqueue_event(&event);
    }
}

[[nodiscard]] int kbd_register(struct kbd_dev *dev_out, struct kbd_dev_ops const *ops, void *data) {
    int result = 0;
    bool prev_interrupts = arch_irq_disable();
    memset(dev_out, 0, sizeof(*dev_out));
    dev_out->data = data;
    dev_out->ops = ops;
    result = iodev_register(&dev_out->iodev, IODEV_TYPE_KEYBOARD, dev_out);
    if (result < 0) {
        goto out;
    }
out:
    arch_irq_restore(prev_interrupts);
    return result;
}
