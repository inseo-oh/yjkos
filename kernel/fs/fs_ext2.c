#include "fsinit.h"
#include <kernel/fs/vfs.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pathreader.h>
#include <kernel/mem/heap.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define EXT2_SIGNATURE 0xef53

#define FSSTATE_CLEAN 1
#define FSSTATE_ERROR 2

#define ERRACTION_IGNORE 1
#define ERRACTION_REMOUNT_RO 2
#define ERRACTION_PANIC 3

#define INODE_ROOTDIRECTORY 2

#define REQUIRED_FEATUREFLAG_COMPRESSION (1U << 0U)
#define REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPE_FIELD (1U << 1U)
#define REQUIRED_FEATUREFLAG_NEED_REPLAY_JOURNAL (1U << 2U)
#define REQUIRED_FEATUREFLAG_JOURNAL_DEVICE_USED (1U << 3U)

/* Sparse superblocks and group descriptor tables */
#define RWMOUNT_FEATUREFLAG_SPARSE_SUPERBLOCK_AND_GDTABLE (1U << 0U)
#define RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE (1U << 1U)
/* Directory contents are stored in the form of a Binary Tree */
#define RWMOUNT_FEATUREFLAG_BINARY_TREE_DIR (1U << 2U)

#define SUPPORTED_REQUIRED_FLAGS \
    REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPE_FIELD

#define SUPPORTED_RWMOUNT_FLAGS                          \
    (RWMOUNT_FEATUREFLAG_SPARSE_SUPERBLOCK_AND_GDTABLE | \
     RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE)

struct fscontext {
    /***************************************************************************
     * Superblock
     **************************************************************************/
    uint32_t superblock_block_num;
    size_t total_inodes;
    blkcnt_t total_blocks;
    blkcnt_t total_unallocated_blocks;
    size_t total_unallocated_inodes;
    blkcnt_t reserved_blocks_for_su;
    blksize_t blocksize;
    blkcnt_t blocks_in_block_group;
    size_t inodes_in_block_group;
    time_t last_mount_time;
    time_t last_written_time;
    uint16_t mounts_since_last_fsck;
    uint16_t mounts_before_fsck_required;
    uint16_t signature;
    uint16_t fs_state;   /* See FSSTATE_~ values */
    uint16_t err_action; /* See ERRACTION_~ values */
    uint16_t minor_ver;
    time_t last_fsck_time;
    time_t fsck_interval;
    uint32_t creator_os_id;
    uint32_t major_ver;
    uid_t reserved_block_uid;
    gid_t reserved_block_gid;

    /* Below are superblock fields for 1.0 <= Version *************************/
    uint32_t block_group;           /* If it's a backup copy */
    ino_t first_non_reserved_inode; /* Pre-1.0: 11 */
    size_t inode_size;              /* Pre-1.0: 128 */
    uint32_t optional_features;
    uint32_t required_features;     /* Required features for both R/W and R/O mount */
    uint32_t required_features_rw;  /* Required features for R/W mount */
    uint32_t compressionalgorithms; /* If compression is used */
    uint8_t preallocatefileblks;
    uint8_t preallocatedirblks;
    uint32_t journalinode;
    uint32_t journaldevice;
    uint32_t orphaninodelisthead;
    uint8_t filesystem_id[16]; /* 16-byte UUID */
    uint8_t journalid[16];     /* 16-byte UUID */
    char volumename[16];
    char last_mount_path[64];

    /***************************************************************************
     * Other fields needed for FS management
     **************************************************************************/
    struct LDisk *disk;
    size_t blk_group_count;
    size_t blk_group_descriptor_blk;
    struct Vfs_FsContext vfs_fscontext;
};

struct block_group_descriptor {
    uint32_t blkusagebitmap;
    uint32_t inodeusagebitmap;
    uint32_t inodetable;
    blkcnt_t unallocatedblocks;
    size_t unallocatedinodes;
    size_t directories;
    uint8_t unused[14];
};

struct ino_context {
    off_t size;
    size_t hardlinks;
    size_t disksectors;
    uint32_t direct_block_ptrs[12];
    uint32_t singly_indirect_table;
    uint32_t doublyindirecttable;
    uint32_t triplyindirecttable;

    uint32_t lastaccesstime;
    uint32_t creationtime;
    uint32_t lastmodifiedtime;
    uint32_t deletiontime;
    uint32_t flags;
    uint32_t generationnumber;
    uint16_t typeandpermissions;
    uint16_t uid;
    uint16_t gid;

    struct fscontext *fs;
    uint32_t current_block_addr;
    size_t next_direct_ptr_index;
    size_t cnt;

    struct {
        off_t offset_in_buf;
        uint8_t *buf;
    } singly_indirect_buf, doubly_indirect_buf, triply_indirect_buf, blockbuf;
    bool singly_indirect_used : 1;
    bool doubly_indirect_used : 1;
    bool triply_indirect_used : 1;
};

/* Bitmask values for type and permissions */
#define INODE_TYPE_MASK 0xf000
#define INODE_TYPE_FIFO 0x1000
#define INODE_TYPE_CHARACTER 0x2000
#define INODE_TYPE_DIRECTORY 0x4000
#define INODE_TYPE_BLOCK_DEVICE 0x6000
#define INODE_TYPE_REGULAR_FILE 0x8000
#define INODE_TYPE_SYMBOLIC_LINK 0xa000
#define INODE_TYPE_UNIX_SOCKET 0xc000

/* `buf` must be able to hold `blkcount * self->blocksize` bytes. */
[[nodiscard]] static int read_blocks(struct fscontext *self, void *buf, uint32_t block_addr, blkcnt_t blkcount) {
    int ret = 0;
    /* TODO: support cases where self->blocksize < self->disk->physdisk->blocksize */
    assert((self->blocksize % self->disk->physdisk->block_size) == 0);
    DISK_BLOCK_ADDR diskblockaddr = block_addr * (self->blocksize / self->disk->physdisk->block_size);
    blkcnt_t diskblkcount = blkcount * (self->blocksize / self->disk->physdisk->block_size);
    ret = (Ldisk_ReadExact(self->disk, buf, diskblockaddr, diskblkcount));
    if (ret < 0) {
        goto out;
    }
    goto out;
out:
    return ret;
}

/* Returns NULL when there's not enough memory. */
[[nodiscard]] static uint8_t *alloc_block_buf(struct fscontext *self, blkcnt_t count, uint8_t flags) {
    uint8_t *buf = Heap_Calloc(count, self->blocksize, flags);
    if (buf == NULL) {
        return NULL;
    }
    return buf;
}

[[nodiscard]] static int readblocks_alloc(uint8_t **out, struct fscontext *self, uint32_t block_addr, blkcnt_t block_count) {
    int ret;
    uint8_t *buf = alloc_block_buf(self, block_count, 0);
    if (buf == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    ret = read_blocks(self, buf, block_addr, block_count);
    if (ret < 0) {
        goto fail;
    }
    *out = buf;
    goto out;
fail:
    Heap_Free(buf);
out:
    return ret;
}

[[nodiscard]] static int read_block_group_descriptor(struct block_group_descriptor *out, struct fscontext *self, uint32_t block_group) {
    int ret;
    enum {
        DESCRIPTOR_SIZE = 32
    };
    assert(block_group < (SIZE_MAX / DESCRIPTOR_SIZE));
    off_t byteoffset = (off_t)block_group * DESCRIPTOR_SIZE;
    uint32_t blockoffset = byteoffset / self->blocksize;
    off_t byteoffsetinblk = byteoffset % self->blocksize;
    assert(blockoffset < (SIZE_MAX - self->blk_group_descriptor_blk));
    blockoffset += self->blk_group_descriptor_blk;

    uint8_t *buf = NULL;
    ret = readblocks_alloc(&buf, self, blockoffset, 1);
    if (ret < 0) {
        goto fail;
    }
    uint8_t *data = &buf[byteoffsetinblk];
    out->blkusagebitmap = Uint32LeAt(&data[0x00]);
    out->inodeusagebitmap = Uint32LeAt(&data[0x04]);
    out->inodetable = Uint32LeAt(&data[0x08]);
    out->unallocatedblocks = Uint16LeAt(&data[0x0c]);
    out->unallocatedinodes = Uint16LeAt(&data[0x0e]);
    out->directories = Uint16LeAt(&data[0x10]);
    goto out;
fail:
out:
    Heap_Free(buf);
    return ret;
}

static uint32_t block_group_of_inode(struct fscontext *self, ino_t inodeaddr) {
    return (inodeaddr - 1) / self->inodes_in_block_group;
}

[[nodiscard]] static int locate_inode(uint32_t *blk_out, off_t *off_out, struct fscontext *self, ino_t inodeaddr) {
    int ret = 0;
    struct block_group_descriptor blkgroup;
    ret = read_block_group_descriptor(&blkgroup, self, block_group_of_inode(self, inodeaddr));
    if (ret < 0) {
        goto fail;
    }
    off_t index = (inodeaddr - 1) % self->inodes_in_block_group;
    assert(index < (SIZE_MAX / self->inode_size));
    *blk_out = blkgroup.inodetable +
               ((index * self->inode_size) / self->blocksize);
    *off_out = (index * self->inode_size) % self->blocksize;
    goto out;
fail:
out:
    return ret;
}

[[nodiscard]] static int next_direct_block_ptr(uint32_t *addr_out, struct ino_context *self) {
    uint32_t result_addr;
    /* We can use direct block pointer */
    result_addr = self->direct_block_ptrs[self->next_direct_ptr_index];
    if (result_addr == 0) {
        return -ENOENT;
    }
    self->next_direct_ptr_index++;
    *addr_out = result_addr;
    return 0;
}

[[nodiscard]] static int next_triply_indirect_table(struct ino_context *self) {
    uint32_t tableaddr;
    int ret = 0;
    if (!self->triply_indirect_used) {
        /* We are using triply indirect table for the first time */
        tableaddr = self->triplyindirecttable;
    } else {
        Iodev_Printf(&self->fs->disk->iodev, "File is too large\n");
        ret = -ENOENT;
        goto out;
    }
    if (tableaddr == 0) {
        Heap_Free(self->triply_indirect_buf.buf);
        self->triply_indirect_buf.buf = NULL;
        self->triply_indirect_buf.offset_in_buf = 0;
        ret = -ENOENT;
        goto out;
    }
    uint8_t *newtable = NULL;
    ret = readblocks_alloc(&newtable, self->fs, tableaddr, 1);
    if (ret < 0) {
        goto out;
    }
    Heap_Free(self->triply_indirect_buf.buf);
    self->triply_indirect_buf.buf = newtable;
    self->triply_indirect_buf.offset_in_buf = 0;
    ret = 0;
out:
    return ret;
}

[[nodiscard]] static int next_triply_block_ptr(uint32_t *addr_out, struct ino_context *self) {
    uint32_t tableaddr;
    int ret = -ENOENT;

    if ((self->triply_indirect_buf.buf == NULL) || (self->fs->blocksize <= self->triply_indirect_buf.offset_in_buf)) {
        ret = next_triply_indirect_table(self);
        if (ret < 0) {
            goto out;
        }
    }
    self->triply_indirect_used = true;
    tableaddr = Uint32LeAt(&self->triply_indirect_buf.buf[self->triply_indirect_buf.offset_in_buf]);
    self->triply_indirect_buf.offset_in_buf += sizeof(uint32_t);
    *addr_out = tableaddr;
    ret = 0;
out:
    return ret;
}

[[nodiscard]] static int next_doubly_indirect_table(struct ino_context *self) {
    int ret = -ENOENT;
    /* We need to move to next doubly indirect table */
    uint32_t tableaddr;
    if (!self->doubly_indirect_used) {
        /* We are using doubly indirect table for the first time */
        tableaddr = self->doublyindirecttable;
        if (tableaddr != 0) {
            ret = 0;
        }
        self->doubly_indirect_used = true;
    } else {
        ret = next_triply_block_ptr(&tableaddr, self);
    }
    if (ret < 0) {
        Heap_Free(self->doubly_indirect_buf.buf);
        self->doubly_indirect_buf.buf = NULL;
        self->doubly_indirect_buf.offset_in_buf = 0;
        goto out;
    }
    uint8_t *newtable = NULL;
    ret = readblocks_alloc(&newtable, self->fs, tableaddr, 1);
    if (ret < 0) {
        goto out;
    }
    Heap_Free(self->doubly_indirect_buf.buf);
    self->doubly_indirect_buf.buf = newtable;
    self->doubly_indirect_buf.offset_in_buf = 0;
    ret = 0;
out:
    return ret;
}

[[nodiscard]] static int next_doubly_block_ptr(uint32_t *addr_out, struct ino_context *self) {
    uint32_t result_addr = 0;
    int ret = -ENOENT;
    if ((self->doubly_indirect_buf.buf == NULL) || (self->fs->blocksize <= self->doubly_indirect_buf.offset_in_buf)) {
        ret = next_doubly_indirect_table(self);
        if (ret < 0) {
            goto out;
        }
    }
    result_addr = Uint32LeAt(&self->doubly_indirect_buf.buf[self->doubly_indirect_buf.offset_in_buf]);
    self->doubly_indirect_buf.offset_in_buf += sizeof(uint32_t);
    if (result_addr == 0) {
        ret = -ENOENT;
        goto out;
    }
    ret = 0;
    *addr_out = result_addr;
out:
    return ret;
}

[[nodiscard]] static int next_singly_indirect_table(struct ino_context *self) {
    int ret = -ENOENT;
    uint32_t tableaddr;
    if (!self->singly_indirect_used) {
        /* We are using singly indirect table for the first time */
        tableaddr = self->singly_indirect_table;
        if (tableaddr != 0) {
            ret = 0;
        }
    } else {
        ret = next_doubly_block_ptr(&tableaddr, self);
    }
    if (ret < 0) {
        Heap_Free(self->singly_indirect_buf.buf);
        self->singly_indirect_buf.buf = NULL;
        self->singly_indirect_buf.offset_in_buf = 0;
        ret = -ENOENT;
        goto out;
    }
    uint8_t *newtable = NULL;
    ret = readblocks_alloc(&newtable, self->fs, tableaddr, 1);
    if (ret < 0) {
        goto out;
    }
    Heap_Free(self->singly_indirect_buf.buf);
    self->singly_indirect_buf.buf = newtable;
    self->singly_indirect_buf.offset_in_buf = 0;
    self->singly_indirect_used = true;
    ret = 0;
out:
    return ret;
}

/*
 * Returns -ENOENT on EOF.
 */
[[nodiscard]] static int next_singly_block_ptr(uint32_t *addr_out, struct ino_context *self) {
    int ret = -ENOENT;
    uint32_t result_addr = 0;
    if ((self->singly_indirect_buf.buf == NULL) || (self->fs->blocksize <= self->singly_indirect_buf.offset_in_buf)) {
        ret = next_singly_indirect_table(self);
        if (ret < 0) {
            goto out;
        }
    }
    result_addr = Uint32LeAt(&self->singly_indirect_buf.buf[self->singly_indirect_buf.offset_in_buf]);
    if (result_addr == 0) {
        goto out;
    }
    self->singly_indirect_buf.offset_in_buf += sizeof(uint32_t);
    ret = 0;
    *addr_out = result_addr;
out:
    return ret;
}

/*
 * Returns -ENOENT on EOF.
 */
[[nodiscard]] static int next_inode_block(struct ino_context *self) {
    int ret = 0;
    enum {
        DIRECT_BLOCK_POINTER_COUNT = sizeof(self->direct_block_ptrs) / sizeof(*self->direct_block_ptrs)
    };
    uint32_t result_addr = 0;
    if (self->next_direct_ptr_index < DIRECT_BLOCK_POINTER_COUNT) {
        /* We can use direct block pointer */
        ret = next_direct_block_ptr(&result_addr, self);
    } else {
        ret = next_singly_block_ptr(&result_addr, self);
    }
    if (ret == 0) {
        self->current_block_addr = result_addr;
        self->cnt++;
    }
    goto out;
out:
    return ret;
}

static void rewindinode(struct ino_context *self) {
    Heap_Free(self->blockbuf.buf);
    Heap_Free(self->singly_indirect_buf.buf);
    Heap_Free(self->doubly_indirect_buf.buf);
    Heap_Free(self->triply_indirect_buf.buf);
    memset(&self->blockbuf, 0, sizeof(self->blockbuf));
    memset(&self->singly_indirect_buf, 0, sizeof(self->singly_indirect_buf));
    memset(&self->doubly_indirect_buf, 0, sizeof(self->doubly_indirect_buf));
    memset(&self->triply_indirect_buf, 0, sizeof(self->triply_indirect_buf));
    self->current_block_addr = 0;
    self->next_direct_ptr_index = 0;
    self->singly_indirect_used = false;
    self->doubly_indirect_used = false;
    self->triply_indirect_used = false;
    self->cnt = 0;
    /* Move to the very first block */
    int ret = next_inode_block(self);
    /* Above should only access the first direct block pointer, which cannot fail. */
    MUST_SUCCEED(ret);
}

[[nodiscard]] static int next_inode_block_and_reset_blockbuf(struct ino_context *self) {
    int ret = next_inode_block(self);
    if (ret < 0) {
        return ret;
    }
    /* Invalidate old buffer */
    Heap_Free(self->blockbuf.buf);
    self->blockbuf.buf = NULL;
    self->blockbuf.offset_in_buf = 0;
    return 0;
}

/*
 * Returns -ENOENT on EOF.
 */
[[nodiscard]] static int skipread_inode(struct ino_context *self, size_t len) {
    assert(len <= STREAM_MAX_TRANSFER_SIZE);
    int ret = 0;
    size_t remaining_len = len;

    while (remaining_len != 0) {
        if (self->fs->blocksize <= self->blockbuf.offset_in_buf) {
            /*
             * We've ran out of current block, so we need to move to the next
             * block
             */
            ret = next_inode_block_and_reset_blockbuf(self);
            if (ret < 0) {
                goto out;
            }
        }
        if ((self->blockbuf.offset_in_buf == 0) &&
            ((size_t)self->fs->blocksize <= remaining_len)) {
            blkcnt_t count = remaining_len / self->fs->blocksize;
            for (blkcnt_t i = 0; i < count; i++) {
                ret = next_inode_block(self);
                if (ret < 0) {
                    goto out;
                }
            }
            size_t skip_len = self->fs->blocksize * count;
            remaining_len -= skip_len;
            Heap_Free(self->blockbuf.buf);
            self->blockbuf.buf = NULL;
        }
        if (remaining_len == 0) {
            break;
        }
        assert(self->blockbuf.offset_in_buf < self->fs->blocksize);

        blkcnt_t maxlen = self->fs->blocksize - self->blockbuf.offset_in_buf;
        blkcnt_t skiplen = remaining_len;
        if (maxlen < skiplen) {
            skiplen = maxlen;
        }
        assert(skiplen != 0);
        self->blockbuf.offset_in_buf += skiplen;
        remaining_len -= skiplen;
    }
    goto out;
out:
    return ret;
}

[[nodiscard]] static int seek_inode(struct ino_context *self, off_t offset, int whence) {
    int ret = 0;
    switch (whence) {
    case SEEK_SET: {
        rewindinode(self);
        off_t remaining = offset;
        while (remaining != 0) {
            off_t skiplen;
            if (SIZE_MAX < remaining) {
                skiplen = SIZE_MAX;
            } else {
                skiplen = remaining;
            }
            assert(skiplen != 0);
            ret = skipread_inode(self, skiplen);
            if (ret == -ENOENT) {
                assert(!"TODO");
            } else if (ret < 0) {
                goto fail;
            }
            remaining -= skiplen;
        }
        break;
    }
    case SEEK_CUR:
    case SEEK_END:
        assert(!"TODO");
        break;
    default:
        ret = -EINVAL;
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

[[nodiscard]] static int read_inode_blocks(struct ino_context *self, size_t count, uint8_t **dest_inout, size_t *remaining_len_inout) {
    uint8_t *dest = *dest_inout;
    size_t remaining_len = *remaining_len_inout;
    int ret = -ENOENT;
    /* Blocks may not be contiguous on ext2, but if we can, it's faster to read as much sectors at once. */
    uint32_t last_base = self->current_block_addr;
    size_t contiguous_len = 1;
    for (blkcnt_t i = 0; i < (count - 1); i++) {
        ret = next_inode_block(self);
        if (ret < 0) {
            goto out;
        }
        if (self->current_block_addr != last_base + contiguous_len) {
            ret = read_blocks(self->fs, dest, last_base, contiguous_len);
            if (ret < 0) {
                goto out;
            }
            size_t read_size = self->fs->blocksize * contiguous_len;
            dest += read_size;
            remaining_len -= read_size;
            contiguous_len = 1;
            last_base = self->current_block_addr;
        } else {
            contiguous_len++;
        }
    }
    ret = read_blocks(self->fs, dest, last_base, contiguous_len);
    if (ret < 0) {
        goto out;
    }
    size_t readsize = self->fs->blocksize * contiguous_len;
    dest += readsize;
    remaining_len -= readsize;
    Heap_Free(self->blockbuf.buf);
    self->blockbuf.buf = NULL;
    ret = next_inode_block(self);
    if ((ret < 0) && ((ret != -ENOENT) || (remaining_len != 0))) {
        goto out;
    } else {
        ret = 0;
    }
out:
    *remaining_len_inout = remaining_len;
    *dest_inout = dest;
    return ret;
}

[[nodiscard]] static int read_inode(struct ino_context *self, void *buf, size_t len) {
    assert(len <= STREAM_MAX_TRANSFER_SIZE);
    int ret = 0;
    size_t remaining_len = len;
    uint8_t *dest = buf;

    while (remaining_len != 0) {
        if (self->fs->blocksize <= self->blockbuf.offset_in_buf) {
            /* We've ran out of current block, so we need to move to the next block */
            ret = next_inode_block_and_reset_blockbuf(self);
            if (ret < 0) {
                goto out;
            }
        }
        /* Read as much blocks as we can, directly to the destination. ********/
        if ((self->blockbuf.offset_in_buf == 0) &&
            ((size_t)self->fs->blocksize <= remaining_len)) {
            blkcnt_t blkcount = remaining_len / self->fs->blocksize;
            ret = read_inode_blocks(self, blkcount, &dest, &remaining_len);
            if (ret < 0) {
                goto out;
            }
            assert(ret == 0);
        }
        if (remaining_len == 0) {
            break;
        }

        if (self->blockbuf.buf == NULL) {
            /* We don't have valid block buffer - Let's buffer a block ********/
            uint8_t *newbuf = NULL;
            ret = readblocks_alloc(&newbuf, self->fs, self->current_block_addr, 1);
            if (ret < 0) {
                goto out;
            }
            self->blockbuf.buf = newbuf;
        }
        assert(self->blockbuf.offset_in_buf < self->fs->blocksize);
        /* Read from current buffered block data, as much as we can. **********/
        blkcnt_t maxlen = self->fs->blocksize - self->blockbuf.offset_in_buf;
        blkcnt_t readlen = remaining_len;
        if (maxlen < readlen) {
            readlen = maxlen;
        }
        assert(readlen != 0);
        memcpy(dest, &self->blockbuf.buf[self->blockbuf.offset_in_buf], readlen);
        self->blockbuf.offset_in_buf += readlen;
        dest += readlen;
        remaining_len -= readlen;
    }
    goto out;
out:
    return ret;
}

[[nodiscard]] static int openinode(struct ino_context *out, struct fscontext *self, ino_t inode) {
    int ret = 0;
    uint32_t block_addr;
    off_t offset;
    uint8_t *blkdata = NULL;
    ret = locate_inode(&block_addr, &offset, self, inode);
    if (ret < 0) {
        goto fail;
    }
    ret = readblocks_alloc(&blkdata, self, block_addr, 1);
    if (ret < 0) {
        goto fail;
    }
    uint8_t *inodedata = &blkdata[offset];
    memset(out, 0, sizeof(*out));
    uint32_t sizel = 0;
    uint32_t sizeh = 0;
    out->fs = self;
    out->typeandpermissions = Uint16LeAt(&inodedata[0x00]);
    out->uid = Uint16LeAt(&inodedata[0x02]);
    sizel = Uint32LeAt(&inodedata[0x04]);
    out->lastaccesstime = Uint32LeAt(&inodedata[0x08]);
    out->creationtime = Uint32LeAt(&inodedata[0x0c]);
    out->lastmodifiedtime = Uint32LeAt(&inodedata[0x10]);
    out->deletiontime = Uint32LeAt(&inodedata[0x14]);
    out->gid = Uint16LeAt(&inodedata[0x18]);
    out->hardlinks = Uint16LeAt(&inodedata[0x1a]);
    out->disksectors = Uint32LeAt(&inodedata[0x1c]);
    out->flags = Uint32LeAt(&inodedata[0x20]);
    for (int i = 0; i < 12; i++) {
        out->direct_block_ptrs[i] = Uint32LeAt(&inodedata[0x28 + sizeof(*out->direct_block_ptrs) * i]);
    }
    out->singly_indirect_table = Uint32LeAt(&inodedata[0x58]);
    out->doublyindirecttable = Uint32LeAt(&inodedata[0x5c]);
    out->triplyindirecttable = Uint32LeAt(&inodedata[0x60]);
    out->generationnumber = Uint32LeAt(&inodedata[0x64]);
    if (1 <= self->major_ver) {
        if (self->required_features_rw & RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE) {
            sizeh = Uint32LeAt(&inodedata[0x6c]);
        }
    }
    if ((sizeh >> 31U) != 0) {
        ret = -EINVAL;
        goto out;
    }
    out->size = (off_t)(((uint64_t)sizeh << 32U) | (uint64_t)sizel);
    /* Move to the very first block */
    ret = next_inode_block(out);
    /* Above should only access the first direct block pointer, which cannot fail. */
    MUST_SUCCEED(ret);
    goto out;
fail:
out:
    Heap_Free(blkdata);
    return ret;
}

static void closeinode(struct ino_context *self) {
    if (self == NULL) {
        return;
    }
    Heap_Free(self->blockbuf.buf);
    Heap_Free(self->singly_indirect_buf.buf);
    Heap_Free(self->doubly_indirect_buf.buf);
    Heap_Free(self->triply_indirect_buf.buf);
}

struct directory {
    struct DIR dir;
    struct ino_context inocontext;
};

/*
 * Returns -ENOENT when it reaches end of the directory.
 */
[[nodiscard]] static int read_directory(struct dirent *out, DIR *self) {
    struct directory *dir = self->data;
    int ret = 0;
    while (1) {
        uint8_t header[8];
        memset(out, 0, sizeof(*out));
        ret = read_inode(&dir->inocontext, header, 8);
        if (ret < 0) {
            goto fail;
        }
        out->d_ino = Uint32LeAt(&header[0x0]);
        size_t entrysize = Uint16LeAt(&header[0x4]);
        size_t namelen = header[0x6];
        if (!(dir->inocontext.fs->required_features & REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPE_FIELD)) {
            /* YJK/OS does not support names longer than 255 characters. */
            if (header[0x7] != 0) {
                ret = -ENAMETOOLONG;
                goto fail;
            }
        }
        ret = read_inode(&dir->inocontext, out->d_name, namelen);
        if (ret < 0) {
            goto fail;
        }
        size_t readlen = namelen + sizeof(header);
        size_t skiplen = entrysize - readlen;
        ret = skipread_inode(&dir->inocontext, skiplen);
        if (ret < 0) {
            goto fail;
        }
        if (out->d_ino != 0) {
            break;
        }
    }
    goto out;
fail:
out:
    return ret;
}

[[nodiscard]] static int open_directory(DIR **dir_out, struct fscontext *self, ino_t inode) {
    int ret = 0;
    *dir_out = NULL;
    struct directory *dir = Heap_Alloc(sizeof(*dir), HEAP_FLAG_ZEROMEMORY);
    if (dir == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    ret = openinode(&dir->inocontext, self, inode);
    if (ret < 0) {
        goto fail_after_alloc;
    }
    if ((dir->inocontext.typeandpermissions & INODE_TYPE_MASK) != INODE_TYPE_DIRECTORY) {
        ret = -ENOTDIR;
        goto fail_after_open;
    }
    dir->dir.data = dir;
    dir->dir.fscontext = &self->vfs_fscontext;
    *dir_out = &dir->dir;
    goto out;
fail_after_open:
    closeinode(&dir->inocontext);
fail_after_alloc:
    Heap_Free(dir);
fail:
out:
    return ret;
}

static void close_directory(DIR *self) {
    if (self == NULL) {
        return;
    }
    struct directory *dir = self->data;
    closeinode(&dir->inocontext);
    Heap_Free(dir);
}

[[nodiscard]] static int openfile(struct ino_context *out, struct fscontext *self, ino_t inode) {
    int ret;
    ret = openinode(out, self, inode);
    assert(ret != -ENOENT);
    if (ret < 0) {
        goto fail;
    }
    if ((out->typeandpermissions & INODE_TYPE_MASK) == INODE_TYPE_DIRECTORY) {
        ret = -EISDIR;
        goto fail_after_open;
    }
    goto out;
fail_after_open:
    closeinode(out);
fail:
out:
    return ret;
}

static void closefile(struct ino_context *self) {
    if (self == NULL) {
        return;
    }
    closeinode(self);
}

[[nodiscard]] static int resolve_path(ino_t *ino_out, struct fscontext *self, ino_t parent, char const *path) {
    int ret = 0;
    DIR *dir;
    ino_t current_ino = parent;
    struct PathReader reader;
    PathReader_Init(&reader, path);
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
        ret = open_directory(&dir, self, current_ino);
        if (ret < 0) {
            goto out;
        }
        while (1) {
            struct dirent ent;
            ret = read_directory(&ent, dir);
            if (ret == -ENOENT) {
                ret = -ENOENT;
                break;
            }
            if (ret < 0) {
                break;
            }
            current_ino = ent.d_ino;
            if (strcmp(name, ent.d_name) == 0) {
                ret = 0;
                break;
            }
        }
        close_directory(dir);
        if (ret < 0) {
            goto out;
        }
    }
    *ino_out = current_ino;
    goto out;
out:
    return ret;
}

struct openfdcontext {
    struct ino_context inocontext;
    struct File fd;
    off_t cursorpos;
};

[[nodiscard]] static ssize_t fd_op_read(struct File *self, void *buf, size_t len) {
    assert(len <= STREAM_MAX_TRANSFER_SIZE);
    struct openfdcontext *context = self->data;
    off_t maxlen = context->inocontext.size - context->cursorpos;
    size_t readlen = len;
    if (maxlen < readlen) {
        readlen = maxlen;
    }
    int ret = read_inode(&context->inocontext, buf, readlen);
    assert(ret != -ENOENT);
    if (ret < 0) {
        goto fail;
    }
    context->cursorpos += readlen;
    goto out;
fail:
    readlen = -1;
out:
    return (ssize_t)readlen;
}

[[nodiscard]] static ssize_t fd_op_write(struct File *self, void const *buf, size_t len) {
    assert(len <= STREAM_MAX_TRANSFER_SIZE);
    (void)self;
    (void)buf;
    return -EIO;
}

[[nodiscard]] static int fd_op_seek(struct File *self, off_t offset, int whence) {
    struct openfdcontext *context = self->data;
    int ret = seek_inode(&context->inocontext, offset, whence);
    assert(ret != -ENOENT);
    return ret;
}

static void fd_op_close(struct File *self) {
    struct openfdcontext *context = self->data;
    Vfs_UnregisterFile(self);
    closefile(&context->inocontext);
    Heap_Free(context);
}

static struct FileOps const FD_OPS = {
    .read = fd_op_read,
    .write = fd_op_write,
    .seek = fd_op_seek,
    .close = fd_op_close,
};

[[nodiscard]] static int vfs_op_mount(struct Vfs_FsContext **out, struct LDisk *disk) {
    int ret = 0;
    uint8_t superblk[1024];
    struct fscontext *context = Heap_Alloc(sizeof(*context), HEAP_FLAG_ZEROMEMORY);
    /* Read superblock ********************************************************/
    {
        assert(1024 % disk->physdisk->block_size == 0);
        off_t blockoffset = 1024 / disk->physdisk->block_size;
        blkcnt_t blkcount = 1024 / disk->physdisk->block_size;
        ret = Ldisk_ReadExact(disk, superblk, blockoffset, blkcount);
        if (ret < 0) {
            goto fail;
        }
    }
    context->signature = Uint16LeAt(&superblk[0x038]);
    if (context->signature != EXT2_SIGNATURE) {
        Iodev_Printf(&disk->iodev, "ext2: invalid superblk signature\n");
        ret = -EINVAL;
        goto fail;
    }
    context->disk = disk;
    context->total_inodes = Uint32LeAt(&superblk[0x000]);
    context->total_blocks = Uint32LeAt(&superblk[0x004]);
    context->reserved_blocks_for_su = Uint32LeAt(&superblk[0x008]);
    context->total_unallocated_blocks = Uint32LeAt(&superblk[0x00c]);
    context->total_unallocated_inodes = Uint32LeAt(&superblk[0x010]);
    context->superblock_block_num = Uint32LeAt(&superblk[0x014]);
    uint32_t blocksize_raw = Uint32LeAt(&superblk[0x018]);
    if (21 < blocksize_raw) {
        Iodev_Printf(&disk->iodev, "ext2: block size value is too large\n");
        ret = -EINVAL;
        goto fail;
    }
    context->blocksize = (blksize_t)(1024UL << blocksize_raw);
    context->blocks_in_block_group = Uint32LeAt(&superblk[0x020]);
    context->inodes_in_block_group = Uint32LeAt(&superblk[0x028]);
    context->last_mount_time = Uint32LeAt(&superblk[0x02c]);
    context->last_written_time = Uint32LeAt(&superblk[0x030]);
    context->mounts_since_last_fsck = Uint16LeAt(&superblk[0x034]);
    context->mounts_before_fsck_required = Uint16LeAt(&superblk[0x036]);
    context->fs_state = Uint16LeAt(&superblk[0x03a]);
    context->err_action = Uint16LeAt(&superblk[0x03c]);
    context->minor_ver = Uint16LeAt(&superblk[0x03e]);
    context->last_fsck_time = Uint32LeAt(&superblk[0x040]);
    context->fsck_interval = Uint32LeAt(&superblk[0x044]);
    context->creator_os_id = Uint32LeAt(&superblk[0x048]);
    context->major_ver = Uint32LeAt(&superblk[0x04c]);
    context->reserved_block_uid = Uint16LeAt(&superblk[0x050]);
    context->reserved_block_gid = Uint16LeAt(&superblk[0x052]);

    if (1 <= context->major_ver) {
        context->first_non_reserved_inode = Uint16LeAt(&superblk[0x054]);
        context->inode_size = Uint16LeAt(&superblk[0x058]);
        context->block_group = Uint16LeAt(&superblk[0x05a]);
        context->optional_features = Uint32LeAt(&superblk[0x05c]);
        context->required_features = Uint32LeAt(&superblk[0x060]);
        context->required_features_rw = Uint32LeAt(&superblk[0x064]);
        memcpy(context->filesystem_id, &superblk[0x068], sizeof(context->filesystem_id));
        memcpy(context->volumename, &superblk[0x078], sizeof(context->volumename));
        memcpy(context->last_mount_path, &superblk[0x088], sizeof(context->last_mount_path));
        bool not_terminated = false;
        if (context->volumename[sizeof(context->volumename) - 1] != '\0') {
            context->volumename[sizeof(context->volumename) - 1] = '\0';
            not_terminated = true;
        }
        if (context->last_mount_path[sizeof(context->last_mount_path) - 1] != '\0') {
            context->last_mount_path[sizeof(context->last_mount_path) - 1] = '\0';
            not_terminated = true;
        }
        if (not_terminated) {
            Iodev_Printf(&disk->iodev, "ext2: some strings in superblock were not terminated - terminating at the last character\n");
        }
        context->compressionalgorithms = Uint32LeAt(&superblk[0x0c8]);
        context->preallocatefileblks = superblk[0x0cc];
        context->preallocatedirblks = superblk[0x0cd];
        memcpy(context->journalid, &superblk[0x0d0], sizeof(context->journalid));
        context->journalinode = Uint32LeAt(&superblk[0x0e0]);
        context->journaldevice = Uint32LeAt(&superblk[0x0e4]);
        context->orphaninodelisthead = Uint32LeAt(&superblk[0x0e8]);
    } else {
        context->first_non_reserved_inode = 11;
        context->inode_size = 128;
    }
    uint8_t const *id = context->filesystem_id;
    Iodev_Printf(
        &disk->iodev,
        "ext2 V%u-%02u, ID: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
        context->major_ver, context->minor_ver,
        id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9],
        id[10], id[11], id[12], id[13], id[14], id[15]);

    size_t blk_group_count = SizeToBlocks(context->total_blocks, context->blocks_in_block_group);
    size_t blk_group_count2 = SizeToBlocks(context->total_inodes, context->inodes_in_block_group);
    if (blk_group_count != blk_group_count2) {
        Iodev_Printf(&disk->iodev, "Two calculated blk group count does not match: %zu != %zu\n", blk_group_count, blk_group_count2);
    }
    context->blk_group_count = blk_group_count;
    if (context->blocksize == 1024) {
        /*
         *  0        1024        2048         3072
         *  |----------|-----------|------------|---
         *    Block 0     Block 1     Block 2
         *              SSSSSSSSSSS BBBBBBBBBBBBBBBB
         *              |           |
         * Superblock --+           |
         * BGDT --------------------+
         */
        context->blk_group_descriptor_blk = 2;
    } else {
        /*
         *  0        1024      blocksize
         *  |----------------------|----------------
         *          Block 0        |       Block 1
         *              SSSSSSSSSSS BBBBBBBBBBBBBBBB
         *              |           |
         * Superblock --+           |
         * BGDT --------------------+
         */
        context->blk_group_descriptor_blk = 1;
    }

    /* Check feature flags ****************************************************/
    if (context->required_features & ~SUPPORTED_REQUIRED_FLAGS) {
        Iodev_Printf(&disk->iodev, "ext2: found unsupported required features(flag %x)\n", context->required_features & ~SUPPORTED_REQUIRED_FLAGS);
        ret = -EINVAL;
        goto fail;
    }
    if (context->required_features_rw & ~SUPPORTED_RWMOUNT_FLAGS) {
        Iodev_Printf(&disk->iodev, "ext2: found unsupported required features for R/W mount(flag %x)\n", context->required_features_rw & ~SUPPORTED_RWMOUNT_FLAGS);
        ret = -EINVAL;
        goto fail;
    }
    context->vfs_fscontext.data = context;
    *out = &context->vfs_fscontext;
    goto out;
fail:
    Heap_Free(context);
out:
    return ret;
}

[[nodiscard]] static int vfs_op_umount(struct Vfs_FsContext *self) {
    Heap_Free(self->data);
    return 0;
}

[[nodiscard]] static int vfs_op_open(struct File **out, struct Vfs_FsContext *self, char const *path, int flags) {
    int ret;
    ino_t inode;
    struct fscontext *fscontext = self->data;
    (void)flags;
    struct openfdcontext *fdcontext = Heap_Alloc(sizeof(*fdcontext), HEAP_FLAG_ZEROMEMORY);
    ret = resolve_path(&inode, fscontext, INODE_ROOTDIRECTORY, path);
    if (ret < 0) {
        goto fail_after_alloc;
    }
    ret = openfile(&fdcontext->inocontext, fscontext, inode);
    if (ret < 0) {
        goto fail_after_alloc;
    }
    ret = Vfs_RegisterFile(&fdcontext->fd, &FD_OPS, self, fdcontext);
    if (ret < 0) {
        goto fail_after_open;
    }
    *out = &fdcontext->fd;
    goto out;
fail_after_open:
    closefile(&fdcontext->inocontext);
fail_after_alloc:
    Heap_Free(fdcontext);
out:
    return ret;
}

[[nodiscard]] static int vfs_op_opendir(DIR **out, struct Vfs_FsContext *self, char const *path) {
    int ret;
    ino_t inode;
    struct fscontext *fscontext = self->data;
    ret = resolve_path(&inode, fscontext, INODE_ROOTDIRECTORY, path);
    if (ret < 0) {
        goto fail;
    }
    ret = open_directory(out, fscontext, inode);
    if (ret < 0) {
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

[[nodiscard]] static int vfs_op_closedir(DIR *self) {
    close_directory(self);
    return 0;
}

[[nodiscard]] static int vfs_op_readdir(struct dirent *out, DIR *self) {
    return read_directory(out, self);
}

static struct Vfs_FsTypeOps const FSTYPE_OPS = {
    .Mount = vfs_op_mount,
    .Umount = vfs_op_umount,
    .Open = vfs_op_open,
    .OpenDir = vfs_op_opendir,
    .CloseDir = vfs_op_closedir,
    .ReadDir = vfs_op_readdir,
};

static struct Vfs_FsType s_fstype;

void FsInit_InitExt2(void) {
    Vfs_RegisterFsType(&s_fstype, "ext2", &FSTYPE_OPS);
}
