#pragma once
#include <stddef.h>
#include <stdint.h>

extern uint8_t _binary_kernelfont_psf_start[];
extern uint8_t _binary_kernelfont_psf_end[];

void psf_init(void);
int psf_getwidth(void);
int psf_getheight(void);
size_t psf_getbytesperline(void);
uint8_t *psf_getglyph(uint32_t chr);
