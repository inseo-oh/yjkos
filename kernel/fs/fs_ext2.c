#include "fsinit.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/types.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

enum {
    EXT2_SIGNATURE = 0xef53,

    FSSTATE_CLEAN = 1,
    FSSTATE_ERROR = 2,

    ERRACTION_IGNORE     = 1,
    ERRACTION_REMOUNT_RO = 2,
    ERRACTION_PANIC      = 3,

    INODE_ROOTDIRECTORY = 2,
};

#define REQUIRED_FEATUREFLAG_COMPRESSION                    (1U<< 0U)
#define REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPE_FIELD   (1U << 1U)
#define REQUIRED_FEATUREFLAG_NEED_REPLAY_JOURNAL            (1U << 2U)
#define REQUIRED_FEATUREFLAG_JOURNAL_DEVICE_USED            (1U << 3U)

// Sparse superblocks and group descriptor tables
#define RWMOUNT_FEATUREFLAG_SPARSE_SUPERBLOCK_AND_GDTABLE   (1U << 0U)
#define RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE                 (1U << 1U)
// Directory contents are stored in the form of a Binary Tree
#define RWMOUNT_FEATUREFLAG_BINARY_TREE_DIR                 (1U << 2U)

#define SUPPORTED_REQUIRED_FLAGS    \
    REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPE_FIELD
    
#define SUPPORTED_RWMOUNT_FLAGS     \
    (RWMOUNT_FEATUREFLAG_SPARSE_SUPERBLOCK_AND_GDTABLE | \
    RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE)

struct fscontext {
    //--------------------------------------------------------------------------
    // Superblock
    //--------------------------------------------------------------------------
    uint32_t superblkblknum;
    size_t totalinodes;
    blkcnt_t totalblks;
    blkcnt_t totalunallocatedblocks;
    size_t totalunallocatedinodes;
    blkcnt_t reservedblksforsu;
    blksize_t blocksize;
    blkcnt_t blksinblkgroup;
    size_t inodesinblkgroup;
    time_t lastmounttime;
    time_t laswrittentime;
    uint16_t mountssincelastfsck;
    uint16_t mountsbeforefsckrequired;
    uint16_t signature;
    uint16_t fsstate;   // See FSSTATE_~ values
    uint16_t erraction; // See ERRACTION_~ values
    uint16_t minorver;
    time_t lastfscktime;
    time_t fsckinterval;
    uint32_t creatorosid;
    uint32_t majorver;
    uid_t reservedblkuid;
    gid_t reservedblkgid;

    // Below are superblk fields for 1.0 <= Version
    uint32_t blkgroup;      // If it's a backup copy
    ino_t firstnonreservedinode;    // Pre-1.0: 11
    size_t inodesize;               // Pre-1.0: 128
    uint32_t optionalfeatures;
    uint32_t requiredfeatures;      // Required features for both R/W and R/O mount 
    uint32_t requiredfeatures_rw;   // Required features for R/W mount
    uint32_t compressionalgorithms; // If compression is used
    uint8_t preallocatefileblks;
    uint8_t preallocatedirblks;
    uint32_t journalinode;
    uint32_t journaldevice;
    uint32_t orphaninodelisthead;
    uint8_t filesystemid[16];       // 16-byte UUID
    uint8_t journalid[16];          // 16-byte UUID
    char volumename[16];
    char lastmountpath[64];

    //--------------------------------------------------------------------------
    // Other fields needed for FS management
    //--------------------------------------------------------------------------
    struct ldisk *disk;
    size_t blkgroupcount;
    size_t blkgroupdescriptorblk;
    struct vfs_fscontext vfs_fscontext;
};

struct blkgroupdescriptor {
    uint32_t blkusagebitmap;
    uint32_t inodeusagebitmap;
    uint32_t inodetable;
    blkcnt_t unallocatedblocks;
    size_t unallocatedinodes;
    size_t directories;
    uint8_t unused[14];
};

struct inocontext {
    off_t size;
    size_t hardlinks;
    size_t disksectors;
    uint32_t direct_blockptrs[12];
    uint32_t singlyindirecttable;
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
    uint32_t currentblockaddr;
    size_t next_direct_ptr_index;
    size_t cnt;

    struct {
        off_t offset_in_buf;
        uint8_t *buf;
    } singlyindirectbuf, doublyindirectbuf, triplyindirectbuf, blockbuf;
    bool singlyindirectused : 1;
    bool doublyindirectused : 1;
    bool triplyindirectused : 1;
};

// Bitmask values for type and permissions
#define INODE_TYPE_MASK          0xf000U
#define INODE_TYPE_FIFO          0x1000U
#define INODE_TYPE_CHARACTER     0x2000U
#define INODE_TYPE_DIRECTORY     0x4000U
#define INODE_TYPE_BLOCK_DEVICE  0x6000U
#define INODE_TYPE_REGULAR_FILE  0x8000U
#define INODE_TYPE_SYMBOLIC_LINK 0xa000U
#define INODE_TYPE_UNIX_SOCKET   0xc000U


// `buf` must be able to hold `blkcount * self->blocksize` bytes.
static WARN_UNUSED_RESULT int readblocks(
    struct fscontext *self, void *buf, uint32_t blockaddr, blkcnt_t blkcount)
{
    int ret = 0;
    /*
     * TODO: support cases where self->blocksize < 
     *          self->disk->physdisk->blocksize
     */
    assert((self->blocksize % self->disk->physdisk->blocksize) == 0);
    diskblkptr diskblockaddr =
        blockaddr * (self->blocksize / self->disk->physdisk->blocksize);
    blkcnt_t diskblkcount =
        blkcount * (self->blocksize / self->disk->physdisk->blocksize);
    ret = (ldisk_read_exact(
        self->disk, buf, diskblockaddr,
        diskblkcount));
    if (ret < 0) {
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

// Returns NULL when there's not enough memory.
static WARN_UNUSED_RESULT uint8_t *allocblockbuf(struct fscontext *self,
    blkcnt_t count, uint8_t flags)
{
    uint8_t *buf = heap_calloc(count, self->blocksize, flags);
    if (buf == NULL) {
        return NULL;
    }
    return buf;
}

static WARN_UNUSED_RESULT int readblocks_alloc(
    uint8_t **out, struct fscontext *self, uint32_t blockaddr,
    blkcnt_t blkcount)
{
    int ret;
    uint8_t *buf = allocblockbuf(self, blkcount, 0);
    if (buf == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    ret = readblocks(self, buf, blockaddr, blkcount);
    if (ret < 0) {
        goto fail;
    }
    *out = buf;
    goto out;
fail:
    heap_free(buf);
out:
    return ret;
}

static WARN_UNUSED_RESULT int readblockgroupdescriptor(
    struct blkgroupdescriptor *out,
    struct fscontext *self, uint32_t blockgroup)
{
    int ret;
    enum {
        DESCRIPTOR_SIZE = 32
    };
    assert(blockgroup < (SIZE_MAX / DESCRIPTOR_SIZE));
    off_t byteoffset = (off_t)blockgroup * DESCRIPTOR_SIZE;
    uint32_t blockoffset = byteoffset / self->blocksize;
    off_t byteoffsetinblk = byteoffset % self->blocksize;
    assert(blockoffset < (SIZE_MAX - self->blkgroupdescriptorblk));
    blockoffset += self->blkgroupdescriptorblk;

    uint8_t *buf = NULL;
    ret = readblocks_alloc(
        &buf, self, blockoffset, 1);
    if (ret < 0) {
        goto fail;
    }
    uint8_t *data = &buf[byteoffsetinblk];
    out->blkusagebitmap    = uint32leat(&data[0x00]);
    out->inodeusagebitmap  = uint32leat(&data[0x04]);
    out->inodetable        = uint32leat(&data[0x08]);
    out->unallocatedblocks = uint16leat(&data[0x0c]);
    out->unallocatedinodes = uint16leat(&data[0x0e]);
    out->directories       = uint16leat(&data[0x10]);
    goto out;
fail:
out:
    heap_free(buf);
    return ret;
}

static uint32_t blockgroup_of_inode(struct fscontext *self, ino_t inodeaddr) {
    return (inodeaddr - 1) / self->inodesinblkgroup;
}

static WARN_UNUSED_RESULT int locateinode(
    uint32_t *blk_out, off_t *off_out, struct fscontext *self, ino_t inodeaddr)
{
    int ret = 0;
    struct blkgroupdescriptor blkgroup;
    ret = readblockgroupdescriptor(
        &blkgroup, self, blockgroup_of_inode(self, inodeaddr));
    if (ret < 0) {
        goto fail;
    }
    off_t index = (inodeaddr - 1) % self->inodesinblkgroup;
    assert(index < (SIZE_MAX / self->inodesize));
    *blk_out = blkgroup.inodetable +
        ((index * self->inodesize) / self->blocksize);
    *off_out = (index * self->inodesize) % self->blocksize;
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int next_direct_blockptr(
    uint32_t *addr_out, struct inocontext *self)
{
    uint32_t result_addr;
    // We can use direct block pointer
    result_addr = self->direct_blockptrs[self->next_direct_ptr_index];
    if (result_addr == 0) {
        return -ENOENT;
    }
    self->next_direct_ptr_index++;
    *addr_out = result_addr;
    return 0;
}

static WARN_UNUSED_RESULT int next_triply_indirect_table(
    struct inocontext *self)
{
    uint32_t tableaddr;
    int ret = 0;
    if (!self->triplyindirectused) {
        // We are using triply indirect table for the first time
        tableaddr = self->triplyindirecttable;
    } else {
        iodev_printf(
            &self->fs->disk->iodev,
            "File is too large\n");
        ret = -ENOENT;
        goto out;
    }
    if (tableaddr == 0) {
        heap_free(self->triplyindirectbuf.buf);
        self->triplyindirectbuf.buf = NULL;
        self->triplyindirectbuf.offset_in_buf = 0;
        ret = -ENOENT;
        goto out;
    }
    uint8_t *newtable = NULL;
    ret = readblocks_alloc(&newtable, self->fs, tableaddr, 1);
    if (ret < 0) {
        goto out;
    }
    heap_free(self->triplyindirectbuf.buf);
    self->triplyindirectbuf.buf = newtable;
    self->triplyindirectbuf.offset_in_buf = 0;
    ret = 0;
out:
    return ret;
}

static WARN_UNUSED_RESULT int next_triply_blockptr(
     uint32_t *addr_out, struct inocontext *self)
{
    uint32_t tableaddr;
    int ret = -ENOENT;

    if (
        (self->triplyindirectbuf.buf == NULL) || (self->fs->blocksize <= self->triplyindirectbuf.offset_in_buf))
    {
        ret = next_triply_indirect_table(self);
        if (ret < 0) {
            goto out;
        }
    }
    self->triplyindirectused = true;
    tableaddr = uint32leat(
        &self->triplyindirectbuf.buf[
            self->triplyindirectbuf.offset_in_buf]);
    self->triplyindirectbuf.offset_in_buf += sizeof(uint32_t);
    *addr_out = tableaddr;
    ret = 0;
out:
    return ret;
}

static WARN_UNUSED_RESULT int next_doubly_indirect_table(
    struct inocontext *self)
{
    int ret = -ENOENT;
    // We need to move to next doubly indirect table
    uint32_t tableaddr;
    if (!self->doublyindirectused) {
        // We are using doubly indirect table for the first time
        tableaddr = self->doublyindirecttable;
        if (tableaddr != 0) {
            ret = 0;
        }
        self->doublyindirectused = true;
    } else {
        ret = next_triply_blockptr(&tableaddr, self);
    }
    if (ret < 0) {
        heap_free(self->doublyindirectbuf.buf);
        self->doublyindirectbuf.buf = NULL;
        self->doublyindirectbuf.offset_in_buf = 0;
        goto out;
    }
    uint8_t *newtable = NULL;
    ret = readblocks_alloc(
        &newtable, self->fs, tableaddr,
        1);
    if (ret < 0) {
        goto out;
    }
    heap_free(self->doublyindirectbuf.buf);
    self->doublyindirectbuf.buf = newtable;
    self->doublyindirectbuf.offset_in_buf = 0;
    ret = 0;
out:
    return ret;
}

static WARN_UNUSED_RESULT int next_doubly_blockptr(
     uint32_t *addr_out, struct inocontext *self)
{
    uint32_t result_addr = 0;
    int ret = -ENOENT;
    if (
        (self->doublyindirectbuf.buf == NULL) || (self->fs->blocksize <= self->doublyindirectbuf.offset_in_buf))
    {
        ret = next_doubly_indirect_table(self);
        if (ret < 0) {
            goto out;
        }
    }
    result_addr = uint32leat(
        &self->doublyindirectbuf.buf[
            self->doublyindirectbuf.offset_in_buf]);
    self->doublyindirectbuf.offset_in_buf += sizeof(uint32_t);
    if (result_addr == 0) {
        ret = -ENOENT;
        goto out;
    }
    ret = 0;
    *addr_out = result_addr;
out:
    return ret;
}

static WARN_UNUSED_RESULT int next_singly_indirect_table(
    struct inocontext *self)
{
    int ret = -ENOENT;
    uint32_t tableaddr;
    if (!self->singlyindirectused) {
        // We are using singly indirect table for the first time
        tableaddr = self->singlyindirecttable;
        if (tableaddr != 0) {
            ret = 0;
        }
    } else {
        ret = next_doubly_blockptr(&tableaddr, self);
    }
    if (ret < 0) {
        heap_free(self->singlyindirectbuf.buf);
        self->singlyindirectbuf.buf = NULL;
        self->singlyindirectbuf.offset_in_buf = 0;
        ret = -ENOENT;
        goto out;
    }
    uint8_t *newtable = NULL;
    ret = readblocks_alloc(
        &newtable, self->fs, tableaddr, 1);
    if (ret < 0) {
        goto out;
    }
    heap_free(self->singlyindirectbuf.buf);
    self->singlyindirectbuf.buf = newtable;
    self->singlyindirectbuf.offset_in_buf = 0;
    self->singlyindirectused = true;
    ret = 0;
out:
    return ret;
}

// Returns -ENOENT on EOF.
static WARN_UNUSED_RESULT int next_singly_blockptr(
    uint32_t *addr_out, struct inocontext *self)
{
    int ret = -ENOENT;
    uint32_t result_addr = 0;
    if (
        (self->singlyindirectbuf.buf == NULL) ||
        (self->fs->blocksize <= self->singlyindirectbuf.offset_in_buf))
    {
        ret = next_singly_indirect_table(self);
        if (ret < 0) {
            goto out;
        }
    }
    result_addr = uint32leat(
        &self->singlyindirectbuf.buf[
            self->singlyindirectbuf.offset_in_buf]);
    if (result_addr == 0) {
        goto out;
    }
    self->singlyindirectbuf.offset_in_buf += sizeof(uint32_t);
    ret = 0;
    *addr_out = result_addr;
out:
    return ret;
}

// Returns -ENOENT on EOF.
static WARN_UNUSED_RESULT int next_inode_block(struct inocontext *self) {
    int ret = 0;
    enum {
        DIRECT_BLOCK_POINTER_COUNT =
            sizeof(self->direct_blockptrs) / sizeof(*self->direct_blockptrs)
    };
    uint32_t result_addr = 0;
    if (self->next_direct_ptr_index < DIRECT_BLOCK_POINTER_COUNT) {
        // We can use direct block pointer
        ret = next_direct_blockptr(&result_addr, self);
    } else {
        ret = next_singly_blockptr(&result_addr, self);
    }
    if (ret == 0) {
        self->currentblockaddr = result_addr;
        self->cnt++;
    }
    goto out;
out:
    return ret;
}

static void rewindinode(struct inocontext *self) {
    heap_free(self->blockbuf.buf);
    heap_free(self->singlyindirectbuf.buf);
    heap_free(self->doublyindirectbuf.buf);
    heap_free(self->triplyindirectbuf.buf);
    memset(&self->blockbuf, 0, sizeof(self->blockbuf));
    memset(
        &self->singlyindirectbuf, 0, sizeof(self->singlyindirectbuf));
    memset(
        &self->doublyindirectbuf, 0, sizeof(self->doublyindirectbuf));
    memset(
        &self->triplyindirectbuf, 0, sizeof(self->triplyindirectbuf));
    self->currentblockaddr = 0;
    self->next_direct_ptr_index = 0;
    self->singlyindirectused = false;
    self->doublyindirectused = false;
    self->triplyindirectused = false;
    self->cnt = 0;
    // Move to the very first block
    int ret = next_inode_block(self);
    /*
     * Above should only access the first direct block pointer, which cannot
     * fail.
     */
    MUST_SUCCEED(ret);
}

static WARN_UNUSED_RESULT int next_inode_block_and_reset_blockbuf(
    struct inocontext *self)
{
    int ret = next_inode_block(self);
    if (ret < 0) {
        return ret;
    }
    // Invalidate old buffer
    heap_free(self->blockbuf.buf);
    self->blockbuf.buf = NULL;
    self->blockbuf.offset_in_buf = 0;
    return 0;
}

// Returns -ENOENT on EOF
static WARN_UNUSED_RESULT int skipread_inode(
    struct inocontext *self, size_t len)
{
    assert(len <= STREAM_MAX_TRANSFER_SIZE);
    int ret = 0;
    size_t remaining_len = len;

    while(remaining_len != 0) {
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
            ((size_t)self->fs->blocksize <= remaining_len))
        {
            blkcnt_t count = remaining_len / self->fs->blocksize;
            for (blkcnt_t i = 0; i < count; i++) {
                ret = next_inode_block(self);
                if (ret < 0) {
                    goto out;
                }
            }
            size_t skip_len = self->fs->blocksize * count;
            remaining_len -= skip_len;
            heap_free(self->blockbuf.buf);
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

static WARN_UNUSED_RESULT int seekinode(
    struct inocontext *self, off_t offset, int whence)
{
    int ret = 0;
    switch(whence) {
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

static WARN_UNUSED_RESULT int read_inode_blocks(
    struct inocontext *self, size_t count, uint8_t **dest_inout,
    size_t *remaining_len_inout)
{
    uint8_t *dest = *dest_inout;
    size_t remaining_len = *remaining_len_inout;
    int ret = -ENOENT;
    /*
        * Blocks may not be contiguous on ext2, but if we can, it's faster
        * to read as much sectors at once.
        */
    uint32_t lastbase = self->currentblockaddr;
    size_t contiguous_len = 1;
    for (blkcnt_t i = 0; i < (count - 1); i++) {
        ret = next_inode_block(self);
        if (ret < 0) {
            goto out;
        }
        if (self->currentblockaddr != lastbase + contiguous_len) {
            ret = readblocks(
                self->fs, dest, lastbase,
                contiguous_len);
            if (ret < 0) {
                goto out;
            }
            size_t read_size = self->fs->blocksize * contiguous_len;
            dest += read_size;
            remaining_len -= read_size;
            contiguous_len = 1;
            lastbase = self->currentblockaddr;
        } else {
            contiguous_len++;
        }
    }
    ret = readblocks(
        self->fs, dest, lastbase,
        contiguous_len);
    if (ret < 0) {
        goto out;
    }
    size_t readsize = self->fs->blocksize * contiguous_len;
    dest += readsize;
    remaining_len -= readsize;
    heap_free(self->blockbuf.buf);
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

static WARN_UNUSED_RESULT int read_inode(
    struct inocontext *self, void *buf, size_t len)
{
    assert(len <= STREAM_MAX_TRANSFER_SIZE);
    int ret = 0;
    size_t remaining_len = len;
    uint8_t *dest = buf;

    while(remaining_len != 0) {
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
        // Read as much blocks as we can, directly to the destination.
        if ((self->blockbuf.offset_in_buf == 0) &&
            ((size_t)self->fs->blocksize <= remaining_len))
        {
            blkcnt_t blkcount = remaining_len / self->fs->blocksize;
            ret = read_inode_blocks(
                self, blkcount, &dest,
                &remaining_len);
            if (ret < 0) {
                goto out;
            } else {
                ret = 0;
            }
        }
        if (remaining_len == 0) {
            break;
        }

        if (self->blockbuf.buf == NULL) {
            // We don't have valid block buffer - Let's buffer a block
            uint8_t *newbuf = NULL;
            ret = readblocks_alloc(
                &newbuf, self->fs,
                self->currentblockaddr, 1);
            if (ret < 0) {
                goto out;
            }
            self->blockbuf.buf = newbuf;
        }
        assert(self->blockbuf.offset_in_buf < self->fs->blocksize);
        // read from current buffered block data, as much as we can.
        blkcnt_t maxlen = self->fs->blocksize - self->blockbuf.offset_in_buf;
        blkcnt_t readlen = remaining_len;
        if (maxlen < readlen) {
            readlen = maxlen;
        }
        assert(readlen != 0);
        memcpy(
            dest, &self->blockbuf.buf[self->blockbuf.offset_in_buf],
            readlen);
        self->blockbuf.offset_in_buf += readlen;
        dest += readlen;
        remaining_len -= readlen;
    }
    goto out;
out:
    return ret;
}

static WARN_UNUSED_RESULT int openinode(
    struct inocontext *out, struct fscontext *self, ino_t inode)
{
    int ret = 0;
    uint32_t blockaddr;
    off_t offset;
    uint8_t *blkdata = NULL;
    ret = locateinode(
        &blockaddr, &offset, self, inode);
    if (ret < 0) {
        goto fail;
    }
    ret = readblocks_alloc(&blkdata, self, blockaddr, 1);
    if (ret < 0) {
        goto fail;
    }
    uint8_t *inodedata = &blkdata[offset];
    memset(out, 0, sizeof(*out));
    uint32_t sizel = 0;
    uint32_t sizeh = 0;
    out->fs                         = self;
    out->typeandpermissions         = uint16leat(&inodedata[0x00]);
    out->uid                        = uint16leat(&inodedata[0x02]);
    sizel                           = uint32leat(&inodedata[0x04]);
    out->lastaccesstime             = uint32leat(&inodedata[0x08]);
    out->creationtime               = uint32leat(&inodedata[0x0c]);
    out->lastmodifiedtime           = uint32leat(&inodedata[0x10]);
    out->deletiontime               = uint32leat(&inodedata[0x14]);
    out->gid                        = uint16leat(&inodedata[0x18]);
    out->hardlinks                  = uint16leat(&inodedata[0x1a]);
    out->disksectors                = uint32leat(&inodedata[0x1c]);
    out->flags                      = uint32leat(&inodedata[0x20]);
    for (int i = 0; i < 12; i++) {
        out->direct_blockptrs[i]       = uint32leat(
            &inodedata[0x28 + sizeof(*out->direct_blockptrs) * i]);
    }
    out->singlyindirecttable        = uint32leat(&inodedata[0x58]);
    out->doublyindirecttable        = uint32leat(&inodedata[0x5c]);
    out->triplyindirecttable        = uint32leat(&inodedata[0x60]);
    out->generationnumber           = uint32leat(&inodedata[0x64]);
    if (1 <= self->majorver) {
        if (self->requiredfeatures_rw & RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE) {
            sizeh                   = uint32leat(&inodedata[0x6c]);
        }
    }
    if ((sizeh >> 31U) != 0) {
        ret = -EINVAL;
        goto out;
    }
    out->size = (off_t)(((uint64_t)sizeh << 32U) | (uint64_t)sizel);
    // Move to the very first block
    ret = next_inode_block(out);
    /*
     * Above should only access the first direct block pointer, which cannot
     * fail.
     */
    MUST_SUCCEED(ret);
    goto out;
fail:
out:
    heap_free(blkdata);
    return ret;
}

static void closeinode(struct inocontext *self) {
    if (self == NULL) {
        return;
    }
    heap_free(self->blockbuf.buf);
    heap_free(self->singlyindirectbuf.buf);
    heap_free(self->doublyindirectbuf.buf);
    heap_free(self->triplyindirectbuf.buf);
}

struct directory {
    struct DIR dir;
    struct inocontext inocontext;
};

// Returns -ENOENT when it reaches end of the directory.
static WARN_UNUSED_RESULT int readdirectory(struct dirent *out, DIR *self) {
    struct directory *dir = self->data;
    int ret = 0;
    while(1) {
        uint8_t header[8];
        memset(out, 0, sizeof(*out));
        ret = read_inode(&dir->inocontext, header, 8);
        if (ret < 0) {
            goto fail;
        }
        out->d_ino       = uint32leat(&header[0x0]);
        size_t entrysize = uint16leat(&header[0x4]);
        size_t namelen   = header[0x6];
        if (!(dir->inocontext.fs->requiredfeatures &
            REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPE_FIELD))
        {
            // YJK/OS does not support names longer than 255 characters.
            if(header[0x7] != 0) {
                ret = -ENAMETOOLONG;
                goto fail;
            }
        }
        ret = read_inode(
            &dir->inocontext, out->d_name, namelen);
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

static WARN_UNUSED_RESULT int opendirectory(
    DIR **dir_out, struct fscontext *self, ino_t inode)
{
    int ret = 0;
    struct directory *dir = heap_alloc(
        sizeof(*dir), HEAP_FLAG_ZEROMEMORY);
    if (dir == NULL) {
        goto fail;
    }
    ret = openinode(&dir->inocontext, self, inode);
    if (ret < 0) {
        goto fail_after_alloc;
    }
    if ((dir->inocontext.typeandpermissions & INODE_TYPE_MASK)
        != INODE_TYPE_DIRECTORY)
    {
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
    heap_free(dir);
fail:
out:
    return ret;
}

static void closedirectory(DIR *self) {
    if (self == NULL) {
        return;
    }
    struct directory *dir = self->data;
    closeinode(&dir->inocontext);
    heap_free(dir);
}

static WARN_UNUSED_RESULT int openfile(
    struct inocontext *out, struct fscontext *self, ino_t inode)
{
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

static void closefile(struct inocontext *self) {
    if (self == NULL) {
        return;
    }
    closeinode(self);
}

static WARN_UNUSED_RESULT int resolve_path(
    ino_t *ino_out, struct fscontext *self, ino_t parent, char const *path)
{
    int ret = 0;
    DIR *dir;
    ino_t currentino = parent;
    char namebuf[NAME_MAX + 1];
    char const *remainingpath = path;
    while(*remainingpath != '\0') {
        char *nextslash = strchr(remainingpath, '/');
        char const *name;
        char const *newremainingpath;
        if (nextslash == NULL) {
            name = remainingpath;
            newremainingpath = strchr(path, '\0');
        } else {
            size_t len = nextslash - remainingpath;
            if (NAME_MAX < len) {
                ret = -ENAMETOOLONG;
                goto fail;
            }
            memcpy(namebuf, remainingpath, len);
            name = namebuf;
            namebuf[len] = '\0';
            newremainingpath = &nextslash[1];
        }
        if (name[0] != '\0') {
            ret = opendirectory(&dir, self, currentino);
            if (ret < 0) {
                goto fail;
            }
            while (1) {
                struct dirent ent;
                ret = readdirectory(&ent, dir);
                if (ret == -ENOENT) {
                    ret = -ENOENT;
                    break;
                }
                if (ret < 0) {
                    break;
                }
                currentino = ent.d_ino;
                if (strcmp(name, ent.d_name) == 0) {
                    ret = 0;
                    break;
                }
            }
            closedirectory(dir);
            if (ret < 0) {
                goto fail;
            }
        }
        remainingpath = newremainingpath;
    }
    *ino_out = currentino;
    goto out;
fail:
out:
    return ret;
}

struct openfdcontext {
    struct inocontext inocontext;
    struct fd fd;
    off_t cursorpos;
};

WARN_UNUSED_RESULT static ssize_t fd_op_read(
    struct fd *self, void *buf, size_t len)
{
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

WARN_UNUSED_RESULT static ssize_t fd_op_write(
    struct fd *self, void const *buf, size_t len)
{
    assert(len <= STREAM_MAX_TRANSFER_SIZE);
    (void)self;
    (void)buf;
    return -EIO;
}

static WARN_UNUSED_RESULT int fd_op_seek(
    struct fd *self, off_t offset, int whence)
{
    struct openfdcontext *context = self->data;
    int ret = seekinode(&context->inocontext, offset, whence);
    assert(ret != -ENOENT);
    return ret;
}

static void fd_op_close(struct fd *self) {
    struct openfdcontext *context = self->data;
    vfs_unregisterfile(self);
    closefile(&context->inocontext);
    heap_free(context);
}

static struct fd_ops const FD_OPS = {
    .read  = fd_op_read,
    .write = fd_op_write,
    .seek  = fd_op_seek,
    .close = fd_op_close,
};

static WARN_UNUSED_RESULT int vfs_op_mount(
    struct vfs_fscontext **out, struct ldisk *disk)
{
    int ret = 0;
    uint8_t superblk[1024];
    struct fscontext *context =
        heap_alloc(sizeof(*context), HEAP_FLAG_ZEROMEMORY);
    //--------------------------------------------------------------------------
    // Read superblock
    //--------------------------------------------------------------------------
    {
        assert(1024 % disk->physdisk->blocksize == 0);
        off_t blockoffset = 1024 / disk->physdisk->blocksize;
        blkcnt_t blkcount = 1024 / disk->physdisk->blocksize;
        ret = ldisk_read_exact(
            disk, superblk, blockoffset,
            blkcount);
        if (ret < 0) {
            goto fail;
        }
    }
    context->signature = uint16leat(&superblk[0x038]);
    if (context->signature != EXT2_SIGNATURE) {
        iodev_printf(
            &disk->iodev, "ext2: invalid superblk signature\n");
        ret = -EINVAL;
        goto fail;
    }
    context->disk                      = disk;
    context->totalinodes               = uint32leat(&superblk[0x000]);
    context->totalblks                 = uint32leat(&superblk[0x004]);
    context->reservedblksforsu         = uint32leat(&superblk[0x008]);
    context->totalunallocatedblocks    = uint32leat(&superblk[0x00c]);
    context->totalunallocatedinodes    = uint32leat(&superblk[0x010]);
    context->superblkblknum            = uint32leat(&superblk[0x014]);
    uint32_t blocksize_raw             = uint32leat(&superblk[0x018]);
    if (21 < blocksize_raw) {
        iodev_printf(
            &disk->iodev, "ext2: block size value is too large\n");
        ret = -EINVAL;
        goto fail;
    }
    context->blocksize                 = (blksize_t)(1024UL << blocksize_raw);
    context->blksinblkgroup            = uint32leat(&superblk[0x020]);
    context->inodesinblkgroup          = uint32leat(&superblk[0x028]);
    context->lastmounttime             = uint32leat(&superblk[0x02c]);
    context->laswrittentime            = uint32leat(&superblk[0x030]);
    context->mountssincelastfsck       = uint16leat(&superblk[0x034]);
    context->mountsbeforefsckrequired  = uint16leat(&superblk[0x036]);
    context->fsstate                   = uint16leat(&superblk[0x03a]);
    context->erraction                 = uint16leat(&superblk[0x03c]);
    context->minorver                  = uint16leat(&superblk[0x03e]);
    context->lastfscktime              = uint32leat(&superblk[0x040]);
    context->fsckinterval              = uint32leat(&superblk[0x044]);
    context->creatorosid               = uint32leat(&superblk[0x048]);
    context->majorver                  = uint32leat(&superblk[0x04c]);
    context->reservedblkuid            = uint16leat(&superblk[0x050]);
    context->reservedblkgid            = uint16leat(&superblk[0x052]);

    if (1 <= context->majorver) {
        context->firstnonreservedinode = uint16leat(&superblk[0x054]);
        context->inodesize             = uint16leat(&superblk[0x058]);
        context->blkgroup              = uint16leat(&superblk[0x05a]);
        context->optionalfeatures      = uint32leat(&superblk[0x05c]);
        context->requiredfeatures      = uint32leat(&superblk[0x060]);
        context->requiredfeatures_rw   = uint32leat(&superblk[0x064]);
        memcpy(
            context->filesystemid,  &superblk[0x068],
            sizeof(context->filesystemid));
        memcpy(
            context->volumename,   
            &superblk[0x078], sizeof(context->volumename));
        memcpy(
            context->lastmountpath, &superblk[0x088],
            sizeof(context->lastmountpath));
        bool notterminated = false;
        if (context->volumename[sizeof(context->volumename) - 1] != '\0') {
            context->volumename[sizeof(context->volumename) - 1] = '\0';
            notterminated = true;
        }
        if (context->lastmountpath[sizeof(context->lastmountpath) - 1] != '\0')
        {
            context->lastmountpath[sizeof(context->lastmountpath) - 1] = '\0';
            notterminated = true;
        }
        if (notterminated) {
            iodev_printf(
                &disk->iodev,
                "ext2: some strings in superblock were not terminated - terminating at the last character\n");

        }
        context->compressionalgorithms = uint32leat(&superblk[0x0c8]);
        context->preallocatefileblks   = superblk[0x0cc];
        context->preallocatedirblks    = superblk[0x0cd];
        memcpy(
            context->journalid, &superblk[0x0d0],
            sizeof(context->journalid));
        context->journalinode          = uint32leat(&superblk[0x0e0]);
        context->journaldevice         = uint32leat(&superblk[0x0e4]);
        context->orphaninodelisthead   = uint32leat(&superblk[0x0e8]);
    } else {
        context->firstnonreservedinode = 11;
        context->inodesize = 128;
    }
    uint8_t const *id = context->filesystemid;
    iodev_printf(
        &disk->iodev,
        "ext2 V%u-%02u, ID: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
        context->majorver, context->minorver,
        id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9],
        id[10], id[11], id[12], id[13], id[14], id[15]);

    size_t blkgroupcount = sizetoblocks(
        context->totalblks, context->blksinblkgroup);
    size_t blkgroupcount2 = sizetoblocks(
        context->totalinodes, context->inodesinblkgroup);
    if (blkgroupcount != blkgroupcount2) {
        iodev_printf(
            &disk->iodev,
            "Two calculated blk group count does not match: %zu != %zu\n",
            blkgroupcount, blkgroupcount2);
    }
    context->blkgroupcount = blkgroupcount;
    if (context->blocksize == 1024) {
        //  0        1024        2048         3072
        //  |----------|-----------|------------|---
        //    Block 0     Block 1     Block 2   
        //              SSSSSSSSSSS BBBBBBBBBBBBBBBB
        //              |           |
        // Superblock --+           |
        // BGDT --------------------+
        context->blkgroupdescriptorblk = 2;
    } else {
        //  0        1024      blocksize
        //  |----------------------|----------------
        //          Block 0        |       Block 1
        //              SSSSSSSSSSS BBBBBBBBBBBBBBBB
        //              |           |
        // Superblock --+           |
        // BGDT --------------------+
        context->blkgroupdescriptorblk = 1;
    }

    //--------------------------------------------------------------------------
    // Check feature flags
    //--------------------------------------------------------------------------
    if (context->requiredfeatures & ~SUPPORTED_REQUIRED_FLAGS) {
        iodev_printf(
            &disk->iodev,
            "ext2: found unsupported required features(flag %x)\n",
            context->requiredfeatures & ~SUPPORTED_REQUIRED_FLAGS);
        ret = -EINVAL;
        goto fail;
    }
    if (context->requiredfeatures_rw & ~SUPPORTED_RWMOUNT_FLAGS) {
        iodev_printf(
            &disk->iodev,
            "ext2: found unsupported required features for R/W mount(flag %x)\n",
            context->requiredfeatures_rw & ~SUPPORTED_RWMOUNT_FLAGS);
        ret = -EINVAL;
        goto fail;
    }
    context->vfs_fscontext.data = context;
    *out = &context->vfs_fscontext;
    goto out;
fail:
    heap_free(context);
out:
    return ret;
}

static  WARN_UNUSED_RESULT int vfs_op_umount(struct vfs_fscontext *self) {
    heap_free(self->data);
    return 0;
}

static WARN_UNUSED_RESULT int vfs_op_open(
    struct fd **out, struct vfs_fscontext *self, char const *path, int flags)
{
    int ret;
    ino_t inode;
    struct fscontext *fscontext = self->data;
    (void)flags;
    struct openfdcontext *fdcontext = heap_alloc(
        sizeof(*fdcontext), HEAP_FLAG_ZEROMEMORY);
    ret = resolve_path(
        &inode, fscontext, INODE_ROOTDIRECTORY, path);
    if (ret < 0) {
        goto fail_after_alloc;
    }
    ret = openfile(&fdcontext->inocontext, fscontext, inode);
    if (ret < 0) {
        goto fail_after_alloc;
    }
    ret = vfs_registerfile(
        &fdcontext->fd, &FD_OPS, self, fdcontext);
    if (ret < 0) {
        goto fail_after_open;
    }
    *out = &fdcontext->fd;
    goto out;
fail_after_open:
    closefile(&fdcontext->inocontext);
fail_after_alloc:
    heap_free(fdcontext);
out:
    return ret;
}

static WARN_UNUSED_RESULT int vfs_op_opendir(
    DIR **out, struct vfs_fscontext *self, char const *path)
{
    int ret;
    ino_t inode;
    struct fscontext *fscontext = self->data;
    ret = resolve_path(
        &inode, fscontext, INODE_ROOTDIRECTORY, path);
    if (ret < 0) {
        goto fail;
    }
    ret = opendirectory(out, fscontext, inode);
    if (ret < 0) {
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int vfs_op_closedir(DIR *self) {
    closedirectory(self);
    return 0;
}

WARN_UNUSED_RESULT static int vfs_op_readdir(struct dirent *out, DIR *self) {
    return readdirectory(out, self);
}

static struct vfs_fstype_ops const FSTYPE_OPS = {
    .mount    = vfs_op_mount,
    .umount   = vfs_op_umount,
    .open     = vfs_op_open,
    .opendir  = vfs_op_opendir,
    .closedir = vfs_op_closedir,
    .readdir  = vfs_op_readdir,
};

static struct vfs_fstype s_fstype;

void fsinit_init_ext2(void) {
    vfs_registerfstype(&s_fstype, "ext2", &FSTYPE_OPS);
}
