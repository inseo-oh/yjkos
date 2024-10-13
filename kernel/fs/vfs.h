#pragma once
#include <kernel/io/disk.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <sys/types.h>

struct vfs_fscontext;
struct fd;

////////////////////////////////////////////////////////////////////////////////

/*
 * File descriptor management
 *
 * XXX: VFS is temporary home for file descriptor management for now. This
 * should go to individual process once we have those implemented. 
 */
struct fd_ops {
    WARN_UNUSED_RESULT ssize_t (*read)(struct fd *self, void *buf, size_t len);
    WARN_UNUSED_RESULT ssize_t (*write)(
        struct fd *self, void const *buf, size_t len);
    WARN_UNUSED_RESULT int (*seek)(
        struct fd *self, off_t offset, int whence);
    void (*close)(struct fd *self);
};

struct fd {
    struct list_node node;
    struct fd_ops const *ops;
    struct vfs_fscontext *fscontext;
    void *data;
    int id;
};

WARN_UNUSED_RESULT int vfs_registerfile(
    struct fd *out, struct fd_ops const *ops, struct vfs_fscontext *fscontext,
    void *data);
void vfs_unregisterfile(struct fd *self);

////////////////////////////////////////////////////////////////////////////////

struct vfs_fstype_ops {
    /*
     * When mounting disk, you just give VFS system some memory to store its
     * own info. It has to be cleared to zero, and set `data` to filesystem
     * driver's private data.
     */
    WARN_UNUSED_RESULT int (*mount)(
        struct vfs_fscontext **out, struct ldisk *disk);
    WARN_UNUSED_RESULT int (*umount)(struct vfs_fscontext *self);
    WARN_UNUSED_RESULT int (*open)(
        struct fd **out, struct vfs_fscontext *self, char const *path,
        int flags);
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

WARN_UNUSED_RESULT int vfs_mount(
    char const *fstype, struct ldisk *disk, char const *mountpath);
WARN_UNUSED_RESULT int vfs_umount(char const *mountpath);
// `name` must be static string.
void vfs_registerfstype(
    struct vfs_fstype *out, char const *name, struct vfs_fstype_ops const *ops);
void vfs_mountroot(void);
WARN_UNUSED_RESULT int vfs_openfile(
    struct fd **out, char const *path, int flags);
void vfs_closefile(struct fd *fd);
WARN_UNUSED_RESULT ssize_t vfs_readfile(struct fd *fd, void *buf, size_t len);
WARN_UNUSED_RESULT ssize_t vfs_writefile(
    struct fd *fd, void const *buf, size_t len);
WARN_UNUSED_RESULT int vfs_seekfile(struct fd *fd, off_t offset, int whence);
