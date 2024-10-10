#pragma once
#include <kernel/io/disk.h>
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <sys/types.h>

typedef struct vfs_fscontext vfs_fscontext_t;

////////////////////////////////////////////////////////////////////////////////

// File descriptor management
// XXX: VFS is temporary home for file descriptor management for now. This should go to
//      individual process once we have those implemented. 

typedef struct fd fd_t;
typedef struct fd_ops fd_ops_t;
struct fd_ops {
    FAILABLE_FUNCTION (*read)(fd_t *self, void *buf, size_t *len_inout);
    FAILABLE_FUNCTION (*write)(fd_t *self, void const *buf, size_t *len_inout);
    FAILABLE_FUNCTION (*seek)(fd_t *self, off_t offset, int whence);
    void (*close)(fd_t *self);
};

struct fd {
    list_node_t node;
    fd_ops_t const *ops;
    vfs_fscontext_t *fscontext;
    void *data;
    int id;
};

FAILABLE_FUNCTION vfs_registerfile(fd_t *out, fd_ops_t const *ops, vfs_fscontext_t *fscontext, void *data);
void vfs_unregisterfile(fd_t *self);

////////////////////////////////////////////////////////////////////////////////

typedef struct vfs_fstype vfs_fstype_t;
typedef struct vfs_fstype_ops vfs_fstype_ops_t;

struct vfs_fstype_ops {
    // When mounting disk, you just give VFS system some memory to store its own info, but
    // it has to be cleared to zero, and set `data` to filesystem driver's private data.
    FAILABLE_FUNCTION (*mount)(vfs_fscontext_t **out, ldisk_t *disk);
    FAILABLE_FUNCTION (*umount)(vfs_fscontext_t *self);
    FAILABLE_FUNCTION (*open)(fd_t **out, vfs_fscontext_t *self, char const *path, int flags);
};

struct vfs_fstype {
    char const *name;
    vfs_fstype_ops_t const *ops;
    list_node_t node;
};

struct vfs_fscontext {
    list_node_t node;
    void *data;
    char *mountpath;
    vfs_fstype_t *fstype;
    _Atomic size_t openfilecount;
};

FAILABLE_FUNCTION vfs_mount(char const *fstype, ldisk_t *disk, char const *mountpath);
FAILABLE_FUNCTION vfs_umount(char const *mountpath);
// `name` must be static string.
void vfs_registerfstype(vfs_fstype_t *out, char const *name, vfs_fstype_ops_t const *ops);
void vfs_mountroot(void);
FAILABLE_FUNCTION vfs_openfile(fd_t **out, char const *path, int flags);
void vfs_closefile(fd_t *fd);
FAILABLE_FUNCTION vfs_readfile(fd_t *fd, void *buf, size_t *len_inout);
FAILABLE_FUNCTION vfs_writefile(fd_t *fd, void const *buf, size_t *len_inout);
FAILABLE_FUNCTION vfs_seekfile(fd_t *fd, off_t offset, int whence);
