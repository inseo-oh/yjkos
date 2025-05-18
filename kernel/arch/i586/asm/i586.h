#pragma once

#include <stdint.h>

uint32_t ArchI586_GetEFlags(void);
void ArchI586_Cli(void);
void ArchI586_Sti(void);
void ArchI586_Hlt(void);
void ArchI586_Rdtsc(uint32_t *upper, uint32_t *lower);
void ArchI586_Invlpg(void *ptr);
void ArchI586_ReloadCr3(void);
uint32_t ArchI586_ReadCr0(void);
void *ArchI586_ReadCr2(void);
uint32_t ArchI586_ReadCr3(void);
uint32_t ArchI586_ReadCr4(void);
uint32_t ArchI586_ReadCr8(void);

static uint32_t const EFLAGS_FLAG_IF = 1 << 9;
