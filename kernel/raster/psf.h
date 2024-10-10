#pragma once
#include <stdint.h>
#include <stddef.h>

extern uint8_t _binary_kernelfont_psf_start[];
extern uint8_t _binary_kernelfont_psf_end[];

void psf_init(void);
size_t psf_getwidth(void);
size_t psf_getheight(void);
size_t psf_getbytesperline(void);
uint8_t *psf_getglyph(uint32_t chr);
