#pragma once

#include <stdbool.h>
#include <stdint.h>

uint32_t archi586_geteflags(void);
void archi586_cli(void);
void archi586_sti(void);
void archi586_hlt(void);
void archi586_rdtsc(uint32_t *upper, uint32_t *lower);
void archi586_invlpg(void *ptr);
void archi586_reload_cr3(void);
uint32_t archi586_read_cr0(void);
void *archi586_read_cr2(void);
uint32_t archi586_read_cr3(void);
uint32_t archi586_read_cr4(void);
uint32_t archi586_read_cr8(void);

static uint32_t const EFLAGS_FLAG_IF = 1 << 9;
