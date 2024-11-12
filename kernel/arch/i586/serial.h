#pragma once
#include "pic.h"
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>
#include <stdint.h>

struct archi586_serial {
    struct archi586_pic_irq_handler irqhandler;
    struct tty tty;
    int32_t masterclock;
    uint16_t baseaddr;
    uint8_t irq;
    _Atomic(bool) txint;
    _Atomic(bool) rxint;
    // Config flags
    bool cr_to_crlf : 1;
    // Internal flags
    bool cr     : 1;
    bool useirq : 1;
};

WARN_UNUSED_RESULT int archi586_serial_init(
    struct archi586_serial *out, uint16_t baseaddr, int32_t masterclock,
    uint8_t irq);
WARN_UNUSED_RESULT int archi586_serial_config(
    struct archi586_serial *self, int32_t baudrate);
void archi586_serial_useirq(struct archi586_serial *self);
WARN_UNUSED_RESULT int archi586_serial_initiodev(struct archi586_serial *self);
