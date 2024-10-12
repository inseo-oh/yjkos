#pragma once
#include <kernel/dev/ps2.h>
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stdint.h>

FAILABLE_FUNCTION ps2kbd_init(struct ps2port *port);

