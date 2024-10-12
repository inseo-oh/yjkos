#pragma once
#include <kernel/io/disk.h>
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <sys/types.h>

struct vfs_fscontext;
struct fd;

////////////////////////////////////////////////////////////////////////////////

// File descriptor management
// XXX: VFS is temporary home for file descriptor management for now. This should go to
//      individual process once we have those implemented. 

struct fd_ops {
    FAILABLE_FUNCTION (*read)(struct fd *self, void *buf, size_t *len_inout);
    FAILABLE_FUNCTION (*write)(struct fd *self, void const *buf, size_t *len_inout);
    FAILABLE_FUNCTION (*seek)(struct fd *self, off_t offset, int whence);
    void (*close)(struct fd *self);
};

struct fd {
    struct list_node node;
    struct fd_ops const *ops;
    struct vfs_fscontext *fscontext;
    void *data;
    int id;
};

FAILABLE_FUNCTION vfs_registerfile(struct fd *out, struct fd_ops const *ops, struct vfs_fscontext *fscontext, void *data);
void vfs_unregisterfile(struct fd *self);

////////////////////////////////////////////////////////////////////////////////

struct vfs_fstype_ops {
    // When mounting disk, you just give VFS system some memory to store its own info, but
    // it has to be cleared to zero, and set `data` to filesystem driver's private data.
    FAILABLE_FUNCTION (*mount)(struct vfs_fscontext **out, struct ldisk *disk);
    FAILABLE_FUNCTION (*umount)(struct vfs_fscontext *self);
    FAILABLE_FUNCTION (*open)(struct fd **out, struct vfs_fscontext *self, char const *path, int flags);
};

struct vfs_fstype {
    char const *name;
    struct vfs_fstype_ops const *ops;
    struct list_node node;
};

struct vfs_fscontext {
    struct list_node node;
    void *data;
    char *mountpath;
    struct vfs_fstype *fstype;
    _Atomic size_t openfilecount;
};

FAILABLE_FUNCTION vfs_mount(char const *fstype, struct ldisk *disk, char const *mountpath);
FAILABLE_FUNCTION vfs_umount(char const *mountpath);
// `name` must be static string.
void vfs_registerfstype(struct vfs_fstype *out, char const *name, struct vfs_fstype_ops const *ops);
void vfs_mountroot(void);
FAILABLE_FUNCTION vfs_openfile(struct fd **out, char const *path, int flags);
void vfs_closefile(struct fd *fd);
FAILABLE_FUNCTION vfs_readfile(struct fd *fd, void *buf, size_t *len_inout);
FAILABLE_FUNCTION vfs_writefile(struct fd *fd, void const *buf, size_t *len_inout);
FAILABLE_FUNCTION vfs_seekfile(struct fd *fd, off_t offset, int whence);
