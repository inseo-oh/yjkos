#include <assert.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <stdint.h>
#include <string.h>

static void toabsblockrange(size_t *firstaddr_out, ldisk_t *self, diskblkptr_t blockaddr, size_t *blockcount_inout) {
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

FAILABLE_FUNCTION ldisk_read(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t *blockcount_inout) {
FAILABLE_PROLOGUE
    size_t firstabsaddr = 0;
    toabsblockrange(&firstabsaddr, self, blockaddr, blockcount_inout);
    size_t finalblockcount = *blockcount_inout;
    *blockcount_inout = finalblockcount;
    if (finalblockcount != 0) {
        TRY(self->physdisk->ops->read(self->physdisk, buf, firstabsaddr, finalblockcount));
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION ldisk_write(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t *blockcount_inout) {
FAILABLE_PROLOGUE
    size_t firstabsaddr = 0;
    toabsblockrange(&firstabsaddr, self, blockaddr, blockcount_inout);
    size_t finalblockcount = *blockcount_inout;
    *blockcount_inout = finalblockcount;
    if (finalblockcount != 0) {
        TRY(self->physdisk->ops->write(self->physdisk, buf, firstabsaddr, finalblockcount));
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION ldisk_read_exact(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t blockcount) {
FAILABLE_PROLOGUE
    size_t newblockcount = blockcount;
    TRY(ldisk_read(self, buf, blockaddr, &newblockcount));
    if (newblockcount != blockcount) {
        THROW(ERR_INVAL);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION ldisk_write_exact(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t blockcount) {
FAILABLE_PROLOGUE
    size_t newblockcount = blockcount;
    TRY(ldisk_write(self, buf, blockaddr, &newblockcount));
    if (newblockcount != blockcount) {
        THROW(ERR_INVAL);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION register_ldisk(pdisk_t *pdisk, diskblkptr_t startblockaddr, size_t blockcount) {
FAILABLE_PROLOGUE
    ldisk_t *disk = heap_alloc(sizeof(*disk), HEAP_FLAG_ZEROMEMORY);
    if (disk == NULL) {
        THROW(ERR_NOMEM);
    }
    disk->physdisk = pdisk;
    disk->startblockaddr = startblockaddr;
    disk->blockcount = blockcount;
    TRY(iodev_register(&disk->iodev, IODEV_TYPE_LOGICAL_DISK, disk));
    // We can't undo iodev_register as of writing this code, so no further failable action can happen.
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(disk);
    }
FAILABLE_EPILOGUE_END
}

typedef struct mbrentry mbrentry_t;
struct mbrentry {
    uint32_t startlba;
    uint32_t sectorcount;
    uint8_t partitiontype;
    uint8_t flags;
};

static void mbrentryat(mbrentry_t *out, uint8_t const *ptr) {
    out->flags = ptr[0x0];
    out->partitiontype = ptr[0x4];
    out->startlba = uint32leat(&ptr[0x8]);
    out->sectorcount = uint32leat(&ptr[0xc]);
}

static bool parsembr(pdisk_t *disk, uint8_t const *firstblock, size_t blocksize) {
    enum {
        MBR_BLOCK_SIZE = 512
    };
    // TODO: support block sizes other than 512.
    assert(MBR_BLOCK_SIZE == blocksize);
    if ((firstblock[510] != 0x55) || (firstblock[511] != 0xaa)) {
        // No valid MBR
        return false;
    }
    mbrentry_t mbrentries[4];
    mbrentryat(&mbrentries[0], &firstblock[0x1be]);
    mbrentryat(&mbrentries[1], &firstblock[0x1ce]);
    mbrentryat(&mbrentries[2], &firstblock[0x1de]);
    mbrentryat(&mbrentries[3], &firstblock[0x1ee]);
    iodev_printf(&disk->iodev, "---------- master boot record ----------\n");
    iodev_printf(&disk->iodev, "    flags  type  start     approx. size\n");
    for (size_t i = 0; i < sizeof(mbrentries)/sizeof(*mbrentries); i++) {
        if (mbrentries[i].partitiontype == 0x00) {
            continue;
        }
        iodev_printf(&disk->iodev, "[%u] %02x     %02x    %08x  %u MiB\n", i, mbrentries[i].flags, mbrentries[i].partitiontype, mbrentries[i].startlba, sizetoblocks(mbrentries[i].sectorcount, (1024 * 1024 / MBR_BLOCK_SIZE)));
    }
    iodev_printf(&disk->iodev, "----------------------------------------\n");
    for (size_t i = 0; i < sizeof(mbrentries)/sizeof(*mbrentries); i++) {
        if (mbrentries[i].partitiontype == 0x00) {
            continue;
        }
        status_t status = register_ldisk(disk, mbrentries[i].startlba, mbrentries[i].sectorcount);
        if (status != OK) {
            iodev_printf(&disk->iodev, "failed to register partition at index %u (error %d)\n", i, status);
        }
    }

    return true;
}

FAILABLE_FUNCTION pdisk_register(pdisk_t *disk_out, size_t blocksize, pdisk_ops_t const *ops, void *data) {
FAILABLE_PROLOGUE
    memset(disk_out, 0, sizeof(*disk_out));
    disk_out->ops = ops;
    disk_out->data = data;
    disk_out->blocksize = blocksize;
    TRY(iodev_register(&disk_out->iodev, IODEV_TYPE_PHYSICAL_DISK, disk_out));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

void ldisk_discover(void) {
    list_t *devlist = iodev_getlist(IODEV_TYPE_PHYSICAL_DISK);
    if (devlist == NULL || devlist->front == NULL) {
        tty_printf("ldisk: no physical disks - aborting\n");
        return;
    }
    for (list_node_t *devnode = devlist->front; devnode != NULL; devnode = devnode->next) {
        iodev_t *device = devnode->data;
        pdisk_t *disk = device->data;
        // read the first block
        uint8_t *firstblock = NULL;
        do {
            firstblock = heap_alloc(disk->blocksize, 0);
            if (firstblock == NULL) {
                iodev_printf(&disk->iodev, "not enough memory to read first block\n");
                goto partitiontablefail;
            }
            status_t status = disk->ops->read(disk, firstblock, 0, 1);
            if (status != OK) {
                iodev_printf(&disk->iodev, "failed to read first block (error %d)\n", status);
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
            iodev_printf(&disk->iodev, "no known partition table found. not reading logical disks.\n");
        }
        heap_free(firstblock);
    }
}
