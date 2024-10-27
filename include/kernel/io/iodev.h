#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <stddef.h>
#include <stdbool.h>

static char const *const IODEV_TYPE_PHYSICAL_DISK = "pdisk";
static char const *const IODEV_TYPE_LOGICAL_DISK  = "ldisk";
static char const *const IODEV_TYPE_PS2PORT       = "ps2port";
static char const *const IODEV_TYPE_KEYBOARD      = "kbd";

struct iodev {
    struct list_node node;
    size_t id;
    char const *devtype;
    void *data;
};

// `devtype` must be static string.
WARN_UNUSED_RESULT int iodev_register(
    struct iodev *dev_out, char const *devtype, void *data);
void iodev_printf(struct iodev *device, char const *fmt, ...);
struct list *iodev_getlist(char const *devtype);
