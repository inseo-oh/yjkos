#include "fsinit.h"
#include <errno.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/disk.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/heap.h>
#include <stdint.h>
#include <string.h>

[[nodiscard]] static int vfs_op_mount(struct Vfs_FsContext **out, struct LDisk *disk) {
    int ret = 0;
    struct Vfs_FsContext *context = Heap_Alloc(sizeof(*context), HEAP_FLAG_ZEROMEMORY);
    if (context == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    (void)disk;
    *out = context;
    goto out;
fail:
    Heap_Free(context);
out:
    return ret;
}

[[nodiscard]] static int vfs_op_umount(struct Vfs_FsContext *self) {
    Heap_Free(self);
    return 0;
}

[[nodiscard]] static int vfs_op_open(struct File **out, struct Vfs_FsContext *self, char const *path, int flags) {
    (void)out;
    (void)self;
    (void)path;
    (void)flags;
    return -ENOENT;
}

static struct Vfs_FsTypeOps const FSTYPE_OPS = {
    .Mount = vfs_op_mount,
    .Umount = vfs_op_umount,
    .Open = vfs_op_open,
};

static struct Vfs_FsType s_fstype;

void FsInit_InitDummyFs(void) {
    Vfs_RegisterFsType(&s_fstype, "dummyfs", &FSTYPE_OPS);
}
