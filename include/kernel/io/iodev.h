#pragma once
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stddef.h>

static char const *const IODEV_TYPE_PHYSICAL_DISK = "pdisk";
static char const *const IODEV_TYPE_LOGICAL_DISK  = "ldisk";
static char const *const IODEV_TYPE_PS2PORT       = "ps2port";
static char const *const IODEV_TYPE_KEYBOARD      = "kbd";

typedef struct iodev iodev_t;
struct iodev {
    list_node_t node;
    size_t id;
    char const *devtype;
    void *data;
};

// `devtype` must be static string.
FAILABLE_FUNCTION iodev_register(iodev_t *dev_out, char const *devtype, void *data);
void iodev_printf(iodev_t *device, char const *fmt, ...);
list_t *iodev_getlist(char const *devtype);
