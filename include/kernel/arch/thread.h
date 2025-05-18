#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

struct Arch_Thread;

/*
 * Those init_ parameters are only valid for initial setup.
 * This of course applies to any new thread, but the boot thread is exception: It's thread for already running code.
 *
 * Returns NULL if there's not enough memory.
 */
[[nodiscard]] struct Arch_Thread *Arch_Thread_Create(size_t init_stacksize, void (*init_mainfunc)(void *), void *init_data);
void Arch_Thread_Destroy(struct Arch_Thread *thread);
void Arch_Thread_Switch(struct Arch_Thread *from, struct Arch_Thread *to);
