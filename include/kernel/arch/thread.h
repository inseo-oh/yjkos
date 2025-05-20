#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

struct arch_thread;

/*
 * Those init_ parameters are only valid for initial setup.
 * This of course applies to any new thread, but the boot thread is exception: It's thread for already running code.
 *
 * Returns NULL if there's not enough memory.
 */
[[nodiscard]] struct arch_thread *arch_thread_create(size_t init_stacksize, void (*init_mainfunc)(void *), void *init_data);
void arch_thread_destroy(struct arch_thread *thread);
void arch_thread_switch(struct arch_thread *from, struct arch_thread *to);
