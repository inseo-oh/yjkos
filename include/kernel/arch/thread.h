#pragma once
#include <kernel/status.h>
#include <stdint.h>
#include <stddef.h>

typedef struct arch_thread arch_thread_t;

FAILABLE_FUNCTION arch_thread_create(arch_thread_t **thread_out, size_t minstacksize, uintptr_t entryaddr);
void arch_thread_destroy(arch_thread_t *thread);
void arch_thread_switch(arch_thread_t *from, arch_thread_t *to);

