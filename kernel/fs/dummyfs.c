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


static FAILABLE_FUNCTION vfs_op_mount(struct vfs_fscontext **out, struct ldisk *disk) {
FAILABLE_PROLOGUE
    struct vfs_fscontext *context = heap_alloc(sizeof(*context), HEAP_FLAG_ZEROMEMORY);
    if (context == NULL) {
        THROW(ERR_NOMEM);
    }
    (void)disk;
    *out = context;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(context);
    }
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION vfs_op_umount(struct vfs_fscontext *self) {
FAILABLE_PROLOGUE
    heap_free(self);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION vfs_op_open(struct fd **out, struct vfs_fscontext *self, char const *path, int flags) {
FAILABLE_PROLOGUE
    (void)out;
    (void)self;
    (void)path;
    (void)flags;
    THROW(ERR_NOENT);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static struct vfs_fstype_ops const FSTYPE_OPS = {
    .mount  = vfs_op_mount,
    .umount = vfs_op_umount,
    .open   = vfs_op_open,
};

static struct vfs_fstype s_fstype;

void fsinit_init_dummyfs(void) {
    vfs_registerfstype(&s_fstype, "dummyfs", &FSTYPE_OPS);
}
