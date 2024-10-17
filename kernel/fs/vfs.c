#include <assert.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
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

static _Atomic int s_nextfdnum = 0;

WARN_UNUSED_RESULT int vfs_registerfile(
    struct fd *out, struct fd_ops const *ops, struct vfs_fscontext *fscontext, void *data)
{
    out->id = s_nextfdnum++;
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
static WARN_UNUSED_RESULT int removerelpath(
    char **newpath_out, char const *path)
{
    int ret = 0;
    char *newpath = NULL;
    size_t size = strlen(path) + 2; // Leave room for / and NULL terminator.
    if (size == 0) {
        ret = -ENOMEM;
        goto fail;
    }
    newpath = heap_alloc(size, 0);
    if (newpath == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    char namebuf[NAME_MAX + 1];
    char const *remainingpath = path;
    char *dest = newpath;
    while(*remainingpath != '\0') {
        char *nextslash = strchr(remainingpath, '/');
        char const *name;
        char const *newremainingpath;
        size_t namelen;
        if (nextslash == NULL) {
            name = remainingpath;
            newremainingpath = strchr(path, '\0');
            namelen = newremainingpath - name;
        } else {
            namelen = nextslash - remainingpath;
            if (NAME_MAX < namelen) {
                ret = -ENAMETOOLONG;
                goto fail;
            }
            memcpy(namebuf, remainingpath, namelen);
            namebuf[namelen] = '\0';
            name = namebuf;
            newremainingpath = &nextslash[1];
        }
        if (name[0] != '\0') {
            do {
                if (strcmp(name, ".") == 0) {
                    break;
                } else if (strcmp(name, "..") == 0) {
                    char *foundpos = strrchr(newpath, '/');
                    if (foundpos == NULL) {
                        dest = newpath;
                    } else {
                        dest = foundpos;
                    }
                    *dest = '\0';
                } else {
                    *dest = '/';
                    dest++;
                    memcpy(dest, name, namelen);
                    dest += namelen;
                }
            } while(0);
        }
        remainingpath = newremainingpath;
    }
    *dest = '\0';
    *newpath_out = newpath;
    goto out;
fail:
    heap_free(newpath);
out:
    return ret;
}

static WARN_UNUSED_RESULT int mount(
    struct vfs_fstype *fstype, struct ldisk *disk, char const *mountpath)
{
    struct vfs_fscontext *context;
    char *newmountpath;
    int ret;
    ret = removerelpath(&newmountpath, mountpath);

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
static WARN_UNUSED_RESULT int findmount(
    struct vfs_fscontext **out, char const *mountpath)
{
    int ret = 0;
    char *newmountpath = NULL;
    ret = removerelpath(&newmountpath, mountpath);
    if (ret < 0) {
        goto out;
    }
    assert(s_mounts.front != NULL);
    struct vfs_fscontext *fscontext = NULL;
    for (struct list_node *mountnode = s_mounts.front; mountnode != NULL; mountnode = mountnode->next) {
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

WARN_UNUSED_RESULT int vfs_mount(
    char const *fstype, struct ldisk *disk, char const *mountpath)
{
    int ret;
    if (fstype == NULL) {
        // Try all possible filesystems
        for (struct list_node *fstypenode = s_fstypes.front; fstypenode != NULL; fstypenode = fstypenode->next) {
            ret = mount(fstypenode->data, disk, mountpath);
            if ((ret < 0) && (ret != -EINVAL)) {
                /* 
                 * If it was EINVAL, that's probably wrong filesystem type. For
                 * others, abort and report the error.
                 */
                goto out;
            } else if(ret == 0) {
                break;
            } else {
                ret = 0;
            }
        }
    } else {
        // Find filesystem with given name.
        struct vfs_fstype *fstyperesult = NULL;
        for (struct list_node *fstypenode = s_fstypes.front; fstypenode != NULL; fstypenode = fstypenode->next) {
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

WARN_UNUSED_RESULT int vfs_umount(char const *mountpath) {
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

void vfs_registerfstype(struct vfs_fstype *out, char const *name, struct vfs_fstype_ops const *ops) {
    memset(out, 0, sizeof(*out));
    out->name = name;
    out->ops = ops;
    list_insertback(&s_fstypes, &out->node, out);
}

void vfs_mountroot(void) {
    tty_printf("vfs: mounting the first usable filesystem...\n");
    struct list *devlist = iodev_getlist(IODEV_TYPE_LOGICAL_DISK);
    if (devlist == NULL || devlist->front == NULL) {
        tty_printf("no logical disks. Mounting dummyfs as root\n");
        int ret = vfs_mount("dummyfs", NULL, "/");
        MUST_SUCCEED(ret);
        return;
    }
    for (struct list_node *devnode = devlist->front; devnode != NULL; devnode = devnode->next) {
        struct iodev *iodev = devnode->data;
        struct ldisk *disk = iodev->data;
        int status = vfs_mount(NULL, disk, "/");
        if (status < 0) {
            continue;
        }
        break;
    }
}

static WARN_UNUSED_RESULT int resolvepath(
    char const *path,
    void (*callback)(struct vfs_fscontext *fscontext, char const *path,
        void *data),
    void *data)
{
    int ret = 0;
    char *newpath = NULL;

    ret = removerelpath(&newpath, path);
    if (ret < 0) {
        goto fail;
    }
    // There should be a rootfs at very least.
    assert(s_mounts.front != NULL);
    struct vfs_fscontext *result = NULL;
    size_t lastmatchlen = 0;
    for (
        struct list_node *mountnode = s_mounts.front; mountnode != NULL; mountnode = mountnode->next)
    {
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

static void resolvepathcallback_openfile(
    struct vfs_fscontext *fscontext, char const *path, void *data)
{
    struct openfilecontext *context = data;
    if (fscontext->fstype->ops->open == NULL) {
        context->ret = -ENOENT;
    } else {
        context->ret = fscontext->fstype->ops->open(
            &context->fdresult, fscontext, path, context->flags);
    }
}

WARN_UNUSED_RESULT int vfs_openfile(
    struct fd **out, char const *path, int flags)
{
    struct openfilecontext context;
    context.flags = flags;
    int ret = resolvepath(
        path, resolvepathcallback_openfile, &context);
    if (ret < 0) {
        return ret;
    } else if (context.ret < 0) {
        return context.ret;
    }
    *out = context.fdresult;
    return 0;
}

void vfs_closefile(struct fd *fd) {
    fd->ops->close(fd);
}

struct opendircontext {
    DIR *dirresult;
    int ret;
};

static void resolvepathcallback_opendirectory(
    struct vfs_fscontext *fscontext, char const *path, void *data)
{
    struct opendircontext *context = data;
    if (fscontext->fstype->ops->opendir == NULL) {
        context->ret = -ENOENT;
    } else {
        context->ret = fscontext->fstype->ops->opendir(
            &context->dirresult, fscontext, path);
    }
}

int vfs_opendir(DIR **out, char const *path) {
    struct opendircontext context;
    int ret = resolvepath(
        path, resolvepathcallback_opendirectory, &context);
    if (ret < 0) {
        return ret;
    } else if (context.ret < 0) {
        return context.ret;
    }
    *out = context.dirresult;
    return 0;
}

WARN_UNUSED_RESULT int vfs_closedir(DIR *dir) {
    if (dir == NULL) {
        return -EBADF;
    }
    if (dir->fscontext->fstype->ops->closedir == NULL) {
        return -EBADF;
    }
    return dir->fscontext->fstype->ops->closedir(dir);
}

WARN_UNUSED_RESULT int vfs_readdir(struct dirent *out, DIR *dir) {
    if (dir == NULL) {
        return -EBADF;
    }
    if (dir->fscontext->fstype->ops->readdir == NULL) {
        return -EBADF;
    }
    return dir->fscontext->fstype->ops->readdir(out, dir);
}

WARN_UNUSED_RESULT ssize_t vfs_readfile(struct fd *fd, void *buf, size_t len) {
    return fd->ops->read(fd, buf, len);
}

WARN_UNUSED_RESULT ssize_t vfs_writefile(
    struct fd *fd, void const *buf, size_t len)
{
    return fd->ops->write(fd, buf, len);
}

WARN_UNUSED_RESULT int vfs_seekfile(struct fd *fd, off_t offset, int whence) {
    return fd->ops->seek(fd, offset, whence);
}

