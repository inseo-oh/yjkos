#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>
#include <kernel/trapmanager.h>
#include <stddef.h>
#include <stdint.h>

/* Each trap entry is a list of trap handlers. */
static struct list s_traps[YJKERNEL_ARCH_TRAP_COUNT];

static uint32_t calculate_checksum(struct trap_handler const *handler) {
    struct trap_handler temp;
    vmemcpy(&temp, handler, sizeof(temp));
    temp.checksum = 0;
    STATIC_ASSERT_TEST(sizeof(*handler) % sizeof(uint32_t) == 0);
    uint32_t *val = (void *)&temp;
    uint32_t sum = 0;
    for (size_t i = 0; i < (sizeof(*handler) / sizeof(uint32_t)); i++) {
        sum += val[i];
    }
    return ((uint32_t)~0) - sum;
}

void trapmanager_register_trap(struct trap_handler *out, int trapnum, void (*callback)(int trapnum, void *trapframe, void *data), void *data) {
    bool prev_interrupts = arch_irq_disable();
    out->callback = callback;
    out->data = data;
    list_insert_back(&s_traps[trapnum], &out->node, out);
    out->checksum = calculate_checksum(out);
    if (out->node.prev != nullptr) {
        struct trap_handler *handler = out->node.prev->data;
        handler->checksum = calculate_checksum(handler);
    }
    if (out->node.next != nullptr) {
        struct trap_handler *handler = out->node.next->data;
        handler->checksum = calculate_checksum(handler);
    }
    arch_irq_restore(prev_interrupts);
}

void trapmanager_trap(int trapnum, void *trapframe) {
    ASSERT_IRQ_DISABLED();
    enum {
        HANDLERS_COUNT = sizeof(s_traps) / sizeof(*s_traps),
    };
    if (HANDLERS_COUNT <= trapnum) {
        co_printf("trap %d is outside of valid trap range(0~%d)\n", trapnum, HANDLERS_COUNT - 1);
        return;
    }
    if (!s_traps[trapnum].front) {
        co_printf("no trap handler registered for trap %d\n", trapnum);
        return;
    }
    LIST_FOREACH(&s_traps[trapnum], handlernode) {
        struct trap_handler *handler = handlernode->data;
        uint32_t expected_checksum = calculate_checksum(handler);
        uint32_t got_checksum = handler->checksum;
        if (expected_checksum != got_checksum) {
            co_printf("bad trap handler checksum in trap %d: expected %#x, got %#x\n", trapnum, expected_checksum, got_checksum);
        } else {
            handler->callback(trapnum, trapframe, handler->data);
        }
    }
}
