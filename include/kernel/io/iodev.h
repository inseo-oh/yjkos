#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <stddef.h>

static char const *const IODEV_TYPE_PHYSICAL_DISK = "pdisk";
static char const *const IODEV_TYPE_LOGICAL_DISK = "ldisk";
static char const *const IODEV_TYPE_PS2PORT = "ps2port";
static char const *const IODEV_TYPE_KEYBOARD = "kbd";
static char const *const IODEV_TYPE_TTY = "tty";

struct IoDev {
    struct List_Node node;
    size_t id;
    char const *devtype;
    void *data;
};

/* `devtype` must be static string. */
[[nodiscard]] int Iodev_Register(struct IoDev *dev_out, char const *devtype, void *data);
void Iodev_Printf(struct IoDev *device, char const *fmt, ...);
struct List *Iodev_GetList(char const *devtype);
