#include "ioport.h"
#include "pic.h"
#include "pit.h"
#include <kernel/io/tty.h>
#include <kernel/tasks/sched.h>
#include <kernel/ticktime.h>
#include <kernel/trapmanager.h>
#include <stddef.h>
#include <stdint.h>


enum {
    PIT_CH0_DATA_PORT = 0x40,
    PIT_MODE_PORT     = 0x43,

    PIT_FREQ = 1193182,
};

// Channel select (Bit 7:6)
static uint8_t const PIT_MODEFLAG_SELECT_CH0 = 0 << 6; 
// Access mode (Bit 5:4)
static uint8_t const PIT_MODEFLAG_ACCESS_LSB_MSB = 3 << 4;
// Operation mode (Bit 3:1)
static uint8_t const PIT_MODEFLAG_OP_RATEGEN = 2 << 1;
// Binary/BCD mode (Bit 0)
static uint8_t const PIT_MODEFLAG_BINMODE = 0 << 0;

static uint8_t const PIT_IRQ = 0;
static int const FREQ_MILLIS = 1;

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
    archx86_in8(PIT_MODE_PORT);
}

static void irqhandler(int irqnum, void *data) {
    (void)data;
    g_ticktime++;
    archx86_pic_sendeoi(irqnum);
    sched_schedule();
}

static archx86_pic_irqhandler_t s_irqhandler;

void archx86_pit_init(void) {
    archx86_pic_maskirq(PIT_IRQ);
    uint32_t initial_counter = countefrommillis(FREQ_MILLIS);
    archx86_out8(PIT_MODE_PORT, PIT_MODEFLAG_SELECT_CH0 | PIT_MODEFLAG_ACCESS_LSB_MSB | PIT_MODEFLAG_OP_RATEGEN | PIT_MODEFLAG_BINMODE);
    archx86_out8(PIT_CH0_DATA_PORT, initial_counter);
    shortinternaldelay();
    archx86_out8(PIT_CH0_DATA_PORT, initial_counter >> 8);
    archx86_pic_registerhandler(&s_irqhandler, PIT_IRQ, irqhandler, NULL);
    archx86_pic_unmaskirq(PIT_IRQ);
}
