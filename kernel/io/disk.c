#include <assert.h>
#include <errno.h>
#include <kernel/io/co.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

static void to_abs_block_range(size_t *firstaddr_out, struct LDisk *self, DISK_BLOCK_ADDR block_addr, size_t *blockcount_inout) {
    size_t disk_first_blockaddr = self->startblockaddr;
    size_t disk_last_blockaddr = self->startblockaddr + (self->block_count - 1);
    size_t first_abs_addr = 0;
    size_t final_block_count = *blockcount_inout;
    if (disk_last_blockaddr - disk_first_blockaddr < block_addr) {
        final_block_count = 0;
    } else {
        first_abs_addr = disk_first_blockaddr + block_addr;
        if (disk_last_blockaddr - first_abs_addr + 1 < final_block_count) {
            final_block_count = disk_last_blockaddr - first_abs_addr + 1;
        }
    }
    *firstaddr_out = first_abs_addr;
    *blockcount_inout = final_block_count;
}

[[nodiscard]] ssize_t Ldisk_Read(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count) {
    size_t final_blockcount = block_count;
    size_t first_abs_addr = 0;
    to_abs_block_range(&first_abs_addr, self, block_addr, &final_blockcount);
    if (final_blockcount != 0) {
        int ret = self->physdisk->ops->read(self->physdisk, buf, first_abs_addr, final_blockcount);
        if (ret < 0) {
            return ret;
        }
    }
    return (ssize_t)final_blockcount;
}

[[nodiscard]] ssize_t LDisk_Write(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count) {
    size_t firstabsaddr = 0;
    size_t final_block_count = block_count;
    to_abs_block_range(&firstabsaddr, self, block_addr, &final_block_count);
    if (final_block_count != 0) {
        int ret = self->physdisk->ops->write(self->physdisk, buf, firstabsaddr, final_block_count);
        if (ret < 0) {
            return ret;
        }
    }
    return (ssize_t)final_block_count;
}

[[nodiscard]] int Ldisk_ReadExact(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count) {
    ssize_t ret = Ldisk_Read(self, buf, block_addr, block_count);
    if (ret < 0) {
        return ret;
    }
    if ((size_t)ret != block_count) {
        return -EINVAL;
    }
    return 0;
}

[[nodiscard]] int Ldisk_WriteExact(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count) {
    ssize_t ret = LDisk_Write(self, buf, block_addr, block_count);
    if ((size_t)ret != block_count) {
        return -EINVAL;
    }
    return 0;
}

static int register_ldisk(struct PDisk *pdisk, DISK_BLOCK_ADDR startblockaddr, size_t block_count) {
    int result;
    struct LDisk *disk = Heap_Alloc(sizeof(*disk), HEAP_FLAG_ZEROMEMORY);
    if (disk == NULL) {
        result = -ENOMEM;
        goto fail;
    }
    disk->physdisk = pdisk;
    disk->startblockaddr = startblockaddr;
    disk->block_count = block_count;
    result = Iodev_Register(&disk->iodev, IODEV_TYPE_LOGICAL_DISK, disk);
    if (result < 0) {
        goto fail;
    }
    /* We can't undo iodev_register as of writing this code, so no further errors are allowed. */
    goto out;
fail:
    Heap_Free(disk);
out:
    return result;
}

struct mbr_entry {
    uint32_t startlba;
    uint32_t sectorcount;
    uint8_t partitiontype;
    uint8_t flags;
};

static void mbr_entry_at(struct mbr_entry *out, uint8_t const *ptr) {
    out->flags = ptr[0x0];
    out->partitiontype = ptr[0x4];
    out->startlba = Uint32LeAt(&ptr[0x8]);
    out->sectorcount = Uint32LeAt(&ptr[0xc]);
}

static bool parse_mbr(struct PDisk *disk, uint8_t const *first_block, size_t block_size) {
    enum {
        MBR_BLOCK_SIZE = 512
    };
    /* TODO: support block sizes other than 512. */
    assert(MBR_BLOCK_SIZE == block_size);
    if ((first_block[510] != 0x55) || (first_block[511] != 0xaa)) {
        /* No valid MBR */
        return false;
    }
    struct mbr_entry mbrentries[4];
    mbr_entry_at(&mbrentries[0], &first_block[0x1be]);
    mbr_entry_at(&mbrentries[1], &first_block[0x1ce]);
    mbr_entry_at(&mbrentries[2], &first_block[0x1de]);
    mbr_entry_at(&mbrentries[3], &first_block[0x1ee]);
    Iodev_Printf(&disk->iodev, "---------- master boot record ----------\n");
    Iodev_Printf(&disk->iodev, "    flags  type  start     approx. size\n");
    for (size_t i = 0; i < sizeof(mbrentries) / sizeof(*mbrentries); i++) {
        if (mbrentries[i].partitiontype == 0x00) {
            continue;
        }
        Iodev_Printf(&disk->iodev, "[%u] %02x     %02x    %08x  %u MiB\n", i, mbrentries[i].flags, mbrentries[i].partitiontype, mbrentries[i].startlba, SizeToBlocks(mbrentries[i].sectorcount, (1024 * 1024 / MBR_BLOCK_SIZE)));
    }
    Iodev_Printf(&disk->iodev, "----------------------------------------\n");
    for (size_t i = 0; i < sizeof(mbrentries) / sizeof(*mbrentries); i++) {
        if (mbrentries[i].partitiontype == 0x00) {
            continue;
        }
        int ret = register_ldisk(disk, mbrentries[i].startlba, mbrentries[i].sectorcount);
        if (ret < 0) {
            Iodev_Printf(&disk->iodev, "failed to register partition at index %u (error %d)\n", i, ret);
        }
    }

    return true;
}

[[nodiscard]] int Pdisk_Register(struct PDisk *disk_out, size_t blocksize, struct PdiskOps const *ops, void *data) {
    memset(disk_out, 0, sizeof(*disk_out));
    disk_out->ops = ops;
    disk_out->data = data;
    disk_out->block_size = blocksize;
    return Iodev_Register(&disk_out->iodev, IODEV_TYPE_PHYSICAL_DISK, disk_out);
}

void Ldisk_Discover(void) {
    struct List *devlist = Iodev_GetList(IODEV_TYPE_PHYSICAL_DISK);
    if (devlist == NULL || devlist->front == NULL) {
        Co_Printf("ldisk: no physical disks - aborting\n");
        return;
    }
    LIST_FOREACH(devlist, devnode) {
        struct IoDev *device = devnode->data;
        struct PDisk *disk = device->data;
        uint8_t *first_block = NULL;
        do {
            first_block = Heap_Alloc(disk->block_size, 0);
            if (first_block == NULL) {
                Iodev_Printf(&disk->iodev, "not enough memory to read first block\n");
                goto partition_table_fail;
            }
            int ret = disk->ops->read(disk, first_block, 0, 1);
            if (ret < 0) {
                Iodev_Printf(&disk->iodev, "failed to read first block (error %d)\n", ret);
                goto partition_table_fail;
            }
            break;
        partition_table_fail:
            Heap_Free(first_block);
            first_block = NULL;
        } while (0);

        /* Try to read MBR from it ********************************************/
        if ((first_block != NULL) && parse_mbr(disk, first_block, disk->block_size)) {
            Iodev_Printf(&disk->iodev, "MBR loaded\n");
        } else {
            Iodev_Printf(&disk->iodev, "no known partition table found.\n");
        }
        Heap_Free(first_block);
    }
}
