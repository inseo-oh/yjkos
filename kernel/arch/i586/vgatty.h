#pragma once
#include <kernel/types.h>
#include <stddef.h>

/* TODO: Update to work with new TTY subsystem. */

/*
 * ArchI586_VgaTty_InitEarlyDebug should only be enabled when debugging very early boot process,
 * and you *have* to boot into text mode as it uses hardcoded textmode parameters.
 */
void ArchI586_VgaTty_InitEarlyDebug(void);

void ArchI586_VgaTty_Init(PHYSPTR baseaddr, size_t columns, size_t rows, size_t bytes_per_row);
