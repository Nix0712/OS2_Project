#include "raid.h"
#include "defs.h"
#include "fs.h"
#include "param.h"
#include "proc.h"

// What if multiple processes call raid_init?
static struct RAIDDevice raid_device;

int is_raid_uninitialized(struct RAIDSuperblock* superblock) {
    struct RAIDSuperblock zero_device = {0}; // Zero-initialized struct
    return memcmp(superblock, &zero_device, sizeof(struct RAIDSuperblock)) == 0;
}

int load_metadata(struct RAIDSuperblock** metadata) {
    if (raid_device.is_init == -1) {
        uchar* data = kalloc();
        read_block(1, 0, data); // TODO: could find healty disk and load from it?
        struct RAIDSuperblock* superblock = (struct RAIDSuperblock*)data;
        if (is_raid_uninitialized(superblock)) {
            kfree(data);
            return -1;
        }
        raid_device.superblock = (struct RAIDSuperblock*)data;
    }

    if(is_raid_uninitialized(raid_device.superblock))
        return -1;

    (*metadata) = raid_device.superblock;
    raid_device.is_init = 1;

    return 0;
}

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

    raid_device.is_init = -1;    // Not initialized (not in memory)
    raid_device.meta_index = -1; // TODO: Do I need this?
    raid_device.superblock = 0;  // Not in memory, will be loaded when needed

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
        } else {
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
            // read_block(disk_num + currMetadata->num_of_disks, blkc_num, (uchar *)p_buff); // Write into mirror disk
        } else {
            write_block(disk_num, blkc_num, (uchar*)p_buff);
            // write_block(disk_num + currMetadata->num_of_disks, blkc_num,(uchar *)p_buff); // Write into mirror disk
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
        metadata->max_blknum = (blockPerDisk * metadata->num_of_disks) - metadata->num_of_disks;
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
    raid_device.superblock = metadata;
    raid_device.is_init = 1; // We initailized the raid device
    return 0;
}

int raid_read_block(uint64 block_num, uint64 buffAddr) {
    // TODO: handle if it's not initailized
    // TODO: chage to read from the raid_device on initalization and not from block
    struct RAIDSuperblock* currMetadata;
    load_metadata(&currMetadata);

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
    // TODO: chage to read from the raid_device on initalization
    struct RAIDSuperblock* currMetadata;
    load_metadata(&currMetadata);

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
    struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;

    currMetadata->disk_status = UNHEALTY;

    // TODO: Add code to handle the failure

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

    // TODO: DO recovery

    currMetadata->disk_status = HEALTHY;
    release(&raid_device.disks[disk_num].disk_lock);

    return 0;
}

int raid_system_info(uint64 blkn, uint64 blks, uint64 diskn) {
    for (int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
        struct RAIDSuperblock* currMetadata;
        if (load_metadata(&currMetadata) == -1)
            return -1;

        //TODO: handle unhealty disk

        struct proc* p = myproc();
        uint64 p_blkn = walkaddr(p->pagetable, blkn) | (blkn & OFFSET_MASK);
        uint64 p_blksz = walkaddr(p->pagetable, blks) | (blks & OFFSET_MASK);
        uint64 p_diskn = walkaddr(p->pagetable, diskn) | (diskn & OFFSET_MASK);
        *(uint*)p_blkn = currMetadata->max_blknum;
        *(uint*)p_blksz = currMetadata->blk_size;
        *(uint*)p_diskn = currMetadata->num_of_disks;

        return 0;
    }
    
    // All disks are in fail state
    return -1;
}

int raid_system_destroy() {
    uchar data[BSIZE];
    memset(data, 0, BSIZE);
    for (int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
        write_block(i, 0, data);
    }
    raid_device.is_init = -1;
    kfree(raid_device.superblock);
    raid_device.superblock = 0;
    return 0;
}
