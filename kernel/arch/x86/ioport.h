#pragma once
#include <stddef.h>
#include <stdint.h>

typedef uint16_t archx86_ioaddr_t;

void archx86_out8(archx86_ioaddr_t port, uint8_t val);
void archx86_out16(archx86_ioaddr_t port, uint16_t val);
void archx86_out32(archx86_ioaddr_t port, uint32_t val);
uint8_t archx86_in8(archx86_ioaddr_t port);
uint16_t archx86_in16(archx86_ioaddr_t port);
uint32_t archx86_in32(archx86_ioaddr_t port);
void archx86_in16rep(archx86_ioaddr_t port, void *buf, size_t len);
