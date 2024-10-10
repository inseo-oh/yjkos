#include "fsinit.h"
#include <dirent.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/vfs.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

typedef struct fscontext fscontext_t;
struct fscontext {
    vfs_fscontext_t vfs_fscontext;
};

static FAILABLE_FUNCTION vfs_op_mount(vfs_fscontext_t **out, ldisk_t *disk) {
FAILABLE_PROLOGUE
    fscontext_t *context = heap_alloc(sizeof(*context), HEAP_FLAG_ZEROMEMORY);
    if (context == NULL) {
        THROW(ERR_NOMEM);
    }
    context->vfs_fscontext.data = context;
    (void)disk;
    *out = &context->vfs_fscontext;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(context);
    }
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION vfs_op_umount(vfs_fscontext_t *self) {
FAILABLE_PROLOGUE
    heap_free(self->data);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION vfs_op_open(fd_t **out, vfs_fscontext_t *self, char const *path, int flags) {
FAILABLE_PROLOGUE
    (void)out;
    (void)self;
    (void)path;
    (void)flags;
    THROW(ERR_NOENT);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static vfs_fstype_ops_t const FSTYPE_OPS = {
    .mount  = vfs_op_mount,
    .umount = vfs_op_umount,
    .open   = vfs_op_open,
};

static vfs_fstype_t s_fstype;

void fsinit_init_dummyfs(void) {
    vfs_registerfstype(&s_fstype, "dummyfs", &FSTYPE_OPS);
}
