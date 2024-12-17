#include "raid.h"
#include "defs.h"
#include "fs.h"
#include "param.h"
#include "proc.h"

// What if multiple processes call raid_init?
static struct RAIDDevice raid_device;

void init_raid_device() {
    for (int i = VIRTIO_RAID_DISK_START; i < VIRTIO_RAID_DISK_END; i++) {
        initlock(&raid_device.disks[i].disk_lock, "disk_lock");
    }

    if (VIRTIO_RAID_DISK_END < 2) {
        printf("Warrning, there is not enoght disks for RAID system. Some "
               "initalization of RAID "
               "systems will fail.\n");
        return;
    }
    raid_device.is_init = -1; // Could change to array of init vals, for one use of disks metadata

    // TODO: Add if RAID system is valid or not (for example, if there is not
    // enough healty disks for RAID1 )
}

int formatOneDisk(uint64 disk_num, uchar* nullData) {
    if (disk_num < VIRTIO_RAID_DISK_START || disk_num > VIRTIO_RAID_DISK_END) {
        return -1;
    }
    for (int j = 0; j < 128 * 1024 * 1024 / BSIZE; j++)
        write_block(disk_num, j, nullData);
    return 0;
}

int formatDisks() {
    uchar data[BSIZE];
    memset(data, 0, BSIZE);
    for (int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
        if (formatOneDisk(i, data) == -1)
            return -1;
    }
    return 0;
}

int rw_block(struct RAIDSuperblock* currMetadata, uint64 block_num, uint64 p_buff, int isRead) {
    enum RAID_TYPE raid_type = currMetadata->raid_level;
    uint64 disk_num;
    uint64 blkc_num;
    uint64 blockPerDisk = (128 * 1024 * 1024) / BSIZE;

    switch (raid_type) {
    case RAID0:
        disk_num = (block_num % currMetadata->num_of_disks) + 1;
        blkc_num = (block_num / currMetadata->num_of_disks) + 1;
        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
        } else 
        {
            write_block(disk_num, blkc_num, (uchar*)p_buff);
        }
        break;
    case RAID1:
        disk_num = (block_num / (blockPerDisk - 1)) + 1;
        blkc_num = (block_num % (blockPerDisk - 1)) + 1;
        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
            read_block(disk_num + currMetadata->num_of_disks, blkc_num, (uchar*)p_buff); // Write into mirror disk
        } else {
            write_block(disk_num, blkc_num, (uchar*)p_buff);
            write_block(disk_num + currMetadata->num_of_disks, blkc_num, (uchar*)p_buff); // Write into mirror disk
        }
        break;
    case RAID0_1:
        // TODO: handle UNHEALTY DISK
        disk_num = (block_num % currMetadata->num_of_disks) + 1;
        blkc_num = ((block_num + currMetadata->num_of_disks) / currMetadata->num_of_disks);

        // TODO: handle when UNHEALTY to get copy of the data
        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
            // read_block(disk_num + currMetadata->num_of_disks, blkc_num,
            //            (uchar *)p_buff); // Write into mirror disk
        } else {
            write_block(disk_num, blkc_num, (uchar*)p_buff);
            // write_block(disk_num + currMetadata->num_of_disks, blkc_num,
            //             (uchar *)p_buff); // Write into mirror disk
        }
        break;
    case RAID4:
        // TODO: handle UNHEALTY DISK
        disk_num = (block_num % currMetadata->num_of_disks) + 1;
        blkc_num = (block_num / currMetadata->num_of_disks) + 1;
        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
            // TODO: Add write_block for parrity_disk
        } else {
            write_block(disk_num, blkc_num, (uchar*)p_buff);
            // TODO: Add write_block for parrity_disk
        }
        break;
    case RAID5:
        uint64 stripe_index = block_num / (currMetadata->num_of_disks - 1);
        uint64 stripe_offset = block_num % (currMetadata->num_of_disks - 1);
        uint64 parrity_index = (currMetadata->num_of_disks - 1) - stripe_index;
        disk_num = ((parrity_index + stripe_offset + 1) % (currMetadata->num_of_disks)) + 1;
        blkc_num = stripe_index + 1;
        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
            // TODO: Add write_block for parrity block
        } else {
            // printf("Writing to disk %d\n", disk_num);
            // printf("Writing to block %d\n\n", blkc_num);
            write_block(disk_num, blkc_num, (uchar*)p_buff);
            // TODO: Add write_block for parrity block
        }
        break;
    }
    return 0;
}

int raid_system_init(enum RAID_TYPE raid_type) {
    // I could add here formatDisks() to format all disks before init
    // TODO: decide if to format disks before init

    struct RAIDSuperblock* metadata = (struct RAIDSuperblock*)kalloc();
    metadata->num_of_disks = VIRTIO_RAID_DISK_END;
    metadata->raid_level = raid_type;
    metadata->blk_size = BSIZE;
    metadata->disk_status = HEALTHY;
    metadata->parrity_disk = -1;
    metadata->swap_disk = -1;
    uint64 blockPerDisk = (128 * 1024 * 1024) / BSIZE;
    switch (raid_type) {
    case RAID0:
        metadata->max_blknum = (blockPerDisk * VIRTIO_RAID_DISK_END) - (VIRTIO_RAID_DISK_END);
        break;

    case RAID1:
    case RAID0_1:
        // If there is no enough disks for RAID1 or RAID0_1
        if (VIRTIO_RAID_DISK_END == 1) {
            kfree(metadata);
            return -1;
        }
        metadata->num_of_disks = (VIRTIO_RAID_DISK_END) / 2;
        metadata->max_blknum =
            (blockPerDisk * metadata->num_of_disks) - metadata->num_of_disks;
        // Set hotswap disk if one disk fail
        if (((VIRTIO_RAID_DISK_END) & 1) == 1)
            metadata->swap_disk = VIRTIO_RAID_DISK_END;
        break;

    case RAID4:
        metadata->num_of_disks = VIRTIO_RAID_DISK_END - 1;
    case RAID5:
        if (VIRTIO_RAID_DISK_END == 2) {
            kfree(metadata);
            return -1;
        }
        metadata->max_blknum = blockPerDisk * (VIRTIO_RAID_DISK_END - 1);
        metadata->parrity_disk = VIRTIO_RAID_DISK_END;
        break;
    default:
        kfree(metadata);
        printf("Invalid RAID type\n");
        return -1;
    }
    for (int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
        metadata->disk_id = i;
        // Write into the first block of disk i
        write_block(i, 0, (uchar*)metadata);
    }
    kfree(metadata);
    return 0;
}

int raid_read_block(uint64 block_num, uint64 buffAddr) {
    // TODO: handle if it's not initailized

    // TODO: chage to read from the raid_device on initalization and not from
    // UNHEALTY
    uchar data[BSIZE];
    read_block(1, 0, data);
    struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;

    // TODO: Check if disk is healthy and it's initialized, possible will be
    // function itself

    // Check if the block number is valid
    if (block_num < 0 || block_num > currMetadata->max_blknum) {
        return -1;
    }

    struct proc* p = myproc();
    uint64 p_buff = walkaddr(p->pagetable, buffAddr) | (buffAddr & OFFSET_MASK);

    rw_block(currMetadata, block_num, p_buff, 1);

    return 0;
}

int raid_write_block(uint64 block_num, uint64 buffAddr) {
    // TODO: handle if it's not initailized

    // TODO: chage to read from the raid_device on initalization
    uchar data[BSIZE];
    read_block(1, 0, data);
    struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;
    // TODO: Check if disk is healthy and it's initialized, possible will be
    // function itself

    // Check if the block number is valid
    if (block_num < 0 || block_num > currMetadata->max_blknum) {
        return -1;
    }
    struct proc* p = myproc();
    uint64 p_buff = walkaddr(p->pagetable, buffAddr) | (buffAddr & OFFSET_MASK);

    rw_block(currMetadata, block_num, p_buff, 0);
    return 0;
}

int raid_fail_disk(uint64 disk_num) {
    if (disk_num < VIRTIO_RAID_DISK_START || disk_num > VIRTIO_RAID_DISK_END) {
        return -1;
    }
    uchar data[BSIZE];
    read_block(disk_num, 0, data);

    acquire(&raid_device.disks[disk_num].disk_lock);
    struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;
    currMetadata->disk_status = UNHEALTY;

    // TODO: Add code to handle the failure

    release(&raid_device.disks[disk_num].disk_lock);
    return 0;
}

int raid_repair_disk(uint64 disk_num) {
    if (disk_num < VIRTIO_RAID_DISK_START || disk_num > VIRTIO_RAID_DISK_END) {
        return -1;
    }
    uchar data[BSIZE];
    read_block(disk_num, 0, data);

    acquire(&raid_device.disks[disk_num].disk_lock);
    struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;
    currMetadata->disk_status = RECOVERY;
    // DO recovery
    currMetadata->disk_status = HEALTHY;
    release(&raid_device.disks[disk_num].disk_lock);

    return 0;
}

int raid_system_info(uint64 blkn, uint64 blks, uint64 diskn) {
    for (int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
        uchar data[BSIZE];
        read_block(i, 0, data);
        acquire(&raid_device.disks[i].disk_lock);

        struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;
        if (currMetadata->disk_status != HEALTHY) {
            release(&raid_device.disks[i].disk_lock);
            continue;
        }
        struct proc* p = myproc();
        uint64 p_blkn = walkaddr(p->pagetable, blkn) | (blkn & OFFSET_MASK);
        uint64 p_blksz = walkaddr(p->pagetable, blks) | (blks & OFFSET_MASK);
        uint64 p_diskn = walkaddr(p->pagetable, diskn) | (diskn & OFFSET_MASK);
        *(uint*)p_blkn = currMetadata->max_blknum;
        *(uint*)p_blksz = currMetadata->blk_size;
        *(uint*)p_diskn = currMetadata->num_of_disks;
        release(&raid_device.disks[i].disk_lock);
        return 0;
    }
    // All disks are in fail state
    return -1;
}
