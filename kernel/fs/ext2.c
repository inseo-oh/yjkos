#include "fsinit.h"
#include <assert.h>
#include <dirent.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/vfs.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

static uint16_t const EXT2_SIGNATURE = 0xef53;

static uint16_t const FSSTATE_CLEAN = 1;
static uint16_t const FSSTATE_ERROR = 2;

static uint16_t const ERRACTION_IGNORE     = 1;
static uint16_t const ERRACTION_REMOUNT_RO = 2;
static uint16_t const ERRACTION_PANIC      = 3;

static uint32_t const REQUIRED_FEATUREFLAG_COMPRESSION                   = 1 << 0;
static uint32_t const REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPEFIELD   = 1 << 1; // Directory entries contain a type field
static uint32_t const REQUIRED_FEATUREFLAG_NEED_REPLAY_JOURNAL           = 1 << 2; // Needs to replay its journal
static uint32_t const REQUIRED_FEATUREFLAG_JOURNAL_DEVICE_USED           = 1 << 3; // Uses a journal device

static uint32_t const RWMOUNT_FEATUREFLAG_SPARSE_SUPERBLOCK_AND_GDTABLE  = 1 << 0; // Sparse superblocks and group descriptor tables
static uint32_t const RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE                = 1 << 1; // 64-bit file size
static uint32_t const RWMOUNT_FEATUREFLAG_BINARY_TREE_DIR                = 1 << 2; // Directory contents are stored in the form of a Binary Tree

static uint32_t const SUPPORTED_REQUIRED_FLAGS = REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPEFIELD;
static uint32_t const SUPPORTED_RWMOUNT_FLAGS  = RWMOUNT_FEATUREFLAG_SPARSE_SUPERBLOCK_AND_GDTABLE | RWMOUNT_FEATUREFLAG_64BIT_FILE_SIZE;

static ino_t const INODE_ROOTDIRECTORY = 2;

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
    uint32_t directblkptrs[12];
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
    size_t nextdirectptrindex;
    size_t cnt;

    struct {
        off_t offset_in_buf;
        uint8_t *buf;
    } singlyindirectbuf, doublyindirectbuf, triplyindirectbuf, blockbuf;
    bool singlyindirectused : 1;
    bool doublyindirectused : 1;
    bool triplyindirectused : 1;
};

// Bitmask values for typeandpermissions
static uint16_t const INODE_TYPE_MASK          = 0xf000;
static uint16_t const INODE_TYPE_FIFO          = 0x1000;
static uint16_t const INODE_TYPE_CHARACTER     = 0x2000;
static uint16_t const INODE_TYPE_DIRECTORY     = 0x4000;
static uint16_t const INODE_TYPE_BLOCK_DEVICE  = 0x6000;
static uint16_t const INODE_TYPE_REGULAR_FILE  = 0x8000;
static uint16_t const INODE_TYPE_SYMBOLIC_LINK = 0xa000;
static uint16_t const INODE_TYPE_UNIX_SOCKET   = 0xc000;

// `out` must be able to hold `blkcount * self->blocksize` bytes.
static FAILABLE_FUNCTION readblocks(struct fscontext *self, void *buf, uint32_t blockaddr, blkcnt_t blkcount) {
FAILABLE_PROLOGUE
    // TODO: support cases where self->blocksize < self->disk->physdisk->blocksize
    assert((self->blocksize % self->disk->physdisk->blocksize) == 0);
    diskblkptr diskblockaddr = blockaddr * (self->blocksize / self->disk->physdisk->blocksize);
    blkcnt_t diskblkcount = blkcount * (self->blocksize / self->disk->physdisk->blocksize);
    TRY(ldisk_read_exact(self->disk, buf, diskblockaddr, diskblkcount));
    
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION allocblockbuf(uint8_t **out, struct fscontext *self, blkcnt_t count, uint8_t flags) {
FAILABLE_PROLOGUE
    uint8_t *buf = heap_calloc(count, self->blocksize, flags);
    if (buf == NULL) {
        THROW(ERR_NOMEM);
    }
    *out = buf;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION readblocks_alloc(uint8_t **out, struct fscontext *self, uint32_t blockaddr, blkcnt_t blkcount) {
FAILABLE_PROLOGUE
    uint8_t *buf = NULL;
    TRY(allocblockbuf(&buf, self, blkcount, 0));
    TRY(readblocks(self, buf, blockaddr, blkcount));
    *out = buf;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(buf);
    }
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION readblockgroupdescriptor(struct blkgroupdescriptor *out, struct fscontext *self, uint32_t blockgroup) {
FAILABLE_PROLOGUE
    enum {
        DESCRIPTOR_SIZE = 32
    };
    assert(blockgroup < (SIZE_MAX / DESCRIPTOR_SIZE));
    off_t byteoffset = blockgroup * DESCRIPTOR_SIZE;
    uint32_t blockoffset = byteoffset / self->blocksize;
    off_t byteoffsetinblk = byteoffset % self->blocksize;
    assert(blockoffset < (SIZE_MAX - self->blkgroupdescriptorblk));
    blockoffset += self->blkgroupdescriptorblk;

    uint8_t *buf = NULL;
    TRY(readblocks_alloc(&buf, self, blockoffset, 1));
    uint8_t *data = &buf[byteoffsetinblk];
    out->blkusagebitmap    = uint32leat(&data[0x00]);
    out->inodeusagebitmap  = uint32leat(&data[0x04]);
    out->inodetable        = uint32leat(&data[0x08]);
    out->unallocatedblocks = uint16leat(&data[0x0c]);
    out->unallocatedinodes = uint16leat(&data[0x0e]);
    out->directories       = uint16leat(&data[0x10]);
FAILABLE_EPILOGUE_BEGIN
    heap_free(buf);
FAILABLE_EPILOGUE_END
}

static uint32_t blockgroup_of_inode(struct fscontext *self, ino_t inodeaddr) {
    return (inodeaddr - 1) / self->inodesinblkgroup;
}

static FAILABLE_FUNCTION locateinode(uint32_t *blk_out, off_t *off_out, struct fscontext *self, ino_t inodeaddr) {
FAILABLE_PROLOGUE
    struct blkgroupdescriptor blkgroup;
    TRY(readblockgroupdescriptor(&blkgroup, self, blockgroup_of_inode(self, inodeaddr)));
    off_t index = (inodeaddr - 1) % self->inodesinblkgroup;
    assert(index < (SIZE_MAX / self->inodesize));
    *blk_out = blkgroup.inodetable + ((index * self->inodesize) / self->blocksize);
    *off_out = (index * self->inodesize) % self->blocksize;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION nextinodeblock(struct inocontext *self) {
FAILABLE_PROLOGUE
    enum {
        DIRECT_BLOCK_POINTER_COUNT = sizeof(self->directblkptrs) / sizeof(*self->directblkptrs)
    };
    uint32_t resultaddr;
    if (self->nextdirectptrindex < DIRECT_BLOCK_POINTER_COUNT) {
        // We can use direct blk pointer
        resultaddr = self->directblkptrs[self->nextdirectptrindex];
        if (resultaddr == 0) {
            THROW(ERR_EOF);
        }
        self->nextdirectptrindex++;
    } else {
        if ((self->singlyindirectbuf.buf == NULL) || (self->fs->blocksize <= self->singlyindirectbuf.offset_in_buf)) {
            // We need to move to the next singly indirect table.
            uint32_t tableaddr;
            if (!self->singlyindirectused) {
                // We are using singly indirect table for the first time
                tableaddr = self->singlyindirecttable;
            } else {
                // read the next pointer from doubly indirect table
                if ((self->doublyindirectbuf.buf == NULL) || (self->fs->blocksize <= self->doublyindirectbuf.offset_in_buf)) {
                    // We need to move to next doubly indirect table
                    uint32_t tableaddr;
                    if (!self->doublyindirectused) {
                        // We are using doubly indirect table for the first time
                        tableaddr = self->doublyindirecttable;
                    } else {
                        /////////////////
                        // read the next pointer from triply indirect table
                        if ((self->triplyindirectbuf.buf == NULL) || (self->fs->blocksize <= self->triplyindirectbuf.offset_in_buf)) {
                            // We need to move to next triply indirect table
                            uint32_t tableaddr;
                            if (!self->triplyindirectused) {
                                // We are using triply indirect table for the first time
                                tableaddr = self->triplyindirecttable;
                            } else {
                                iodev_printf(&self->fs->disk->iodev, "File is too large\n");
                                THROW(ERR_EOF);
                            }
                            if (tableaddr == 0) {
                                heap_free(self->triplyindirectbuf.buf);
                                self->triplyindirectbuf.buf = NULL;
                                self->triplyindirectbuf.offset_in_buf = 0;
                                THROW(ERR_EOF);
                            }
                            uint8_t *newtable = NULL;
                            TRY(readblocks_alloc(&newtable, self->fs, tableaddr, 1));
                            heap_free(self->triplyindirectbuf.buf);
                            self->triplyindirectbuf.buf = newtable;
                            self->triplyindirectbuf.offset_in_buf = 0;
                        }
                        self->triplyindirectused = true;
                        tableaddr = uint32leat(&self->triplyindirectbuf.buf[self->triplyindirectbuf.offset_in_buf]);
                        self->triplyindirectbuf.offset_in_buf += sizeof(uint32_t);
                        /////////////////
                    }
                    if (tableaddr == 0) {
                        heap_free(self->doublyindirectbuf.buf);
                        self->doublyindirectbuf.buf = NULL;
                        self->doublyindirectbuf.offset_in_buf = 0;
                        THROW(ERR_EOF);
                    }
                    uint8_t *newtable = NULL;
                    TRY(readblocks_alloc(&newtable, self->fs, tableaddr, 1));
                    heap_free(self->doublyindirectbuf.buf);
                    self->doublyindirectbuf.buf = newtable;
                    self->doublyindirectbuf.offset_in_buf = 0;
                }
                self->doublyindirectused = true;
                tableaddr = uint32leat(&self->doublyindirectbuf.buf[self->doublyindirectbuf.offset_in_buf]);
                self->doublyindirectbuf.offset_in_buf += sizeof(uint32_t);
            }
            if (tableaddr == 0) {
                heap_free(self->singlyindirectbuf.buf);
                self->singlyindirectbuf.buf = NULL;
                self->singlyindirectbuf.offset_in_buf = 0;
                THROW(ERR_EOF);
            }
            uint8_t *newtable = NULL;
            TRY(readblocks_alloc(&newtable, self->fs, tableaddr, 1));
            heap_free(self->singlyindirectbuf.buf);
            self->singlyindirectbuf.buf = newtable;
            self->singlyindirectbuf.offset_in_buf = 0;
        }
        self->singlyindirectused = true;
        resultaddr = uint32leat(&self->singlyindirectbuf.buf[self->singlyindirectbuf.offset_in_buf]);
        if (resultaddr == 0) {
            THROW(ERR_EOF);
        }
        self->singlyindirectbuf.offset_in_buf += sizeof(uint32_t);
    }
    self->currentblockaddr = resultaddr;

    self->cnt++;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static void rewindinode(struct inocontext *self) {
    heap_free(self->blockbuf.buf);
    heap_free(self->singlyindirectbuf.buf);
    heap_free(self->doublyindirectbuf.buf);
    heap_free(self->triplyindirectbuf.buf);
    memset(&self->blockbuf, 0, sizeof(self->blockbuf));
    memset(&self->singlyindirectbuf, 0, sizeof(self->singlyindirectbuf));
    memset(&self->doublyindirectbuf, 0, sizeof(self->doublyindirectbuf));
    memset(&self->triplyindirectbuf, 0, sizeof(self->triplyindirectbuf));
    self->currentblockaddr = 0;
    self->nextdirectptrindex = 0;
    self->singlyindirectused = false;
    self->doublyindirectused = false;
    self->triplyindirectused = false;
    self->cnt = 0;
    // Move to the very first block
    {
        status_t status = nextinodeblock(self);
        // Above should only access the first direct block pointer, which cannot fail.
        assert(status == OK);
        (void)status;
    }
}

static FAILABLE_FUNCTION skipreadinode(struct inocontext *self, size_t len) {
FAILABLE_PROLOGUE
    size_t remaininglen = len;

    while(remaininglen != 0) {
        if (self->fs->blocksize <= self->blockbuf.offset_in_buf) {
            // We've ran out of current block, so we need to move to the next block
            TRY(nextinodeblock(self));
            // Invalidate old buffer
            heap_free(self->blockbuf.buf);
            self->blockbuf.buf = NULL;
            self->blockbuf.offset_in_buf = 0;
        }
        if ((self->blockbuf.offset_in_buf == 0) && (self->fs->blocksize <= remaininglen)) {
            blkcnt_t blkcount = remaininglen / self->fs->blocksize;
            for (blkcnt_t i = 0; i < blkcount; i++) {
                TRY(nextinodeblock(self));
            }
            size_t skiplen = self->fs->blocksize * blkcount;
            remaininglen -= skiplen;
            heap_free(self->blockbuf.buf);
            self->blockbuf.buf = NULL;
        }
        if (remaininglen == 0) {
            break;
        }
        assert(self->blockbuf.offset_in_buf < self->fs->blocksize);

        if (self->blockbuf.buf == NULL) {
            uint8_t *newbuf = NULL;
            TRY(readblocks_alloc(&newbuf, self->fs, self->currentblockaddr, 1));
            self->blockbuf.buf = newbuf;
        }
        blkcnt_t maxlen = self->fs->blocksize - self->blockbuf.offset_in_buf;
        blkcnt_t skiplen = remaininglen;
        if (maxlen < skiplen) {
            skiplen = maxlen;
        }
        assert(skiplen != 0);
        self->blockbuf.offset_in_buf += skiplen;
        remaininglen -= skiplen;
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION seekinode(struct inocontext *self, off_t offset, int whence) {
FAILABLE_PROLOGUE
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
                TRY(skipreadinode(self, skiplen));
                remaining -= skiplen;
            }
            break;
        }
        case SEEK_CUR:
        case SEEK_END:
            assert(!"TODO");
            break;
        default:
            THROW(ERR_INVAL);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION readinode(struct inocontext *self, void *buf, size_t len) {
FAILABLE_PROLOGUE
    size_t remaininglen = len;
    uint8_t *dest = buf;

    while(remaininglen != 0) {
        if (self->fs->blocksize <= self->blockbuf.offset_in_buf) {
            // We've ran out of current block, so we need to move to the next block
            TRY(nextinodeblock(self));
            // Invalidate old buffer
            heap_free(self->blockbuf.buf);
            self->blockbuf.buf = NULL;
            self->blockbuf.offset_in_buf = 0;
        }
        // read as much blocks as we can, directly to the destination.
        if ((self->blockbuf.offset_in_buf == 0) && (self->fs->blocksize <= remaininglen)) {
            blkcnt_t blkcount = remaininglen / self->fs->blocksize;
            // Blocks may not be contiguous on ext2, but if we can, it's faster to read as much sectors at once.
            uint32_t lastbase = self->currentblockaddr;
            size_t contiguouslen = 1;
            for (blkcnt_t i = 0; i < (blkcount - 1); i++) {
                TRY(nextinodeblock(self));
                if (self->currentblockaddr != lastbase + contiguouslen) {
                    TRY(readblocks(self->fs, dest, lastbase, contiguouslen));
                    size_t readsize = self->fs->blocksize * contiguouslen;
                    dest += readsize;
                    remaininglen -= readsize;
                    contiguouslen = 1;
                    lastbase = self->currentblockaddr;
                } else {
                    contiguouslen++;
                }
            }
            TRY(readblocks(self->fs, dest, lastbase, contiguouslen));
            TRY(nextinodeblock(self));
            size_t readsize = self->fs->blocksize * contiguouslen;
            dest += readsize;
            remaininglen -= readsize;
            heap_free(self->blockbuf.buf);
            self->blockbuf.buf = NULL;
        }
        if (remaininglen == 0) {
            break;
        }
        assert(self->blockbuf.offset_in_buf < self->fs->blocksize);

        if (self->blockbuf.buf == NULL) {
            // We don't have valid blk buffer - Let's buffer a block
            uint8_t *newbuf = NULL;
            TRY(readblocks_alloc(&newbuf, self->fs, self->currentblockaddr, 1));
            self->blockbuf.buf = newbuf;
        }
        // read from current buffered blk data, as much as we can.
        blkcnt_t maxlen = self->fs->blocksize - self->blockbuf.offset_in_buf;
        blkcnt_t readlen = remaininglen;
        if (maxlen < readlen) {
            readlen = maxlen;
        }
        assert(readlen != 0);
        memcpy(dest, &self->blockbuf.buf[self->blockbuf.offset_in_buf], readlen);
        self->blockbuf.offset_in_buf += readlen;
        dest += readlen;
        remaininglen -= readlen;
    }

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION openinode(struct inocontext *out, struct fscontext *self, ino_t inode) {
FAILABLE_PROLOGUE
    uint32_t blockaddr;
    off_t offset;
    uint8_t *blkdata = NULL;
    TRY(locateinode(&blockaddr, &offset, self, inode));
    TRY(readblocks_alloc(&blkdata, self, blockaddr, 1));
    uint8_t *inodedata = &blkdata[offset];
    memset(out, 0, sizeof(*out));
    uint32_t sizel = 0, sizeh = 0;
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
        out->directblkptrs[i]       = uint32leat(&inodedata[0x28 + sizeof(*out->directblkptrs) * i]);
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
    out->size = ((uint64_t)sizeh << 32) | (uint64_t)sizel;
    // Move to the very first block
    {
        status_t status = nextinodeblock(out);
        // Above should only access the first direct block pointer, which cannot fail.
        assert(status == OK);
        (void)status;
    }

FAILABLE_EPILOGUE_BEGIN
    heap_free(blkdata);
FAILABLE_EPILOGUE_END
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

struct DIR {
    struct inocontext inocontext;
};

// Returns ERR_EOF when it reaches end of the directory.
static FAILABLE_FUNCTION readdirectory(struct dirent *out, DIR *self) {
FAILABLE_PROLOGUE
    while(1) {
        uint8_t header[8];
        memset(out, 0, sizeof(*out));
        TRY(readinode(&self->inocontext, header, 8));
        out->d_ino       = uint32leat(&header[0x0]);
        size_t entrysize = uint16leat(&header[0x4]);
        size_t namelen   = header[0x6];
        if (!(self->inocontext.fs->requiredfeatures & REQUIRED_FEATUREFLAG_DIRENTRY_CONTAINS_TYPEFIELD)) {
            // YJK/OS does not support name longer than 255 characters.
            if(header[0x7] != 0) {
                THROW(ERR_NAMETOOLONG);
            }
        }
        TRY(readinode(&self->inocontext, out->d_name, namelen));
        size_t readlen = namelen + sizeof(header);
        size_t skiplen = entrysize - readlen;
#if 1
        TRY(skipreadinode(&self->inocontext, skiplen));
#else
        {
            size_t remaininglen = skiplen;
            for (size_t i = 0; i < skiplen; i++) {
                uint8_t unused[16];
                size_t maxlen = sizeof(unused);
                size_t readlen = remaininglen;
                if (maxlen < readlen) {
                    readlen = maxlen;
                }
                TRY(readinode(&self->inocontext, unused, readlen));
                remaininglen -= readlen;
            }
        }
#endif
        if (out->d_ino != 0) {
            break;
        }
    }

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION opendirectory(DIR **dir_out, struct fscontext *self, ino_t inode) {
FAILABLE_PROLOGUE
    bool inodeopened = false;
    DIR *dir = heap_alloc(sizeof(*dir), HEAP_FLAG_ZEROMEMORY);
    if (dir == NULL) {
        THROW(ERR_NOMEM);
    }
    TRY(openinode(&dir->inocontext, self, inode));
    inodeopened = true;
    if ((dir->inocontext.typeandpermissions & INODE_TYPE_MASK) != INODE_TYPE_DIRECTORY) {
        THROW(ERR_NOTDIR);
    }
    *dir_out = dir;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        if (inodeopened) {
            closeinode(&dir->inocontext);
        }
        heap_free(dir);
    }
FAILABLE_EPILOGUE_END
}

static void closedirectory(DIR *self) {
    if (self == NULL) {
        return;
    }
    closeinode(&self->inocontext);
    heap_free(self);
}

static FAILABLE_FUNCTION openfile(struct inocontext *out, struct fscontext *self, ino_t inode) {
FAILABLE_PROLOGUE
    bool inodeopened = false;
    TRY(openinode(out, self, inode));
    inodeopened = true;
    if ((out->typeandpermissions & INODE_TYPE_MASK) == INODE_TYPE_DIRECTORY) {
        THROW(ERR_ISDIR);
    }
    inodeopened = true;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        if (inodeopened) {
            closeinode(out);
        }
    }
FAILABLE_EPILOGUE_END
}

static void closefile(struct inocontext *self) {
    if (self == NULL) {
        return;
    }
    closeinode(self);
}

static FAILABLE_FUNCTION resolvepath(ino_t *ino_out, struct fscontext *self, ino_t parent, char const *path) {
FAILABLE_PROLOGUE
    DIR *dir;
    bool diropened = false;
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
                THROW(ERR_NAMETOOLONG);
            }
            memcpy(namebuf, remainingpath, len);
            name = namebuf;
            namebuf[len] = '\0';
            newremainingpath = &nextslash[1];
        }
        if (name[0] != '\0') {
            TRY(opendirectory(&dir, self, currentino));
            diropened = true;
            bool found = false;
            while (1) {
                struct dirent ent;
                status_t status = readdirectory(&ent, dir);
                if (status == ERR_EOF) {
                    break;
                } else if (status != OK) {
                    THROW(status);
                }
                currentino = ent.d_ino;
                if (strcmp(name, ent.d_name) == 0) {
                    found = true;
                    break;
                }
            }
            closedirectory(dir);
            diropened = false;
            if (!found) {
                THROW(ERR_NOENT);
            }
        }
        remainingpath = newremainingpath;
    }
    *ino_out = currentino;
FAILABLE_EPILOGUE_BEGIN
    if (diropened) {
        closedirectory(dir);
    }
FAILABLE_EPILOGUE_END
}

struct openfdcontext {
    struct inocontext inocontext;
    struct fd fd;
    off_t cursorpos;
};

static FAILABLE_FUNCTION fd_op_read(struct fd *self, void *buf, size_t *len_inout) {
FAILABLE_PROLOGUE
    struct openfdcontext *context = self->data;
    off_t maxlen = context->inocontext.size - context->cursorpos;
    size_t readlen = *len_inout;
    if (maxlen < readlen) {
        readlen = maxlen;
    }
    TRY(readinode(&context->inocontext, buf, readlen));
    context->cursorpos += readlen;
    *len_inout = readlen;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION fd_op_write(struct fd *self, void const *buf, size_t *len_inout) {
FAILABLE_PROLOGUE
    (void)self;
    (void)buf;
    (void)len_inout;
    THROW(ERR_NOTSUP);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION fd_op_seek(struct fd *self, off_t offset, int whence) {
    struct openfdcontext *context = self->data;
    return seekinode(&context->inocontext, offset, whence);
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

static FAILABLE_FUNCTION vfs_op_mount(struct vfs_fscontext **out, struct ldisk *disk) {
FAILABLE_PROLOGUE
    uint8_t superblk[1024];
    struct fscontext *context = heap_alloc(sizeof(*context), HEAP_FLAG_ZEROMEMORY);
    //--------------------------------------------------------------------------
    // Read superblock
    //--------------------------------------------------------------------------
    {
        assert(1024 % disk->physdisk->blocksize == 0);
        off_t blockoffset = 1024 / disk->physdisk->blocksize;
        blkcnt_t blkcount = 1024 / disk->physdisk->blocksize;
        TRY(ldisk_read_exact(disk, superblk, blockoffset, blkcount));
    }
    context->signature = uint16leat(&superblk[0x038]);
    if (context->signature != EXT2_SIGNATURE) {
        iodev_printf(&disk->iodev, "ext2: invalid superblk signature\n");
        THROW(ERR_INVAL);
    }
    context->disk                      = disk;
    context->totalinodes               = uint32leat(&superblk[0x000]);
    context->totalblks                 = uint32leat(&superblk[0x004]);
    context->reservedblksforsu         = uint32leat(&superblk[0x008]);
    context->totalunallocatedblocks    = uint32leat(&superblk[0x00c]);
    context->totalunallocatedinodes    = uint32leat(&superblk[0x010]);
    context->superblkblknum            = uint32leat(&superblk[0x014]);
    uint32_t blksizeraw            = uint32leat(&superblk[0x018]);
    if (21 < blksizeraw) {
        iodev_printf(&disk->iodev, "ext2: block size value is too large\n");
        THROW(ERR_INVAL);
    }
    context->blocksize                   = 1024U << blksizeraw;
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
        memcpy(context->filesystemid,  &superblk[0x068], sizeof(context->filesystemid));
        memcpy(context->volumename,    &superblk[0x078], sizeof(context->volumename));
        memcpy(context->lastmountpath, &superblk[0x088], sizeof(context->lastmountpath));
        bool notterminated = false;
        if (context->volumename[sizeof(context->volumename) - 1] != '\0') {
            context->volumename[sizeof(context->volumename) - 1] = '\0';
            notterminated = true;
        }
        if (context->lastmountpath[sizeof(context->lastmountpath) - 1] != '\0') {
            context->lastmountpath[sizeof(context->lastmountpath) - 1] = '\0';
            notterminated = true;
        }
        if (notterminated) {
            iodev_printf(&disk->iodev, "ext2: some strings in superblk were not terminated - terminating at the last character\n");

        }
        context->compressionalgorithms = uint32leat(&superblk[0x0c8]);
        context->preallocatefileblks   = superblk[0x0cc];
        context->preallocatedirblks    = superblk[0x0cd];
        memcpy(context->journalid, &superblk[0x0d0], sizeof(context->journalid));
        context->journalinode          = uint32leat(&superblk[0x0e0]);
        context->journaldevice         = uint32leat(&superblk[0x0e4]);
        context->orphaninodelisthead   = uint32leat(&superblk[0x0e8]);
    } else {
        context->firstnonreservedinode = 11;
        context->inodesize = 128;
    }
    uint8_t const *id = context->filesystemid;
    iodev_printf(&disk->iodev, "ext2 V%u-%02u, ID: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", context->majorver, context->minorver, id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);

    size_t blkgroupcount = sizetoblocks(context->totalblks, context->blksinblkgroup);
    size_t blkgroupcount2 = sizetoblocks(context->totalinodes, context->inodesinblkgroup);
    if (blkgroupcount != blkgroupcount2) {
        iodev_printf(&disk->iodev, "Two calculated blk group count does not match: %zu != %zu\n", blkgroupcount, blkgroupcount2);
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
        iodev_printf(&disk->iodev, "ext2: found unsupported required features(flag %x)\n", context->requiredfeatures & ~SUPPORTED_REQUIRED_FLAGS);
        THROW(ERR_INVAL);
    }
    if (context->requiredfeatures_rw & ~SUPPORTED_RWMOUNT_FLAGS) {
        iodev_printf(&disk->iodev, "ext2: found unsupported required features for R/W mount(flag %x)\n", context->requiredfeatures_rw & ~SUPPORTED_RWMOUNT_FLAGS);
        THROW(ERR_INVAL);
    }
    context->vfs_fscontext.data = context;
    *out = &context->vfs_fscontext;

FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(context);
    }
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION vfs_op_umount(struct vfs_fscontext *self) {
FAILABLE_PROLOGUE
    heap_free(self->data);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION vfs_op_open(struct fd **out, struct vfs_fscontext *self, char const *path, int flags) {
FAILABLE_PROLOGUE
    bool fileopened = false;
    bool fileregistered = false;
    ino_t inode;
    struct fscontext *fscontext = self->data;
    (void)flags;
    struct openfdcontext *fdcontext = heap_alloc(sizeof(*fdcontext), HEAP_FLAG_ZEROMEMORY);
    fileopened = true;
    TRY(resolvepath(&inode, fscontext, INODE_ROOTDIRECTORY, path));
    TRY(openfile(&fdcontext->inocontext, fscontext, inode));
    fileopened = true;
    TRY(vfs_registerfile(&fdcontext->fd, &FD_OPS, self, fdcontext));
    fileregistered = true;
    *out = &fdcontext->fd;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        if (fileregistered) {
            vfs_unregisterfile(&fdcontext->fd);
        }
        if (fileopened) {
            closefile(&fdcontext->inocontext);
        }
        heap_free(fdcontext);
    }
FAILABLE_EPILOGUE_END
}

static struct vfs_fstype_ops const FSTYPE_OPS = {
    .mount  = vfs_op_mount,
    .umount = vfs_op_umount,
    .open   = vfs_op_open,
};

static struct vfs_fstype s_fstype;

void fsinit_init_ext2(void) {
    vfs_registerfstype(&s_fstype, "ext2", &FSTYPE_OPS);
}
