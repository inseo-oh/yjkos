#include <kernel/arch/interrupts.h>
#include <kernel/io/iodev.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

struct iodevtype {
    struct list_node node;
    struct list devices;
    char const *name;
    _Atomic size_t nextid;
};

static struct list s_devtypes; // Contains iodevtype nodes.

static struct iodevtype *getiodevtypefor(char const *devtype) {
    LIST_FOREACH(&s_devtypes, typenode) {
        struct iodevtype *type = typenode->data;
        if (strcmp(devtype, type->name) == 0) {
            return type;
        }
    }
    return NULL;
}

WARN_UNUSED_RESULT bool iodev_register(
    struct iodev *dev_out, char const *devtype, void *data)
{
    bool result = true;
    bool previnterrupts = arch_interrupts_disable();
    // Look for existing iodevtype_t
    dev_out->devtype = devtype;
    dev_out->data = data;
    struct iodevtype *desttype = getiodevtypefor(devtype);
    // If there's no such type, create a new type.
    if (desttype == NULL) {
        struct iodevtype *type = heap_alloc(
            sizeof(*type), HEAP_FLAG_ZEROMEMORY);
        if (type == NULL) {
            goto fail_oom;
        }
        type->name = devtype;
        desttype = type;
        list_insertback(&s_devtypes, &type->node, type);
    }
    dev_out->id = desttype->nextid++;
    if (dev_out->id == SIZE_MAX) {
        // nextid overflowed
        panic("iodev: TODO: Handle nextid integer overflow");
    }
    list_insertback(
        &desttype->devices, &dev_out->node, dev_out);
    goto out;
fail_oom:
    result = false;
out:
    interrupts_restore(previnterrupts);
    return result;
}

void iodev_printf(struct iodev *device, char const *fmt, ...) {
    tty_printf("%s%d: ", device->devtype, device->id);
    va_list ap;
    va_start(ap, fmt);
    tty_vprintf(fmt, ap);
    va_end(ap);
}

struct list *iodev_getlist(char const *devtype) {
    struct iodevtype *type = getiodevtypefor(devtype);
    if (type == NULL) {
        return NULL;
    }
    return &type->devices;
}
