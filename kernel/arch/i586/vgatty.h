#pragma once
#include <kernel/types.h>
#include <stddef.h>

// archi586_vgatty_init_earlydebug should only be enabled when debugging very early boot process,
// and you *have* to boot into text mode as it uses hardcoded textmode parameters.
void archi586_vgatty_init_earlydebug(void);

void archi586_vgatty_init(physptr baseaddr, size_t columns, size_t rows, size_t bytesperrow);

