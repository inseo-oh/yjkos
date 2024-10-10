#include <kernel/arch/interrupts.h>
#include <kernel/io/iodev.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <kernel/status.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

typedef struct iodevtype iodevtype_t;
struct iodevtype {
    list_node_t node;
    list_t devices;
    char const *name;
    _Atomic size_t nextid;
};

static list_t s_devtypes; // Contains iodevtype_t nodes.

static iodevtype_t *getiodevtypefor(char const *devtype) {
    for (list_node_t *typenode = s_devtypes.front; typenode != NULL; typenode = typenode->next) {
        iodevtype_t *type = typenode->data;
        if (strcmp(devtype, type->name) == 0) {
            return type;
        }
    }
    return NULL;
}

FAILABLE_FUNCTION iodev_register(iodev_t *dev_out, char const *devtype, void *data) {
FAILABLE_PROLOGUE
    bool previnterrupts = arch_interrupts_disable();
    // Look for existing iodevtype_t
    dev_out->devtype = devtype;
    dev_out->data = data;
    iodevtype_t *desttype = getiodevtypefor(devtype);
    // If there's no such type, create a new type.
    if (desttype == NULL) {
        iodevtype_t *type = heap_alloc(sizeof(*type), HEAP_FLAG_ZEROMEMORY);
        if (type == NULL) {
            THROW(ERR_NOMEM);
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
    list_insertback(&desttype->devices, &dev_out->node, dev_out);

FAILABLE_EPILOGUE_BEGIN
    interrupts_restore(previnterrupts);
FAILABLE_EPILOGUE_END
}

void iodev_printf(iodev_t *device, char const *fmt, ...) {
    tty_printf("%s%d: ", device->devtype, device->id);
    va_list ap;
    va_start(ap, fmt);
    tty_vprintf(fmt, ap);
    va_end(ap);
}

list_t *iodev_getlist(char const *devtype) {
    iodevtype_t *type = getiodevtypefor(devtype);
    if (type == NULL) {
        return NULL;
    }
    return &type->devices;
}
