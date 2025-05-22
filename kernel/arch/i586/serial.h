#pragma once
#include "pic.h"
#include <kernel/io/tty.h>
#include <kernel/lib/queue.h>
#include <stdint.h>

/*#define OLD_IRQ_DRIVER*/
#define ARCHI586_SERIAL_QUEUE_SIZE  64

struct archi586_serial {
    struct archi586_pic_irq_handler irqhandler;
    struct tty tty;
    int32_t masterclock;
    uint16_t baseaddr;
    uint8_t irq;
#ifdef OLD_IRQ_DRIVER
    _Atomic(bool) txint;
    _Atomic(bool) rxint;
#else
    struct queue tx_queue, rx_queue;
    char tx_queue_buf[ARCHI586_SERIAL_QUEUE_SIZE];
    char rx_queue_buf[ARCHI586_SERIAL_QUEUE_SIZE];
#endif
    /* Config flags ***********************************************************/
    bool cr_to_crlf : 1;
    /* Internal flags *********************************************************/
    bool cr : 1;
    bool use_irq : 1;
};

[[nodiscard]] int archi586_serial_init(struct archi586_serial *out, uint16_t baseaddr, int32_t masterclock, uint8_t irq);
[[nodiscard]] int archi586_serial_config(struct archi586_serial *self, int32_t baudrate);
void archi586_serial_use_irq(struct archi586_serial *self);
[[nodiscard]] int archi586_serial_init_iodev(struct archi586_serial *self);
