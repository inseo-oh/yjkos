#include "ioport.h"
#include "pic.h"
#include "pit.h"
#include <kernel/tasks/sched.h>
#include <kernel/ticktime.h>
#include <stddef.h>
#include <stdint.h>


#define PIT_CH0_DATA_PORT   0x40
#define PIT_MODE_PORT       0x43

#define PIT_FREQ    1193182

// Channel select (Bit 7:6)
#define PIT_MODEFLAG_SELECT_CH0     (0U << 6) 
// Access mode (Bit 5:4)
#define PIT_MODEFLAG_ACCESS_LSB_MSB (3U << 4)
// Operation mode (Bit 3:1)
#define PIT_MODEFLAG_OP_RATEGEN     (2U << 1)
// Binary/BCD mode (Bit 0)
#define PIT_MODEFLAG_BINMODE        (0U << 0)

#define PIT_IRQ     0
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
    archi586_in8(PIT_MODE_PORT);
}

static void irqhandler(int irqnum, void *data) {
    (void)data;
    g_ticktime++;
    archi586_pic_sendeoi(irqnum);
    sched_schedule();
}

static struct archi586_pic_irq_handler s_irqhandler;

void archi586_pit_init(void) {
    archi586_pic_maskirq(PIT_IRQ);
    uint32_t initial_counter = countefrommillis(FREQ_MILLIS);
    archi586_out8(
        PIT_MODE_PORT,
        PIT_MODEFLAG_SELECT_CH0 | PIT_MODEFLAG_ACCESS_LSB_MSB
        | PIT_MODEFLAG_OP_RATEGEN | PIT_MODEFLAG_BINMODE);
    archi586_out8(PIT_CH0_DATA_PORT, initial_counter);
    shortinternaldelay();
    archi586_out8(PIT_CH0_DATA_PORT, initial_counter >> 8);
    archi586_pic_registerhandler(
        &s_irqhandler, PIT_IRQ, irqhandler, NULL);
    archi586_pic_unmaskirq(PIT_IRQ);
}
