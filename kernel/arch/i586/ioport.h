#pragma once
#include <stddef.h>
#include <stdint.h>

void archi586_out8(uint16_t port, uint8_t val);
void archi586_out16(uint16_t port, uint16_t val);
void archi586_out32(uint16_t port, uint32_t val);
uint8_t archi586_in8(uint16_t port);
uint16_t archi586_in16(uint16_t port);
uint32_t archi586_in32(uint16_t port);
void archi586_in16_rep(uint16_t port, void *buf, size_t len);
