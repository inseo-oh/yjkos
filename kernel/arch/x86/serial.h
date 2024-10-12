#pragma once
#include "ioport.h"
#include "pic.h"
#include <kernel/io/stream.h>
#include <kernel/status.h>
#include <stdbool.h>
#include <stdint.h>

struct archx86_serial {
    struct stream stream;
    uint32_t masterclock;
    archx86_ioaddr_t baseaddr;
    struct archx86_pic_irqhandler irqhandler;
    uint8_t irq;
    _Atomic(int) txint;
    _Atomic(int) rxint;
    // Config flags
    bool cr_to_crlf : 1;
    // Internal flags
    bool cr     : 1;
    bool useirq : 1;
};

FAILABLE_FUNCTION archx86_serial_init(struct archx86_serial *out, archx86_ioaddr_t baseaddr, uint32_t masterclock, uint8_t irq);
FAILABLE_FUNCTION archx86_serial_config(struct archx86_serial *self, uint32_t baudrate);
void archx86_serial_useirq(struct archx86_serial *self);
