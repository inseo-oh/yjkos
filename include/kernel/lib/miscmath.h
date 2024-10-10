#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool isaligned(size_t x, size_t align);
size_t alignup(size_t x, size_t align);
size_t aligndown(size_t x, size_t align);
size_t sizetoblocks(size_t size, size_t blocksize);

uint16_t uint16leat(uint8_t const *ptr);
uint32_t uint32leat(uint8_t const *ptr);
