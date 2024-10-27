#include <assert.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

static void toabsblockrange(
    size_t *firstaddr_out, struct ldisk *self, diskblkptr blockaddr,
    size_t *blockcount_inout)
{
    size_t diskfirstblockaddr = self->startblockaddr;
    size_t disklastblockaddr = self->startblockaddr + (self->blockcount - 1);
    size_t firstabsaddr = 0;
    size_t finalblockcount = *blockcount_inout;
    if (disklastblockaddr - diskfirstblockaddr < blockaddr) {
        finalblockcount = 0;
    } else {
        firstabsaddr = diskfirstblockaddr + blockaddr;
        if (disklastblockaddr - firstabsaddr + 1 < finalblockcount) {
            finalblockcount = disklastblockaddr - firstabsaddr + 1;
        }
    }
    *firstaddr_out = firstabsaddr;
    *blockcount_inout = finalblockcount;
}

WARN_UNUSED_RESULT ssize_t ldisk_read(
    struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount)
{
    size_t finalblockcount = blockcount;
    size_t firstabsaddr = 0;
    toabsblockrange(&firstabsaddr, self, blockaddr, &finalblockcount);
    if (finalblockcount != 0) {
        int ret = self->physdisk->ops->read(self->physdisk, buf, firstabsaddr, finalblockcount);
        if (ret < 0) {
            return ret;
        }
    }
    return finalblockcount;
}

WARN_UNUSED_RESULT ssize_t ldisk_write(
    struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount)
{
    size_t firstabsaddr = 0;
    size_t finalblockcount = blockcount;
    toabsblockrange(&firstabsaddr, self, blockaddr, &finalblockcount);
    if (finalblockcount != 0) {
        int ret = self->physdisk->ops->write(self->physdisk, buf, firstabsaddr, finalblockcount);
        if (ret < 0) {
            return ret;
        }
    }
    return finalblockcount;
}

WARN_UNUSED_RESULT int ldisk_read_exact(struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount) {
    ssize_t ret = ldisk_read(self, buf, blockaddr, blockcount);
    if (ret < 0) {
        return ret;
    }
    if ((size_t)ret != blockcount) {
        return -EINVAL;
    }
    return 0;
}

WARN_UNUSED_RESULT int ldisk_write_exact(struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount) {
    ssize_t ret = ldisk_write(self, buf, blockaddr, blockcount);
    if ((size_t)ret != blockcount) {
        return -EINVAL;
    }
    return 0;
}

static int register_ldisk(
    struct pdisk *pdisk, diskblkptr startblockaddr, size_t blockcount)
{
    int result;
    struct ldisk *disk = heap_alloc(sizeof(*disk), HEAP_FLAG_ZEROMEMORY);
    if (disk == NULL) {
        result = -ENOMEM;
        goto fail;
    }
    disk->physdisk = pdisk;
    disk->startblockaddr = startblockaddr;
    disk->blockcount = blockcount;
    result = iodev_register(&disk->iodev, IODEV_TYPE_LOGICAL_DISK, disk);
    if (result < 0) {
        goto fail;
    }
    /* 
     * We can't undo iodev_register as of writing this code, so no further
     * errors are allowed.
     */
     goto out;
fail:
    heap_free(disk);
out:
    return result;
}

struct mbrentry {
    uint32_t startlba;
    uint32_t sectorcount;
    uint8_t partitiontype;
    uint8_t flags;
};

static void mbrentryat(struct mbrentry *out, uint8_t const *ptr) {
    out->flags = ptr[0x0];
    out->partitiontype = ptr[0x4];
    out->startlba = uint32leat(&ptr[0x8]);
    out->sectorcount = uint32leat(&ptr[0xc]);
}

static bool parsembr(struct pdisk *disk, uint8_t const *firstblock, size_t blocksize) {
    enum {
        MBR_BLOCK_SIZE = 512
    };
    // TODO: support block sizes other than 512.
    assert(MBR_BLOCK_SIZE == blocksize);
    if ((firstblock[510] != 0x55) || (firstblock[511] != 0xaa)) {
        // No valid MBR
        return false;
    }
    struct mbrentry mbrentries[4];
    mbrentryat(&mbrentries[0], &firstblock[0x1be]);
    mbrentryat(&mbrentries[1], &firstblock[0x1ce]);
    mbrentryat(&mbrentries[2], &firstblock[0x1de]);
    mbrentryat(&mbrentries[3], &firstblock[0x1ee]);
    iodev_printf(
        &disk->iodev,
        "---------- master boot record ----------\n");
    iodev_printf(
        &disk->iodev,
        "    flags  type  start     approx. size\n");
    for (size_t i = 0; i < sizeof(mbrentries)/sizeof(*mbrentries); i++) {
        if (mbrentries[i].partitiontype == 0x00) {
            continue;
        }
        iodev_printf(
            &disk->iodev,
            "[%u] %02x     %02x    %08x  %u MiB\n",
            i, mbrentries[i].flags, mbrentries[i].partitiontype,
            mbrentries[i].startlba,
            sizetoblocks(mbrentries[i].sectorcount,
                (1024 * 1024 / MBR_BLOCK_SIZE)));
    }
    iodev_printf(&disk->iodev, "----------------------------------------\n");
    for (size_t i = 0; i < sizeof(mbrentries)/sizeof(*mbrentries); i++) {
        if (mbrentries[i].partitiontype == 0x00) {
            continue;
        }
        int ret = register_ldisk(disk, mbrentries[i].startlba, mbrentries[i].sectorcount);
        if (ret < 0) {
            iodev_printf(
                &disk->iodev,
                "failed to register partition at index %u (error %d)\n",
                i, ret);
        }
    }

    return true;
}

WARN_UNUSED_RESULT int pdisk_register(struct pdisk *disk_out, size_t blocksize, struct pdisk_ops const *ops, void *data) {
    memset(disk_out, 0, sizeof(*disk_out));
    disk_out->ops = ops;
    disk_out->data = data;
    disk_out->blocksize = blocksize;
    return iodev_register(
        &disk_out->iodev, IODEV_TYPE_PHYSICAL_DISK, disk_out);
}

void ldisk_discover(void) {
    struct list *devlist = iodev_getlist(IODEV_TYPE_PHYSICAL_DISK);
    if (devlist == NULL || devlist->front == NULL) {
        co_printf("ldisk: no physical disks - aborting\n");
        return;
    }
    LIST_FOREACH(devlist, devnode) {
        struct iodev *device = devnode->data;
        struct pdisk *disk = device->data;
        uint8_t *firstblock = NULL;
        do {
            firstblock = heap_alloc(disk->blocksize, 0);
            if (firstblock == NULL) {
                iodev_printf(&disk->iodev, "not enough memory to read first block\n");
                goto partitiontablefail;
            }
            int ret = disk->ops->read(disk, firstblock, 0, 1);
            if (ret < 0) {
                iodev_printf(
                    &disk->iodev,
                    "failed to read first block (error %d)\n", ret);
                goto partitiontablefail;
            }
            break;
        partitiontablefail:
            heap_free(firstblock);
            firstblock = NULL;
        } while(0);

        // Try to read MBR from it
        if ((firstblock != NULL) && parsembr(disk, firstblock, disk->blocksize)) {
            iodev_printf(&disk->iodev, "MBR loaded\n");
        } else {
            iodev_printf(
                &disk->iodev,
                "no known partition table found.\n");
        }
        heap_free(firstblock);
    }
}
