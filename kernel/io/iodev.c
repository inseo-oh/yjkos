#include <errno.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/io/iodev.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

struct iodevtype {
    struct list_node node;
    struct list devices;
    char const *name;
    _Atomic size_t nextid;
};

static struct list s_iodevtypes;

static struct iodevtype *getiodevtypefor(char const *devtype) {
    LIST_FOREACH(&s_iodevtypes, typenode) {
        struct iodevtype *type = typenode->data;
        if (kstrcmp(devtype, type->name) == 0) {
            return type;
        }
    }
    return nullptr;
}

[[nodiscard]] int iodev_register(struct iodev *dev_out, char const *devtype, void *data) {
    int result = 0;
    bool prev_interrupts = arch_irq_disable();
    /* Look for existing iodevtype */
    dev_out->devtype = devtype;
    dev_out->data = data;
    struct iodevtype *desttype = getiodevtypefor(devtype);
    /* If there's no such type, create a new type. */
    if (desttype == nullptr) {
        struct iodevtype *type = heap_alloc(sizeof(*type), HEAP_FLAG_ZEROMEMORY);
        if (type == nullptr) {
            goto fail_oom;
        }
        type->name = devtype;
        desttype = type;
        list_insert_back(&s_iodevtypes, &type->node, type);
    }
    dev_out->id = desttype->nextid++;
    if (dev_out->id == SIZE_MAX) {
        /* nextid overflowed */
        panic("iodev: TODO: Handle nextid integer overflow");
    }
    list_insert_back(&desttype->devices, &dev_out->node, dev_out);
    goto out;
fail_oom:
    result = -ENOMEM;
out:
    arch_irq_restore(prev_interrupts);
    return result;
}

void iodev_printf(struct iodev *device, char const *fmt, ...) {
    co_printf("%s%d: ", device->devtype, device->id);
    va_list ap;
    va_start(ap, fmt);
    co_vprintf(fmt, ap);
    va_end(ap);
}

struct list *iodev_get_list(char const *devtype) {
    struct iodevtype *type = getiodevtypefor(devtype);
    if (type == nullptr) {
        return nullptr;
    }
    return &type->devices;
}
