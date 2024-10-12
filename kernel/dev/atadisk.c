#include <kernel/dev/atadisk.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/tty.h>
#include <kernel/status.h>
#include <kernel/ticktime.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

enum {
    TIMEOUT     = 5000,
    MAX_RETRIES = 3,
};

static FAILABLE_FUNCTION waitirq(struct atadisk *disk) {
FAILABLE_PROLOGUE
    enum {
        STATUS_POLL_PERIOD = 10,
    };
    ticktime_type starttime = g_ticktime;
    ticktime_type lastchecktime = 0;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        if (STATUS_POLL_PERIOD <= (lastchecktime - g_ticktime)) {
            uint8_t diskstatus = disk->ops->readstatus(disk);
            if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
                THROW(ERR_IO);
            }
            lastchecktime = g_ticktime;
        }
        if (disk->ops->getirqflag(disk)) {
            disk->ops->clearirqflag(disk);
            ok = true;
            break;
        }
    }
    if (!ok) {
        THROW(ERR_IO);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION waitbusyclear(struct atadisk *disk) {
FAILABLE_PROLOGUE
    enum {
        STATUS_POLL_PERIOD = 10,
    };
    ticktime_type starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint8_t diskstatus = disk->ops->readstatus(disk);
        if (!(diskstatus & ATA_STATUSFLAG_BSY)) {
            ok = true;
            break;
        }
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
                THROW(ERR_IO);
            }
    }
    if (!ok) {
        THROW(ERR_IO);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION waitbusyclear_irq(struct atadisk *disk) {
FAILABLE_PROLOGUE
    while(1) {
        TRY(waitirq(disk));
        if (!(disk->ops->readstatus(disk) & ATA_STATUSFLAG_BSY)) {
            break;
        }
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION waitdrqset(struct atadisk *disk) {
FAILABLE_PROLOGUE
    ticktime_type starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint8_t diskstatus = disk->ops->readstatus(disk);
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            THROW(ERR_IO);
        }
        if (diskstatus & ATA_STATUSFLAG_DRQ) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        THROW(ERR_IO);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static void extractstring_from_identifydata(char *dest, struct ata_databuf *rawdata, size_t startoffset, size_t endoffset) {
    for (size_t i = startoffset; i <= endoffset; i++) {
        dest[(i - startoffset) * 2] = rawdata->data[i] >> 8;
        dest[(i - startoffset) * 2 + 1] = rawdata->data[i];
    }
}

struct identifyresult {
    char serial[41];
    char firmware[9];
    char modelnum[41];
};

static FAILABLE_FUNCTION identifydevice(struct identifyresult *out, struct atadisk *disk) {
FAILABLE_PROLOGUE
    TRY(disk->ops->selectdisk(disk));
    disk->ops->setlbaparam(disk, 0);
    disk->ops->issuecmd(disk, ATA_CMD_IDENTIFYDEVICE);
    uint8_t diskstatus = disk->ops->readstatus(disk);
    if (diskstatus == 0) {
        // Disk is not there
        THROW(ERR_NODEV);
    }
    TRY(waitbusyclear_irq(disk));

    // Wait for DRQ. waitdrqset() isn't used, because we also check LBA outputs while waiting.
    ticktime_type starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint32_t lba = disk->ops->getlbaoutput(disk);
        if ((lba & 0xffff00) != 0) {
            // Not an ATA device
            THROW(ERR_NODEV);
        }
        uint8_t diskstatus = disk->ops->readstatus(disk);
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            THROW(ERR_IO);
        }
        if (diskstatus & ATA_STATUSFLAG_DRQ) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        THROW(ERR_IO);
    }
    struct ata_databuf buffer;
    disk->ops->readdata(&buffer, disk);
    memset(out, 0, sizeof(*out));
    extractstring_from_identifydata(out->serial, &buffer, 10, 19);
    extractstring_from_identifydata(out->firmware, &buffer, 23, 26);
    extractstring_from_identifydata(out->modelnum, &buffer, 27, 46);

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION flushcache(struct atadisk *disk) {
FAILABLE_PROLOGUE
    TRY(disk->ops->selectdisk(disk));
    disk->ops->issuecmd(disk, ATA_CMD_FLUSHCACHE);
    TRY(waitbusyclear(disk));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION writesectors(struct atadisk *disk, uint32_t lba, size_t sectorcount, void const *buf) {
FAILABLE_PROLOGUE
    disk->ops->lock(disk);
    bool candma = disk->ops->dma_beginsession(disk);
    size_t remainingsectorcount = sectorcount;
    uint32_t currentlba = lba;
    uint8_t const *srcbuf = buf;

    while(remainingsectorcount > 0) {
        for (uint8_t try = 0; try < MAX_RETRIES; try++) {
            bool iscrcerror = false;
            bool dmainitialized = false;
            bool dmarunning = false;
            size_t currentsectorcount = remainingsectorcount;
            if (ATA_MAX_SECTORS_PER_TRANSFER < remainingsectorcount) {
                currentsectorcount = ATA_MAX_SECTORS_PER_TRANSFER;
            }
            // dma_inittransfer can do both write and read to srcbuf, depending on whether it's read or write.
            // so we have to manually cast to non-const pointer here, even though we are never writing to srcbuf.
            FAILABLE_STATUS_VAR = disk->ops->dma_inittransfer(disk, (uint8_t *)srcbuf, currentsectorcount * ATA_SECTOR_SIZE, false);
            if (FAILABLE_STATUS_VAR != OK) {
                iodev_printf(&disk->physdisk.iodev, "failed to initialize DMA transfer (error %d)\n", FAILABLE_STATUS_VAR);
                goto tryfailed;
            }
            dmainitialized = true;
            FAILABLE_STATUS_VAR = disk->ops->selectdisk(disk);
            if (FAILABLE_STATUS_VAR != OK) {
                iodev_printf(&disk->physdisk.iodev, "failed to select disk (error %d)\n", FAILABLE_STATUS_VAR);
                goto tryfailed;
            }
            disk->ops->setfeaturesparam(disk, 0);
            if (currentsectorcount == ATA_MAX_SECTORS_PER_TRANSFER) {
                disk->ops->setcountparam(disk, 0);
            } else {
                disk->ops->setcountparam(disk, currentsectorcount);
            }
            disk->ops->setlbaparam(disk, currentlba);
            if (candma) {
                // Use DMA
                disk->ops->issuecmd(disk, ATA_CMD_WRITEDMA);
                FAILABLE_STATUS_VAR = disk->ops->dma_begintransfer(disk);
                if (FAILABLE_STATUS_VAR != OK) {
                    iodev_printf(&disk->physdisk.iodev, "failed to start DMA transfer (error %d)\n", FAILABLE_STATUS_VAR);
                    goto tryfailed;
                }
                dmarunning = true;
                while (dmarunning) {
                    TRY(waitirq(disk));
                    enum ata_dmastatus dmastatus = disk->ops->dma_checktransfer(disk);
                    switch (dmastatus) {
                        case ATA_DMASTATUS_BUSY:
                            continue;
                        case ATA_DMASTATUS_SUCCESS:
                            dmarunning = false;
                            break;
                        case ATA_DMASTATUS_FAIL_UDMA_CRC:
                            FAILABLE_STATUS_VAR = ERR_IO;
                            iscrcerror = true;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_OTHER_IO:
                            FAILABLE_STATUS_VAR = ERR_IO;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_NOMEM:
                            THROW(ERR_NOMEM);
                    }
                }
                disk->ops->dma_endtransfer(disk, true);
                disk->ops->dma_deinittransfer(disk);
                srcbuf += currentsectorcount * ATA_SECTOR_SIZE;
            } else {
                // Use PIO
                disk->ops->issuecmd(disk, ATA_CMD_WRITESECTORS);
                for (size_t sector = 0; sector < currentsectorcount; sector++) {
                    struct ata_databuf buffer;
                    TRY(waitbusyclear(disk));
                    TRY(waitdrqset(disk));
                    for (size_t i = 0; i < 256; i++) {
                        buffer.data[i] = ((uint16_t)srcbuf[1] << 8) | srcbuf[0];
                        srcbuf += 2;
                    }
                    disk->ops->writedata(disk, &buffer);
                    TRY(waitbusyclear_irq(disk));
                }
            }
            remainingsectorcount -= currentsectorcount;
            currentlba += currentsectorcount;
            FAILABLE_STATUS_VAR = waitbusyclear(disk);
            if (FAILABLE_STATUS_VAR != OK) {
                iodev_printf(&disk->physdisk.iodev, "failed to wait for write to finish (error %d)\n", FAILABLE_STATUS_VAR);
                goto tryfailed;
            }
            FAILABLE_STATUS_VAR = flushcache(disk);
            if (FAILABLE_STATUS_VAR != OK) {
                iodev_printf(&disk->physdisk.iodev, "disk flush failed after writing(error %d)\n", FAILABLE_STATUS_VAR);
                goto tryfailed;
            }

            break;
        tryfailed:
            if (dmarunning) {
                disk->ops->dma_endtransfer(disk, false);
            }
            if (dmainitialized) {
                disk->ops->dma_deinittransfer(disk);
            }
            iodev_printf(&disk->physdisk.iodev, "I/O error occured (try %u/%u)\n", try + 1, MAX_RETRIES);
            if (!iscrcerror && candma) {
                // If we are using DMA, reset the disk to take it out of DMA mode, to be safe.
                iodev_printf(&disk->physdisk.iodev, "resetting disk\n");
                disk->ops->softreset(disk);
            }
            continue;
        }
    }

FAILABLE_EPILOGUE_BEGIN
    if (candma) {
        disk->ops->dma_endsession(disk);
    }
    disk->ops->unlock(disk);
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION readsectors(struct atadisk *disk, uint32_t lba, size_t sectorcount, void *buf) {
FAILABLE_PROLOGUE
    disk->ops->lock(disk);
    bool candma = disk->ops->dma_beginsession(disk);
    size_t remainingsectorcount = sectorcount;
    uint32_t currentlba = lba;
    uint8_t *destbuf = buf;

    while(remainingsectorcount > 0) {
        for (uint8_t try = 0; try < MAX_RETRIES; try++) {
            bool iscrcerror = false;
            bool dmainitialized = false;
            bool dmarunning = false;
            size_t currentsectorcount = remainingsectorcount;
            if (ATA_MAX_SECTORS_PER_TRANSFER < remainingsectorcount) {
                currentsectorcount = ATA_MAX_SECTORS_PER_TRANSFER;
            }
            FAILABLE_STATUS_VAR = disk->ops->dma_inittransfer(disk, destbuf, currentsectorcount * ATA_SECTOR_SIZE, true);
            if (FAILABLE_STATUS_VAR != OK) {
                iodev_printf(&disk->physdisk.iodev, "failed to initialize DMA transfer (error %d)\n", FAILABLE_STATUS_VAR);
                goto tryfailed;
            }
            dmainitialized = true;
            FAILABLE_STATUS_VAR = disk->ops->selectdisk(disk);
            if (FAILABLE_STATUS_VAR != OK) {
                iodev_printf(&disk->physdisk.iodev, "failed to select disk (error %d)\n", FAILABLE_STATUS_VAR);
                goto tryfailed;
            }
            disk->ops->setfeaturesparam(disk, 0);
            if (currentsectorcount == ATA_MAX_SECTORS_PER_TRANSFER) {
                disk->ops->setcountparam(disk, 0);
            } else {
                disk->ops->setcountparam(disk, currentsectorcount);
            }
            disk->ops->setlbaparam(disk, currentlba);
            if (candma) {
                // Use DMA
                disk->ops->issuecmd(disk, ATA_CMD_READDMA);
                FAILABLE_STATUS_VAR = disk->ops->dma_begintransfer(disk);
                if (FAILABLE_STATUS_VAR != OK) {
                    iodev_printf(&disk->physdisk.iodev, "failed to start DMA transfer (error %d)\n", FAILABLE_STATUS_VAR);
                    goto tryfailed;
                }
                dmarunning = true;
                while (dmarunning) {
                    TRY(waitirq(disk));
                    enum ata_dmastatus dmastatus = disk->ops->dma_checktransfer(disk);
                    switch (dmastatus) {
                        case ATA_DMASTATUS_BUSY:
                            continue;
                        case ATA_DMASTATUS_SUCCESS:
                            dmarunning = false;
                            break;
                        case ATA_DMASTATUS_FAIL_UDMA_CRC:
                            FAILABLE_STATUS_VAR = ERR_IO;
                            iscrcerror = true;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_OTHER_IO:
                            FAILABLE_STATUS_VAR = ERR_IO;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_NOMEM:
                            THROW(ERR_NOMEM);
                    }
                }
                disk->ops->dma_endtransfer(disk, true);
                disk->ops->dma_deinittransfer(disk);
                destbuf += currentsectorcount * ATA_SECTOR_SIZE;
            } else {
                // Use PIO
                disk->ops->issuecmd(disk, ATA_CMD_READSECTORS);
                for (size_t sector = 0; sector < currentsectorcount; sector++) {
                    struct ata_databuf buffer;
                    TRY(waitbusyclear_irq(disk));
                    TRY(waitdrqset(disk));
                    disk->ops->readdata(&buffer, disk);
                    for (size_t i = 0; i < 256; i++) {
                        *(destbuf++) = buffer.data[i];
                        *(destbuf++) = buffer.data[i] >> 8;
                    }
                }
            }
            remainingsectorcount -= currentsectorcount;
            currentlba += currentsectorcount;
            break;
        tryfailed:
            if (dmarunning) {
                disk->ops->dma_endtransfer(disk, false);
            }
            if (dmainitialized) {
                disk->ops->dma_deinittransfer(disk);
            }
            iodev_printf(&disk->physdisk.iodev, "I/O error occured (try %u/%u)\n", try + 1, MAX_RETRIES);
            if (!iscrcerror && candma) {
                // If we are using DMA, reset the disk to take it out of DMA mode, to be safe.
                iodev_printf(&disk->physdisk.iodev, "resetting disk\n");
                disk->ops->softreset(disk);
            }
            continue;
        }
    }
FAILABLE_EPILOGUE_BEGIN
    if (candma) {
        disk->ops->dma_endsession(disk);
    }
    disk->ops->unlock(disk);
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION op_read(struct pdisk *self, void *buf, size_t blockaddr, size_t blockcount) {
    struct atadisk *disk = self->data;
    return readsectors(disk, blockaddr, blockcount, buf);
}
static FAILABLE_FUNCTION op_write(struct pdisk *self, void const *buf, size_t blockaddr, size_t blockcount) {
    struct atadisk *disk = self->data;
    return writesectors(disk, blockaddr, blockcount, buf);
}

static struct pdisk_ops const OPS = {
    .read = op_read,
    .write = op_write,
};

FAILABLE_FUNCTION atadisk_register(struct atadisk *disk_out, struct atadisk_ops const *ops, void *data) {
FAILABLE_PROLOGUE
    memset(disk_out, 0, sizeof(*disk_out));
    disk_out->ops = ops;
    disk_out->data = data;
    struct identifyresult result;
    TRY(identifydevice(&result, disk_out));
    TRY(pdisk_register(&disk_out->physdisk, ATA_SECTOR_SIZE, &OPS, disk_out));
    iodev_printf(&disk_out->physdisk.iodev, "   model: %s\n", result.modelnum);
    iodev_printf(&disk_out->physdisk.iodev, "firmware: %s\n", result.firmware);
    iodev_printf(&disk_out->physdisk.iodev, "  serial: %s\n", result.serial);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}
