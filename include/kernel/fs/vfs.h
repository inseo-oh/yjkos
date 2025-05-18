#pragma once
#include <dirent.h>
#include <kernel/io/disk.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <stddef.h>
#include <sys/types.h>

struct Vfs_FsContext;
struct File;


/*
 * File descriptor management
 *
 * XXX: VFS is temporary home for file descriptor management for now.
 * This should go to individual process once we have those implemented.
 */
struct FileOps {
    ssize_t (*read)(struct File *self, void *buf, size_t len);
    ssize_t (*write)(struct File *self, void const *buf, size_t len);
    int (*seek)(struct File *self, off_t offset, int whence);
    void (*close)(struct File *self);
};

struct File {
    struct List_Node node;
    struct FileOps const *ops;
    struct Vfs_FsContext *fscontext;
    void *data;
    int id;
};

[[nodiscard]] int Vfs_RegisterFile(struct File *out, struct FileOps const *ops, struct Vfs_FsContext *fscontext, void *data);
void Vfs_UnregisterFile(struct File *self);

struct DIR {
    struct Vfs_FsContext *fscontext;
    void *data;
};

/* TODO: Make all fields optional */
struct Vfs_FsTypeOps {
    /*
     * When mounting disk, you just give VFS system some memory to store its own info.
     * It has to be cleared to zero, and set `data` to filesystem driver's private data.
     */
    int (*Mount)(struct Vfs_FsContext **out, struct LDisk *disk);
    int (*Umount)(struct Vfs_FsContext *self);
    /* Below are optional */
    int (*Open)(struct File **out, struct Vfs_FsContext *self, char const *path, int flags);
    int (*OpenDir)(DIR **out, struct Vfs_FsContext *self, char const *path);
    int (*CloseDir)(DIR *self);
    int (*ReadDir)(struct dirent *out, DIR *self);
};

struct Vfs_FsType {
    char const *name;
    struct Vfs_FsTypeOps const *ops;
    struct List_Node node;
};

struct Vfs_FsContext {
    struct List_Node node;
    void *data;
    char *mount_path;
    struct Vfs_FsType *fstype;
    _Atomic size_t open_file_count;
};

[[nodiscard]] int Vfs_Mount(char const *fstype, struct LDisk *disk, char const *mountpath);
[[nodiscard]] int Vfs_Umount(char const *mountpath);
/* `name` must be static string. */
void Vfs_RegisterFsType(struct Vfs_FsType *out, char const *name, struct Vfs_FsTypeOps const *ops);
void Vfs_MountRoot(void);
[[nodiscard]] int Vfs_OpenFile(struct File **out, char const *path, int flags);
void Vfs_CloseFile(struct File *fd);
[[nodiscard]] int Vfs_OpenDir(DIR **out, char const *path);
int Vfs_CloseDir(DIR *dir);
[[nodiscard]] int Vfs_ReadDir(struct dirent *out, DIR *dir);
[[nodiscard]] ssize_t Vfs_ReadFile(struct File *fd, void *buf, size_t len);
[[nodiscard]] ssize_t Vfs_WriteFile(struct File *fd, void const *buf, size_t len);
[[nodiscard]] int Vfs_SeekFile(struct File *fd, off_t offset, int whence);
