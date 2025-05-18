#include <kernel/fs/vfs.h>
#include <kernel/io/co.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/pathreader.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

/******************************************************************************/

/*
 * File descriptor management
 *
 * XXX: VFS is temporary home for file descriptor management for now. This
 * should go to individual process once we have those implemented.
 */

static _Atomic int s_next_fd_num = 0;

[[nodiscard]] int Vfs_RegisterFile(struct File *out, struct FileOps const *ops, struct Vfs_FsContext *fscontext, void *data) {
    out->id = s_next_fd_num++;
    if (out->id == INT_MAX) {
        /*
         * it's more likely that UBSan catched signed integer overflow, but we
         * check for it anyway.
         */
        Panic("vfs: TODO: Handle s_nextfdnum integer overflow");
    }
    out->ops = ops;
    out->data = data;
    out->fscontext = fscontext;
    fscontext->open_file_count++;
    return 0;
}

void Vfs_UnregisterFile(struct File *self) {
    if (self == NULL) {
        return;
    }
    self->fscontext->open_file_count--;
}

/******************************************************************************/

static struct List s_fstypes; /* struct Vfs_FsType items */
static struct List s_mounts;  /* struct Vfs_FsContext items */

/* Resolves and removes . and .. in the path. */
[[nodiscard]] static int remove_rel_path(char **newpath_out, char const *path) {
    int ret = 0;
    char *new_path = NULL;
    size_t size = strlen(path) + 2; /* Leave room for / and NULL terminator. */
    if (size == 0) {
        ret = -ENOMEM;
        goto fail;
    }
    new_path = Heap_Alloc(size, 0);
    if (new_path == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    struct PathReader reader;
    PathReader_Init(&reader, path);
    char *dest = new_path;
    while (1) {
        char const *name;
        ret = PathReader_Next(&name, &reader);
        if (ret == -ENOENT) {
            ret = 0;
            break;
        }
        if (ret < 0) {
            goto out;
        }
        if (strcmp(name, ".") == 0) {
            /* Do nothing */
        } else if (strcmp(name, "..") == 0) {
            char *found_pos = strrchr(new_path, '/');
            if (found_pos == NULL) {
                dest = new_path;
            } else {
                dest = found_pos;
            }
            *dest = '\0';
        } else if (name[0] != '\0') {
            *dest = '/';
            dest++;
            size_t name_len = strlen(name);
            memcpy(dest, name, name_len);
            dest += name_len;
        }
    }
    *dest = '\0';
    *newpath_out = new_path;
    goto out;
fail:
    Heap_Free(new_path);
out:
    return ret;
}

[[nodiscard]] static int mount(struct Vfs_FsType *fstype, struct LDisk *disk, char const *mountpath) {
    struct Vfs_FsContext *context;
    char *newmountpath;
    int ret;
    ret = remove_rel_path(&newmountpath, mountpath);

    if (ret < 0) {
        goto fail;
    }
    ret = fstype->ops->Mount(&context, disk);
    if (ret < 0) {
        goto fail;
    }
    /*
     * Since unmounting can also technically fail, we don't want any errors
     * after this point.
     */
    List_InsertBack(&s_mounts, &context->node, context);
    context->mount_path = newmountpath;
    context->fstype = fstype;
    goto out;
fail:
    Heap_Free(newmountpath);
out:
    return ret;
}

/* Returns ERR_INVAL if `mountpath` is not a mount point. */
[[nodiscard]] static int findmount(struct Vfs_FsContext **out, char const *mountpath) {
    int ret = 0;
    char *newmountpath = NULL;
    ret = remove_rel_path(&newmountpath, mountpath);
    if (ret < 0) {
        goto out;
    }
    assert(s_mounts.front != NULL);
    struct Vfs_FsContext *fscontext = NULL;
    LIST_FOREACH(&s_mounts, mountnode) {
        struct Vfs_FsContext *entry = mountnode->data;
        assert(entry);
        if (strcmp(entry->mount_path, newmountpath) == 0) {
            fscontext = entry;
            break;
        }
    }
    if (fscontext == NULL) {
        ret = -EINVAL;
        goto out;
    }
    *out = fscontext;
out:
    Heap_Free(newmountpath);
    return ret;
}

[[nodiscard]] int Vfs_Mount(char const *fstype, struct LDisk *disk, char const *mountpath) {
    int ret = -ENODEV;
    if (fstype == NULL) {
        /* Try all possible filesystems */
        LIST_FOREACH(&s_fstypes, fstypenode) {
            ret = mount(fstypenode->data, disk, mountpath);
            if ((ret < 0) && (ret != -EINVAL)) {
                /* If it was EINVAL, that's probably wrong filesystem type. For  others, abort and report the error. */
                goto out;
            } else if (ret == 0) {
                break;
            } else {
                ret = 0;
            }
        }
    } else {
        /* Find filesystem with given name. */
        struct Vfs_FsType *fstyperesult = NULL;
        LIST_FOREACH(&s_fstypes, fstypenode) {
            struct Vfs_FsType *currentfstype = fstypenode->data;
            if (strcmp(currentfstype->name, fstype) == 0) {
                fstyperesult = currentfstype;
            }
        }
        if (fstyperesult == NULL) {
            ret = -ENODEV;
            goto out;
        }
        ret = mount(fstyperesult, disk, mountpath);
        if (ret < 0) {
            goto out;
        }
    }
out:
    return ret;
}

[[nodiscard]] int Vfs_Umount(char const *mountpath) {
    int ret = 0;
    struct Vfs_FsContext *fscontext;
    ret = findmount(&fscontext, mountpath);
    if (ret < 0) {
        goto out;
    }
    char *contextmountpath = fscontext->mount_path;
    ret = fscontext->fstype->ops->Umount(fscontext);
    if (ret < 0) {
        goto out;
    }
    Heap_Free(contextmountpath);
out:
    return ret;
}

void Vfs_RegisterFsType(struct Vfs_FsType *out, char const *name, struct Vfs_FsTypeOps const *ops) {
    memset(out, 0, sizeof(*out));
    out->name = name;
    out->ops = ops;
    List_InsertBack(&s_fstypes, &out->node, out);
}

void Vfs_MountRoot(void) {
    Co_Printf("vfs: mounting the first usable filesystem...\n");
    struct List *devlist = Iodev_GetList(IODEV_TYPE_LOGICAL_DISK);
    if (devlist == NULL || devlist->front == NULL) {
        Co_Printf("no logical disks. Mounting dummyfs as root\n");
        int ret = Vfs_Mount("dummyfs", NULL, "/");
        MUST_SUCCEED(ret);
        return;
    }
    LIST_FOREACH(devlist, devnode) {
        struct IoDev *iodev = devnode->data;
        struct LDisk *disk = iodev->data;
        int status = Vfs_Mount(NULL, disk, "/");
        if (status < 0) {
            continue;
        }
        break;
    }
}

[[nodiscard]] static int resolve_path(
    char const *path,
    void (*callback)(struct Vfs_FsContext *fscontext, char const *path, void *data),
    void *data) {
    int ret = 0;
    char *newpath = NULL;

    ret = remove_rel_path(&newpath, path);
    if (ret < 0) {
        goto fail;
    }
    /* There should be a rootfs at very least. */
    assert(s_mounts.front != NULL);
    struct Vfs_FsContext *result = NULL;
    size_t lastmatchlen = 0;
    LIST_FOREACH(&s_mounts, mountnode) {
        struct Vfs_FsContext *entry = mountnode->data;
        size_t len = strlen(entry->mount_path);
        if (lastmatchlen <= len) {
            if (strncmp(entry->mount_path, path, len) == 0) {
                result = entry;
                lastmatchlen = len;
            }
        }
    }
    assert(result != NULL);
    callback(result, &newpath[lastmatchlen], data);
    goto out;
fail:
    Heap_Free(newpath);
out:
    return ret;
}

struct openfilecontext {
    struct File *fdresult;
    int flags;
    int ret;
};

static void resolve_path_callback_open_file(struct Vfs_FsContext *fscontext, char const *path, void *data) {
    struct openfilecontext *context = data;
    if (fscontext->fstype->ops->Open == NULL) {
        context->ret = -ENOENT;
    } else {
        context->ret = fscontext->fstype->ops->Open(&context->fdresult, fscontext, path, context->flags);
    }
}

[[nodiscard]] int Vfs_OpenFile(struct File **out, char const *path, int flags) {
    struct openfilecontext context;
    context.flags = flags;
    int ret = resolve_path(path, resolve_path_callback_open_file, &context);
    if (ret < 0) {
        return ret;
    }
    if (context.ret < 0) {
        return context.ret;
    }
    *out = context.fdresult;
    return 0;
}

void Vfs_CloseFile(struct File *fd) {
    fd->ops->close(fd);
}

struct open_dir_context {
    DIR *dirresult;
    int ret;
};

static void resolve_path_callback_open_directory(struct Vfs_FsContext *fs_context, char const *path, void *data) {
    struct open_dir_context *context = data;
    if (fs_context->fstype->ops->OpenDir == NULL) {
        context->ret = -ENOENT;
    } else {
        context->ret = fs_context->fstype->ops->OpenDir(&context->dirresult, fs_context, path);
    }
}

int Vfs_OpenDir(DIR **out, char const *path) {
    struct open_dir_context context;
    int ret = resolve_path(path, resolve_path_callback_open_directory, &context);
    if (ret < 0) {
        return ret;
    }
    if (context.ret < 0) {
        return context.ret;
    }
    *out = context.dirresult;
    return 0;
}

[[nodiscard]] int Vfs_CloseDir(DIR *dir) {
    if (dir == NULL) {
        return -EBADF;
    }
    if (dir->fscontext->fstype->ops->CloseDir == NULL) {
        return -EBADF;
    }
    return dir->fscontext->fstype->ops->CloseDir(dir);
}

[[nodiscard]] int Vfs_ReadDir(struct dirent *out, DIR *dir) {
    if (dir == NULL) {
        return -EBADF;
    }
    if (dir->fscontext->fstype->ops->ReadDir == NULL) {
        return -EBADF;
    }
    return dir->fscontext->fstype->ops->ReadDir(out, dir);
}

[[nodiscard]] ssize_t Vfs_ReadFile(struct File *fd, void *buf, size_t len) {
    return fd->ops->read(fd, buf, len);
}

[[nodiscard]] ssize_t Vfs_WriteFile(struct File *fd, void const *buf, size_t len) {
    return fd->ops->write(fd, buf, len);
}

[[nodiscard]] int Vfs_SeekFile(struct File *fd, off_t offset, int whence) {
    return fd->ops->seek(fd, offset, whence);
}
