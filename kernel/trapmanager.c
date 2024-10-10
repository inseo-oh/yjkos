#include <kernel/arch/interrupts.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/panic.h>
#include <kernel/trapmanager.h>
#include <stdint.h>
#include <string.h>

// Each trap entry is a list of trap handlers.
static list_t s_traps[YJKERNEL_ARCH_TRAP_COUNT];

static uint32_t calculatechecksum(traphandler_t const *handler) {
    traphandler_t temp;
    memcpy(&temp, handler, sizeof(temp));
    temp.checksum = 0;
    STATIC_ASSERT_TEST(sizeof(*handler) % sizeof(uint32_t) == 0);
    uint32_t *v = (void *)&temp;
    uint32_t sum = 0;
    for (size_t i = 0; i < (sizeof(*handler) / sizeof(uint32_t)); i++) {
        sum += v[i];
    }
    return ((uint32_t)~0) - sum;
}

void trapmanager_register(traphandler_t *out, int trapnum, void (*callback)(int trapnum, void *trapframe, void *data), void *data) {
    bool previnterrupts = arch_interrupts_disable();
    out->callback = callback;
    out->data = data;
    list_insertback(&s_traps[trapnum], &out->node, out);
    out->checksum = calculatechecksum(out);
    if (out->node.prev != NULL) {
        traphandler_t *handler = out->node.prev->data;
        handler->checksum = calculatechecksum(handler);
    }
    if (out->node.next != NULL) {
        traphandler_t *handler = out->node.next->data;
        handler->checksum = calculatechecksum(handler);
    }
    interrupts_restore(previnterrupts);
}

void trapmanager_trap(int trapnum, void *trapframe) {
    ASSERT_INTERRUPTS_DISABLED();
    enum {
        HANDLERS_COUNT = sizeof(s_traps)/sizeof(*s_traps),
    };
    if (HANDLERS_COUNT <= trapnum) {
        tty_printf("trap %d is outside of valid trap range(0~%d)\n", trapnum, HANDLERS_COUNT - 1);
        return;
    }
    if (!s_traps[trapnum].front) {
        tty_printf("no trap handler registered for trap %d\n", trapnum);
        return;
    }
    for (list_node_t *handlernode = s_traps[trapnum].front; handlernode != NULL; handlernode = handlernode->next) {
        traphandler_t *handler = handlernode->data;
        uint32_t expectedchecksum = calculatechecksum(handler);
        uint32_t gotchecksum = handler->checksum;
        if (expectedchecksum != gotchecksum) {
            tty_printf("bad trap handler checksum in trap %d: expected %#x, got %#x\n", trapnum, expectedchecksum, gotchecksum);
        } else {
            handler->callback(trapnum, trapframe, handler->data);
        }
    }

}
