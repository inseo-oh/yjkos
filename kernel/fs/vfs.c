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

////////////////////////////////////////////////////////////////////////////////

/*
 * File descriptor management
 *
 * XXX: VFS is temporary home for file descriptor management for now. This
 * should go to individual process once we have those implemented.
 */

static _Atomic int s_next_fd_num = 0;

NODISCARD int vfs_register_file(struct fd *out, struct fd_ops const *ops, struct vfs_fscontext *fscontext, void *data) {
    out->id = s_next_fd_num++;
    if (out->id == INT_MAX) {
        /*
         * it's more likely that UBSan catched signed integer overflow, but we
         * check for it anyway.
         */
        panic("vfs: TODO: Handle s_nextfdnum integer overflow");
    }
    out->ops = ops;
    out->data = data;
    out->fscontext = fscontext;
    fscontext->openfilecount++;
    return 0;
}

void vfs_unregisterfile(struct fd *self) {
    if (self == NULL) {
        return;
    }
    self->fscontext->openfilecount--;
}

////////////////////////////////////////////////////////////////////////////////

static struct list s_fstypes; // struct vfs_fstype items
static struct list s_mounts;  // struct vfs_fscontext items

// Resolves and removes . and .. in the path.
NODISCARD static int remove_rel_path(char **newpath_out, char const *path) {
    int ret = 0;
    char *new_path = NULL;
    size_t size = strlen(path) + 2; // Leave room for / and NULL terminator.
    if (size == 0) {
        ret = -ENOMEM;
        goto fail;
    }
    new_path = heap_alloc(size, 0);
    if (new_path == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    struct pathreader reader;
    pathreader_init(&reader, path);
    char *dest = new_path;
    while (1) {
        char const *name;
        ret = pathreader_next(&name, &reader);
        if (ret == -ENOENT) {
            ret = 0;
            break;
        }
        if (ret < 0) {
            goto out;
        }
        if (strcmp(name, ".") == 0) {
            // Do nothing
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
    heap_free(new_path);
out:
    return ret;
}

NODISCARD static int mount(struct vfs_fstype *fstype, struct ldisk *disk, char const *mountpath) {
    struct vfs_fscontext *context;
    char *newmountpath;
    int ret;
    ret = remove_rel_path(&newmountpath, mountpath);

    if (ret < 0) {
        goto fail;
    }
    ret = fstype->ops->mount(&context, disk);
    if (ret < 0) {
        goto fail;
    }
    /*
     * Since unmounting can also technically fail, we don't want any errors
     * after this point.
     */
    list_insertback(&s_mounts, &context->node, context);
    context->mountpath = newmountpath;
    context->fstype = fstype;
    goto out;
fail:
    heap_free(newmountpath);
out:
    return ret;
}

// Returns ERR_INVAL if `mountpath` is not a mount point.
NODISCARD static int findmount(struct vfs_fscontext **out, char const *mountpath) {
    int ret = 0;
    char *newmountpath = NULL;
    ret = remove_rel_path(&newmountpath, mountpath);
    if (ret < 0) {
        goto out;
    }
    assert(s_mounts.front != NULL);
    struct vfs_fscontext *fscontext = NULL;
    LIST_FOREACH(&s_mounts, mountnode) {
        struct vfs_fscontext *entry = mountnode->data;
        assert(entry);
        if (strcmp(entry->mountpath, newmountpath) == 0) {
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
    heap_free(newmountpath);
    return ret;
}

NODISCARD int vfs_mount(char const *fstype, struct ldisk *disk, char const *mountpath) {
    int ret = -ENODEV;
    if (fstype == NULL) {
        // Try all possible filesystems
        LIST_FOREACH(&s_fstypes, fstypenode) {
            ret = mount(fstypenode->data, disk, mountpath);
            if ((ret < 0) && (ret != -EINVAL)) {
                /*
                 * If it was EINVAL, that's probably wrong filesystem type. For
                 * others, abort and report the error.
                 */
                goto out;
            } else if (ret == 0) {
                break;
            } else {
                ret = 0;
            }
        }
    } else {
        // Find filesystem with given name.
        struct vfs_fstype *fstyperesult = NULL;
        LIST_FOREACH(&s_fstypes, fstypenode) {
            struct vfs_fstype *currentfstype = fstypenode->data;
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

NODISCARD int vfs_umount(char const *mountpath) {
    int ret = 0;
    struct vfs_fscontext *fscontext;
    ret = findmount(&fscontext, mountpath);
    if (ret < 0) {
        goto out;
    }
    char *contextmountpath = fscontext->mountpath;
    ret = fscontext->fstype->ops->umount(fscontext);
    if (ret < 0) {
        goto out;
    }
    heap_free(contextmountpath);
out:
    return ret;
}

void vfs_register_fs_type(struct vfs_fstype *out, char const *name, struct vfs_fstype_ops const *ops) {
    memset(out, 0, sizeof(*out));
    out->name = name;
    out->ops = ops;
    list_insertback(&s_fstypes, &out->node, out);
}

void vfs_mount_root(void) {
    co_printf("vfs: mounting the first usable filesystem...\n");
    struct list *devlist = iodev_getlist(IODEV_TYPE_LOGICAL_DISK);
    if (devlist == NULL || devlist->front == NULL) {
        co_printf("no logical disks. Mounting dummyfs as root\n");
        int ret = vfs_mount("dummyfs", NULL, "/");
        MUST_SUCCEED(ret);
        return;
    }
    LIST_FOREACH(devlist, devnode) {
        struct iodev *iodev = devnode->data;
        struct ldisk *disk = iodev->data;
        int status = vfs_mount(NULL, disk, "/");
        if (status < 0) {
            continue;
        }
        break;
    }
}

NODISCARD static int resolve_path(
    char const *path,
    void (*callback)(struct vfs_fscontext *fscontext, char const *path, void *data),
    void *data) {
    int ret = 0;
    char *newpath = NULL;

    ret = remove_rel_path(&newpath, path);
    if (ret < 0) {
        goto fail;
    }
    // There should be a rootfs at very least.
    assert(s_mounts.front != NULL);
    struct vfs_fscontext *result = NULL;
    size_t lastmatchlen = 0;
    LIST_FOREACH(&s_mounts, mountnode) {
        struct vfs_fscontext *entry = mountnode->data;
        size_t len = strlen(entry->mountpath);
        if (lastmatchlen <= len) {
            if (strncmp(entry->mountpath, path, len) == 0) {
                result = entry;
                lastmatchlen = len;
            }
        }
    }
    assert(result != NULL);
    callback(result, &newpath[lastmatchlen], data);
    goto out;
fail:
    heap_free(newpath);
out:
    return ret;
}

struct openfilecontext {
    struct fd *fdresult;
    int flags;
    int ret;
};

static void resolve_path_callback_open_file(struct vfs_fscontext *fscontext, char const *path, void *data) {
    struct openfilecontext *context = data;
    if (fscontext->fstype->ops->open == NULL) {
        context->ret = -ENOENT;
    } else {
        context->ret = fscontext->fstype->ops->open(&context->fdresult, fscontext, path, context->flags);
    }
}

NODISCARD int vfs_open_file(struct fd **out, char const *path, int flags) {
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

void vfs_close_file(struct fd *fd) {
    fd->ops->close(fd);
}

struct open_dir_context {
    DIR *dirresult;
    int ret;
};

static void resolve_path_callback_open_directory(struct vfs_fscontext *fs_context, char const *path, void *data) {
    struct open_dir_context *context = data;
    if (fs_context->fstype->ops->opendir == NULL) {
        context->ret = -ENOENT;
    } else {
        context->ret = fs_context->fstype->ops->opendir(&context->dirresult, fs_context, path);
    }
}

int vfs_opendir(DIR **out, char const *path) {
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

NODISCARD int vfs_closedir(DIR *dir) {
    if (dir == NULL) {
        return -EBADF;
    }
    if (dir->fscontext->fstype->ops->closedir == NULL) {
        return -EBADF;
    }
    return dir->fscontext->fstype->ops->closedir(dir);
}

NODISCARD int vfs_readdir(struct dirent *out, DIR *dir) {
    if (dir == NULL) {
        return -EBADF;
    }
    if (dir->fscontext->fstype->ops->readdir == NULL) {
        return -EBADF;
    }
    return dir->fscontext->fstype->ops->readdir(out, dir);
}

NODISCARD ssize_t vfs_readfile(struct fd *fd, void *buf, size_t len) {
    return fd->ops->read(fd, buf, len);
}

NODISCARD ssize_t vfs_writefile(struct fd *fd, void const *buf, size_t len) {
    return fd->ops->write(fd, buf, len);
}

NODISCARD int vfs_seekfile(struct fd *fd, off_t offset, int whence) {
    return fd->ops->seek(fd, offset, whence);
}
