#pragma once
#include <kernel/lib/diagnostics.h>
#include <stdint.h>
#include <stddef.h>

struct arch_thread;

// Returns NULL when there's not enough memory.
WARN_UNUSED_RESULT struct arch_thread *arch_thread_create(size_t minstacksize, uintptr_t entryaddr);
void arch_thread_destroy(struct arch_thread *thread);
void arch_thread_switch(struct arch_thread *from, struct arch_thread *to);

