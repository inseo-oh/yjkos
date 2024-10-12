#pragma once
#include <stdint.h>

struct trapframe {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    // This is only included as part of PUSHAD doing its thing, so it does not actually
    // represent the original ESP value before servicing the interrupt/exception.
    uint32_t esp_useless;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t errcode;
    uint32_t eip, cs, eflags;
    // Only valid if CS != 0.
    uint32_t esp_usermode, ss_usermode;
};

void archx86_exceptions_init(void);
