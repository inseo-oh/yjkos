#include "fsinit.h"
#include <errno.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/disk.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <stddef.h>

[[nodiscard]] static int vfs_op_mount(struct vfs_fscontext **out, struct ldisk *disk) {
    int ret = 0;
    struct vfs_fscontext *context = heap_alloc(sizeof(*context), HEAP_FLAG_ZEROMEMORY);
    if (context == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    (void)disk;
    *out = context;
    goto out;
fail:
    heap_free(context);
out:
    return ret;
}

[[nodiscard]] static int vfs_op_umount(struct vfs_fscontext *self) {
    heap_free(self);
    return 0;
}

[[nodiscard]] static int vfs_op_open(struct file **out, struct vfs_fscontext *self, char const *path, int flags) {
    (void)out;
    (void)self;
    (void)path;
    (void)flags;
    return -ENOENT;
}

static struct vfs_fstype_ops const FSTYPE_OPS = {
    .mount = vfs_op_mount,
    .umount = vfs_op_umount,
    .open = vfs_op_open,
};

static struct vfs_fstype s_fstype;

void fsinit_init_dummyfs(void) {
    vfs_register_fstype(&s_fstype, "dummyfs", &FSTYPE_OPS);
}
