#pragma once
#include <kernel/dev/ps2.h>
#include <kernel/lib/diagnostics.h>
#include <stdint.h>

[[nodiscard]] int ps2kbd_init(struct ps2port *port);

