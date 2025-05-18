#pragma once
#include <stddef.h>
#include <stdint.h>

void ArchI586_Out8(uint16_t port, uint8_t val);
void ArchI586_Out16(uint16_t port, uint16_t val);
void ArchI586_Out32(uint16_t port, uint32_t val);
uint8_t ArchI586_In8(uint16_t port);
uint16_t ArchI586_In16(uint16_t port);
uint32_t ArchI586_In32(uint16_t port);
void ArchI586_In16Rep(uint16_t port, void *buf, size_t len);
