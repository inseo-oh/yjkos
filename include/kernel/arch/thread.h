#pragma once
#include <kernel/status.h>
#include <stdint.h>
#include <stddef.h>

struct arch_thread;

FAILABLE_FUNCTION arch_thread_create(struct arch_thread **thread_out, size_t minstacksize, uintptr_t entryaddr);
void arch_thread_destroy(struct arch_thread *thread);
void arch_thread_switch(struct arch_thread *from, struct arch_thread *to);

