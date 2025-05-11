#pragma once
#include <dirent.h>
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
    NODISCARD ssize_t (*read)(struct fd *self, void *buf, size_t len);
    NODISCARD ssize_t (*write)(struct fd *self, void const *buf, size_t len);
    NODISCARD int (*seek)(struct fd *self, off_t offset, int whence);
    void (*close)(struct fd *self);
};

struct fd {
    struct list_node node;
    struct fd_ops const *ops;
    struct vfs_fscontext *fscontext;
    void *data;
    int id;
};

NODISCARD int vfs_register_file(struct fd *out, struct fd_ops const *ops, struct vfs_fscontext *fscontext, void *data);
void vfs_unregisterfile(struct fd *self);

////////////////////////////////////////////////////////////////////////////////

struct DIR {
    struct vfs_fscontext *fscontext;
    void *data;
};

/* TODO: Make all fields optional */
struct vfs_fstype_ops {
    /*
     * When mounting disk, you just give VFS system some memory to store its
     * own info. It has to be cleared to zero, and set `data` to filesystem
     * driver's private data.
     */
    NODISCARD int (*mount)(struct vfs_fscontext **out, struct ldisk *disk);
    NODISCARD int (*umount)(struct vfs_fscontext *self);
    /* Below are optional */
    NODISCARD int (*open)(struct fd **out, struct vfs_fscontext *self, char const *path, int flags);
    NODISCARD int (*opendir)(DIR **out, struct vfs_fscontext *self, char const *path);
    NODISCARD int (*closedir)(DIR *self);
    NODISCARD int (*readdir)(struct dirent *out, DIR *self);
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

NODISCARD int vfs_mount(char const *fstype, struct ldisk *disk, char const *mountpath);
NODISCARD int vfs_umount(char const *mountpath);
// `name` must be static string.
void vfs_register_fs_type(struct vfs_fstype *out, char const *name, struct vfs_fstype_ops const *ops);
void vfs_mount_root(void);
NODISCARD int vfs_open_file(struct fd **out, char const *path, int flags);
void vfs_close_file(struct fd *fd);
NODISCARD int vfs_opendir(DIR **out, char const *path);
int vfs_closedir(DIR *dir);
NODISCARD int vfs_readdir(struct dirent *out, DIR *dir);
NODISCARD ssize_t vfs_readfile(struct fd *fd, void *buf, size_t len);
NODISCARD ssize_t vfs_writefile(struct fd *fd, void const *buf, size_t len);
NODISCARD int vfs_seekfile(struct fd *fd, off_t offset, int whence);
