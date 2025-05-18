#pragma once
#include <kernel/dev/ps2.h>
#include <kernel/lib/diagnostics.h>
#include <stdint.h>

[[nodiscard]] int Ps2kbd_Init(struct Ps2Port *port);

