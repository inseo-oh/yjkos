#pragma once

#include <stdbool.h>
#include <stdint.h>

uint32_t archx86_geteflags(void);
void archx86_cli(void);
void archx86_sti(void);
void archx86_hlt(void);
void archx86_rdtsc(uint32_t *upper, uint32_t *lower);
void archx86_invlpg(void *ptr);
void archx86_reloadcr3(void);
uint32_t archx86_readcr0(void);
uint32_t archx86_readcr2(void);
uint32_t archx86_readcr3(void);
uint32_t archx86_readcr4(void);
uint32_t archx86_readcr8(void);


static uint32_t const EFLAGS_FLAG_IF = 1 << 9;
