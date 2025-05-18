#include "pit.h"
#include "ioport.h"
#include "pic.h"
#include <kernel/tasks/sched.h>
#include <kernel/ticktime.h>
#include <stddef.h>
#include <stdint.h>

#define PIT_CH0_DATA_PORT 0x40
#define PIT_MODE_PORT 0x43

#define PIT_FREQ 1193182

#define PIT_MODEFLAG_SELECT_CH0 (0U << 6)     /* Channel select (Bit 7:6) */
#define PIT_MODEFLAG_ACCESS_LSB_MSB (3U << 4) /* Access mode (Bit 5:4) */
#define PIT_MODEFLAG_OP_RATEGEN (2U << 1)     /* Operation mode (Bit 3:1) */
#define PIT_MODEFLAG_BINMODE (0U << 0)        /* Binary/BCD mode (Bit 0) */

#define PIT_IRQ 0
#define FREQ_MILLIS 1

static uint32_t countervaluefromhz(uint32_t hz) {
    return PIT_FREQ / hz;
}

static uint32_t hzfrommillis(uint32_t millis) {
    return 1000 / millis;
}

static uint32_t countefrommillis(uint32_t millis) {
    return countervaluefromhz(hzfrommillis(millis));
}

static void shortinternaldelay(void) {
    ArchI586_In8(PIT_MODE_PORT);
}

static void irqhandler(int irqnum, void *data) {
    (void)data;
    g_ticktime++;
    ArchI586_Pic_SendEoi(irqnum);
    Sched_Schedule();
}

static struct ArchI586_Pic_IrqHandler s_irqhandler;

void ArchI586_Pit_Init(void) {
    ArchI586_Pic_MaskIrq(PIT_IRQ);
    uint32_t initial_counter = countefrommillis(FREQ_MILLIS);
    ArchI586_Out8(PIT_MODE_PORT, PIT_MODEFLAG_SELECT_CH0 | PIT_MODEFLAG_ACCESS_LSB_MSB | PIT_MODEFLAG_OP_RATEGEN | PIT_MODEFLAG_BINMODE);
    ArchI586_Out8(PIT_CH0_DATA_PORT, initial_counter);
    shortinternaldelay();
    ArchI586_Out8(PIT_CH0_DATA_PORT, initial_counter >> 8);
    ArchI586_Pic_RegisterHandler(&s_irqhandler, PIT_IRQ, irqhandler, NULL);
    ArchI586_Pic_UnmaskIrq(PIT_IRQ);
}
