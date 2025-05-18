#pragma once
#include "pic.h"
#include <kernel/io/tty.h>
#include <stdint.h>

struct ArchI586_Serial {
    struct ArchI586_Pic_IrqHandler irqhandler;
    struct Tty tty;
    int32_t masterclock;
    uint16_t baseaddr;
    uint8_t irq;
    _Atomic(bool) txint;
    _Atomic(bool) rxint;
    /* Config flags ***********************************************************/
    bool cr_to_crlf : 1;
    /* Internal flags *********************************************************/
    bool cr : 1;
    bool useirq : 1;
};

[[nodiscard]] int ArchI586_Serial_Init(struct ArchI586_Serial *out, uint16_t baseaddr, int32_t masterclock, uint8_t irq);
[[nodiscard]] int ArchI586_Serial_Config(struct ArchI586_Serial *self, int32_t baudrate);
void ArchI586_Serial_UseIrq(struct ArchI586_Serial *self);
[[nodiscard]] int ArchI586_Serial_InitIoDev(struct ArchI586_Serial *self);
