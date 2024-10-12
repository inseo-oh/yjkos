#pragma once
#include <stddef.h>
#include <stdint.h>

void archx86_out8(uint16_t port, uint8_t val);
void archx86_out16(uint16_t port, uint16_t val);
void archx86_out32(uint16_t port, uint32_t val);
uint8_t archx86_in8(uint16_t port);
uint16_t archx86_in16(uint16_t port);
uint32_t archx86_in32(uint16_t port);
void archx86_in16rep(uint16_t port, void *buf, size_t len);
