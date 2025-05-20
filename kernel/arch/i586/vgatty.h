#pragma once
#include <kernel/types.h>
#include <stddef.h>

/* TODO: Update to work with new TTY subsystem. */

/*
 * archi586_vgatty_init_early_debug should only be enabled when debugging very early boot process,
 * and you *have* to boot into text mode as it uses hardcoded textmode parameters.
 */
void archi586_vgatty_init_early_debug(void);

void archi586_vgatty_init(PHYSPTR baseaddr, size_t columns, size_t rows, size_t bytes_per_row);
