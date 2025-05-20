#pragma once
#include <dirent.h>
#include <kernel/io/disk.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <stddef.h>
#include <sys/types.h>

struct vfs_fscontext;
struct file;


/*
 * File descriptor management
 *
 * XXX: VFS is temporary home for file descriptor management for now.
 * This should go to individual process once we have those implemented.
 */
struct file_ops {
    ssize_t (*read)(struct file *self, void *buf, size_t len);
    ssize_t (*write)(struct file *self, void const *buf, size_t len);
    int (*seek)(struct file *self, off_t offset, int whence);
    void (*close)(struct file *self);
};

struct file {
    struct list_node node;
    struct file_ops const *ops;
    struct vfs_fscontext *fscontext;
    void *data;
    int id;
};

[[nodiscard]] int vfs_register_file(struct file *out, struct file_ops const *ops, struct vfs_fscontext *fscontext, void *data);
void vfs_unregister_file(struct file *self);

struct DIR {
    struct vfs_fscontext *fscontext;
    void *data;
};

/* TODO: Make all fields optional */
struct vfs_fstype_ops {
    /*
     * When mounting disk, you just give VFS system some memory to store its own info.
     * It has to be cleared to zero, and set `data` to filesystem driver's private data.
     */
    int (*mount)(struct vfs_fscontext **out, struct ldisk *disk);
    int (*umount)(struct vfs_fscontext *self);
    /* Below are optional */
    int (*open)(struct file **out, struct vfs_fscontext *self, char const *path, int flags);
    int (*open_directory)(DIR **out, struct vfs_fscontext *self, char const *path);
    int (*close_directory)(DIR *self);
    int (*read_directory)(struct dirent *out, DIR *self);
};

struct vfs_fstype {
    char const *name;
    struct vfs_fstype_ops const *ops;
    struct list_node node;
};

struct vfs_fscontext {
    struct list_node node;
    void *data;
    char *mount_path;
    struct vfs_fstype *fstype;
    _Atomic size_t open_file_count;
};

[[nodiscard]] int vfs_mount(char const *fstype, struct ldisk *disk, char const *mountpath);
[[nodiscard]] int vfs_umount(char const *mountpath);
/* `name` must be static string. */
void vfs_register_fstype(struct vfs_fstype *out, char const *name, struct vfs_fstype_ops const *ops);
void vfs_mount_root(void);
[[nodiscard]] int vfs_open_file(struct file **out, char const *path, int flags);
void vfs_close_file(struct file *fd);
[[nodiscard]] int vfs_open_directory(DIR **out, char const *path);
int vfs_close_directory(DIR *dir);
[[nodiscard]] int vfs_read_directory(struct dirent *out, DIR *dir);
[[nodiscard]] ssize_t vfs_read_file(struct file *fd, void *buf, size_t len);
[[nodiscard]] ssize_t vfs_write_file(struct file *fd, void const *buf, size_t len);
[[nodiscard]] int vfs_seek_file(struct file *fd, off_t offset, int whence);
