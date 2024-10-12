#include <assert.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/tty.h>
#include <kernel/io/vfs.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <kernel/status.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

////////////////////////////////////////////////////////////////////////////////

// File descriptor management
// XXX: VFS is temporary home for file descriptor management for now. This should go to
//      individual process once we have those implemented. 
//      This works for now, as we have only one process: Kernel

static _Atomic int s_nextfdnum = 0;

FAILABLE_FUNCTION vfs_registerfile(struct fd *out, struct fd_ops const *ops, struct vfs_fscontext *fscontext, void *data) {
FAILABLE_PROLOGUE
    out->id = s_nextfdnum++;
    if (out->id == INT_MAX) {
        // though it's more likely that UBSan catched signed integer overflow before we did.
        panic("vfs: TODO: Handle s_nextfdnum integer overflow");
    }
    out->ops = ops;
    out->data = data;
    out->fscontext = fscontext;
    fscontext->openfilecount++;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
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
static FAILABLE_FUNCTION removerelpath(char **newpath_out, char const *path) {
FAILABLE_PROLOGUE
    char *newpath = NULL;
    size_t size = strlen(path) + 1;
    if (size == 0) {
        THROW(ERR_NOMEM);
    }
    newpath = heap_alloc(size, 0);
    if (newpath == NULL) {
        THROW(ERR_NOMEM);
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
                THROW(ERR_NAMETOOLONG);
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
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(newpath);
    }
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION mount(struct vfs_fstype *fstype, struct ldisk *disk, char const *mountpath) {
FAILABLE_PROLOGUE
    struct vfs_fscontext *context;
    char *newmountpath;
    TRY(removerelpath(&newmountpath, mountpath));
    TRY(fstype->ops->mount(&context, disk));
    // We don't want any failable action to happen after mount, because unmounting can also technically fail.
    list_insertback(&s_mounts, &context->node, context);
    context->mountpath = newmountpath;
    context->fstype = fstype;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(newmountpath);
    }
FAILABLE_EPILOGUE_END
}

// Returns ERR_INVAL if `mountpath` is not a mount point.
static FAILABLE_FUNCTION findmount(struct vfs_fscontext **out, char const *mountpath) {
FAILABLE_PROLOGUE
    char *newmountpath = NULL;
    TRY(removerelpath(&newmountpath, mountpath));
    assert(s_mounts.front != NULL);
    struct vfs_fscontext *result = NULL;
    for (struct list_node *mountnode = s_mounts.front; mountnode != NULL; mountnode = mountnode->next) {
        struct vfs_fscontext *entry = mountnode->data;
        assert(entry);
        if (strcmp(entry->mountpath, newmountpath) == 0) {
            result = entry;
            break;
        }
    }
    if (result == NULL) {
        THROW(ERR_INVAL);
    }
    *out = result;
FAILABLE_EPILOGUE_BEGIN
    heap_free(newmountpath);
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION vfs_mount(char const *fstype, struct ldisk *disk, char const *mountpath) {
FAILABLE_PROLOGUE
    if (fstype == NULL) {
        // Try all possible filesystems
        for (struct list_node *fstypenode = s_fstypes.front; fstypenode != NULL; fstypenode = fstypenode->next) {
            status_t mountstatus = mount(fstypenode->data, disk, mountpath);
            if ((mountstatus != OK) && (mountstatus != ERR_INVAL)) {
                // If it was ERR_INVAL, that's probably wrong filesystem type. For others, abort and report the error.
                THROW(mountstatus);
            } else if(mountstatus == OK) {
                break;
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
            THROW(ERR_NODEV);
        }
        TRY(mount(fstyperesult, disk, mountpath));
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION vfs_umount(char const *mountpath) {
FAILABLE_PROLOGUE
    struct vfs_fscontext *fscontext;
    TRY(findmount(&fscontext, mountpath));
    char *contextmountpath = fscontext->mountpath;
    TRY(fscontext->fstype->ops->umount(fscontext));
    heap_free(contextmountpath);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
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
        status_t status = vfs_mount("dummyfs", NULL, "/");
        if (status != OK) {
            tty_printf("can't even mount dummyfs (Error %d)\n", status);
        }
        return;
    }
    for (struct list_node *devnode = devlist->front; devnode != NULL; devnode = devnode->next) {
        struct iodev *iodev = devnode->data;
        struct ldisk *disk = iodev->data;
        status_t status = vfs_mount(NULL, disk, "/");
        if (status != OK) {
            continue;
        }
        break;
    }
}

static FAILABLE_FUNCTION resolvepath(struct vfs_fscontext **out, char const *path, void (*callback)(struct vfs_fscontext *fscontext, char const *path, void *data), void *data) {
FAILABLE_PROLOGUE
    char *newpath = NULL;
    TRY(removerelpath(&newpath, path));
    // There should be a rootfs at very least.
    assert(s_mounts.front != NULL);
    struct vfs_fscontext *result = NULL;
    size_t lastmatchlen = 0;
    for (struct list_node *mountnode = s_mounts.front; mountnode != NULL; mountnode = mountnode->next) {
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
    *out = result;
FAILABLE_EPILOGUE_BEGIN
    heap_free(newpath);
FAILABLE_EPILOGUE_END
}

struct openfilecontext {
    struct fd *fdresult;
    int flags;
    status_t status;
};

static void resolvepathcallback_openfile(struct vfs_fscontext *fscontext, char const *path, void *data) {
    struct openfilecontext *context = data;
    context->status = fscontext->fstype->ops->open(&context->fdresult, fscontext, path, context->flags);
}

FAILABLE_FUNCTION vfs_openfile(struct fd **out, char const *path, int flags) {
FAILABLE_PROLOGUE
    struct vfs_fscontext *fscontext;
    struct openfilecontext context;
    context.flags = flags;
    TRY(resolvepath(&fscontext, path, resolvepathcallback_openfile, &context));
    if (context.status != OK) {
        THROW(context.status);
    }
    *out = context.fdresult;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

void vfs_closefile(struct fd *fd) {
    fd->ops->close(fd);
}

FAILABLE_FUNCTION vfs_readfile(struct fd *fd, void *buf, size_t *len_inout) {
    return fd->ops->read(fd, buf, len_inout);
}

FAILABLE_FUNCTION vfs_writefile(struct fd *fd, void const *buf, size_t *len_inout) {
    return fd->ops->write(fd, buf, len_inout);
}

FAILABLE_FUNCTION vfs_seekfile(struct fd *fd, off_t offset, int whence) {
    return fd->ops->seek(fd, offset, whence);
}

