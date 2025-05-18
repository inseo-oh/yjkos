#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/trapmanager.h>
#include <stdint.h>
#include <string.h>

/* Each trap entry is a list of trap handlers. */
static struct List s_traps[YJKERNEL_ARCH_TRAP_COUNT];

static uint32_t calculate_checksum(struct TrapHandler const *handler) {
    struct TrapHandler temp;
    memcpy(&temp, handler, sizeof(temp));
    temp.checksum = 0;
    STATIC_ASSERT_TEST(sizeof(*handler) % sizeof(uint32_t) == 0);
    uint32_t *val = (void *)&temp;
    uint32_t sum = 0;
    for (size_t i = 0; i < (sizeof(*handler) / sizeof(uint32_t)); i++) {
        sum += val[i];
    }
    return ((uint32_t)~0) - sum;
}

void TrapManager_Register(struct TrapHandler *out, int trapnum, void (*callback)(int trapnum, void *trapframe, void *data), void *data) {
    bool prev_interrupts = Arch_Irq_Disable();
    out->callback = callback;
    out->data = data;
    List_InsertBack(&s_traps[trapnum], &out->node, out);
    out->checksum = calculate_checksum(out);
    if (out->node.prev != NULL) {
        struct TrapHandler *handler = out->node.prev->data;
        handler->checksum = calculate_checksum(handler);
    }
    if (out->node.next != NULL) {
        struct TrapHandler *handler = out->node.next->data;
        handler->checksum = calculate_checksum(handler);
    }
    Arch_Irq_Restore(prev_interrupts);
}

void TrapManager_Trap(int trapnum, void *trapframe) {
    ASSERT_IRQ_DISABLED();
    enum {
        HANDLERS_COUNT = sizeof(s_traps) / sizeof(*s_traps),
    };
    if (HANDLERS_COUNT <= trapnum) {
        Co_Printf("trap %d is outside of valid trap range(0~%d)\n", trapnum, HANDLERS_COUNT - 1);
        return;
    }
    if (!s_traps[trapnum].front) {
        Co_Printf("no trap handler registered for trap %d\n", trapnum);
        return;
    }
    LIST_FOREACH(&s_traps[trapnum], handlernode) {
        struct TrapHandler *handler = handlernode->data;
        uint32_t expected_checksum = calculate_checksum(handler);
        uint32_t got_checksum = handler->checksum;
        if (expected_checksum != got_checksum) {
            Co_Printf("bad trap handler checksum in trap %d: expected %#x, got %#x\n", trapnum, expected_checksum, got_checksum);
        } else {
            handler->callback(trapnum, trapframe, handler->data);
        }
    }
}
