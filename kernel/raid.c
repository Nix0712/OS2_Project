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

enum DISK_HEALTH get_disk_health(int disk_num) {
    if (disk_num < VIRTIO_RAID_DISK_START || disk_num > VIRTIO_RAID_DISK_END) {
        return -1;
    }

    if (raid_device.disk_status[disk_num] == UNITIALIZED) {
        uchar* data = kalloc();
        read_block(disk_num, 0, data);
        struct RAIDSuperblock* superblock = (struct RAIDSuperblock*)data;
        raid_device.disk_status[disk_num] = superblock->disk_status;
        kfree(data);
    }

    return raid_device.disk_status[disk_num];
}

int load_metadata(struct RAIDSuperblock** metadata) {
    if (raid_device.is_init == -1) {
        uchar* data = kalloc();
        read_block(1, 0, data);
        struct RAIDSuperblock* superblock = (struct RAIDSuperblock*)data;
        if (is_raid_uninitialized(superblock)) {
            kfree(data);
            return -1;
        }
        raid_device.superblock = (struct RAIDSuperblock*)data;
    }

    if (is_raid_uninitialized(raid_device.superblock))
        return -1;

    (*metadata) = raid_device.superblock;
    raid_device.is_init = 1;
    raid_device.disk_status[0] = raid_device.superblock->disk_status;

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

    raid_device.is_init = -1;   // Not initialized (not in memory)
    raid_device.superblock = 0; // Not in memory, will be loaded when needed

    for (int i = 0; i <= VIRTIO_RAID_DISK_END; i++) {
        raid_device.disk_status[i] = UNITIALIZED;
    }
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

int handle_rw_raid01(struct RAIDSuperblock* currMetadata, uint64 disk_num, uint64 blkc_num, uchar* p_buff, int isRead) {
    if (isRead) {
        enum DISK_HEALTH disk_health = get_disk_health(disk_num);
        if (disk_health == HEALTHY) {
            read_block(disk_num, blkc_num, p_buff);
        } else {
            disk_health = get_disk_health(disk_num + currMetadata->num_of_disks);
            if (disk_health != HEALTHY) {
                return -1; // We can't read from the mirror disk, LOST DATA!
            }
            read_block(disk_num + currMetadata->num_of_disks, blkc_num, p_buff);
        }
    } else {
        enum DISK_HEALTH disk_health = get_disk_health(disk_num);
        if (disk_health == HEALTHY) {
            write_block(disk_num, blkc_num, p_buff);
        } else {
            disk_health = get_disk_health(disk_num + currMetadata->num_of_disks);
            if (disk_health != HEALTHY) {
                return -1; // We can't write into the mirror disk, LOST DATA!
            }
        }
        write_block(disk_num + currMetadata->num_of_disks, blkc_num, p_buff); // Write into mirror disk
    }
    return 0;
}

// For given parity disk and block, calculate and write new parrity block onto the disk
void write_parrity_block(uint64 parity_disk, uint64 parity_block, uchar* oldData, uchar* newData) {
    uchar parityData[BSIZE];
    read_block(parity_disk, parity_block, parityData);

    // Calculate new parity data
    uchar* newParrityData = kalloc();
    for (int i = 0; i < BSIZE; i++)
        newParrityData[i] = (newData[i] ^ oldData[i]) ^ parityData[i];

    write_block(parity_disk, parity_block, newParrityData); // Write new data to parity block
    kfree(newParrityData);
}

// Handle read/write for all RAID levels
int rw_block(struct RAIDSuperblock* currMetadata, uint64 block_num, uint64 p_buff, int isRead) {
    enum RAID_TYPE raid_type = currMetadata->raid_level;
    uint64 disk_num;
    uint64 blkc_num;
    uint64 blockPerDisk = (128 * 1024 * 1024) / BSIZE;
    enum DISK_HEALTH disk_health;
    switch (raid_type) {
    case RAID0:
        disk_num = (block_num % currMetadata->num_of_disks) + 1;
        blkc_num = (block_num / currMetadata->num_of_disks) + 1;

        disk_health = get_disk_health(disk_num);
        if (disk_health != HEALTHY)
            return -1; // LOST DATA!

        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
        } else {
            write_block(disk_num, blkc_num, (uchar*)p_buff);
        }
        break;
    case RAID1:
        disk_num = (block_num / (blockPerDisk - 1)) + 1;
        blkc_num = (block_num % (blockPerDisk - 1)) + 1;
        if (handle_rw_raid01(currMetadata, disk_num, blkc_num, (uchar*)p_buff, isRead) == -1) {
            return -1; // LOST DATA!
        }
        break;
    case RAID0_1:
        disk_num = (block_num % currMetadata->num_of_disks) + 1;
        blkc_num = ((block_num + currMetadata->num_of_disks) / currMetadata->num_of_disks);
        if (handle_rw_raid01(currMetadata, disk_num, blkc_num, (uchar*)p_buff, isRead) == -1) {
            return -1; // LOST DATA!
        }
        break;
    case RAID4:
        disk_num = (block_num % currMetadata->num_of_disks) + 1;
        blkc_num = (block_num / currMetadata->num_of_disks) + 1;

        // Check if the disk is healthy
        disk_health = get_disk_health(disk_num);
        if (disk_health != HEALTHY)
            return -1; // LOST DATA!

        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
        } else {
            uchar oldData[BSIZE];
            read_block(disk_num, blkc_num, oldData);

            // It's the same data, no need to write
            if (memcmp(oldData, (uchar*)p_buff, BSIZE) == 0)
                return 0;

            write_parrity_block(currMetadata->parrity_disk, blkc_num, oldData, (uchar*)p_buff);
            write_block(disk_num, blkc_num, (uchar*)p_buff);
        }
        break;
    case RAID5:
        uint64 stripe_index = block_num / (currMetadata->num_of_disks - 1);
        uint64 stripe_offset = block_num % (currMetadata->num_of_disks - 1);
        uint64 parrity_index = (currMetadata->num_of_disks - 1) - stripe_index;
        disk_num = ((parrity_index + stripe_offset + 1) % (currMetadata->num_of_disks)) + 1;
        blkc_num = stripe_index + 1;

        // Check if the disk is healthy
        disk_health = get_disk_health(disk_num);
        if (disk_health != HEALTHY)
            return -1; // LOST DATA!

        if (isRead) {
            read_block(disk_num, blkc_num, (uchar*)p_buff);
        } else {
            uchar oldData[BSIZE];
            read_block(disk_num, blkc_num, oldData);

            // It's the same data, no need to write
            if (memcmp(oldData, (uchar*)p_buff, BSIZE) == 0)
                return 0;

            // TODO: handle parity write
            // write_parrity_block(parrity_index, blkc_num, oldData, (uchar*)p_buff);
            write_block(disk_num, blkc_num, (uchar*)p_buff);
        }
        break;
    }
    return 0;
}

// Recover data based on the RAID level and given failed disk number
int handle_recovery(struct RAIDSuperblock* currMetadata, uint64 failed_count, uint64 disk_num) {
    enum RAID_TYPE raid_type = currMetadata->raid_level;

    switch (raid_type) {
    case RAID0:
        return -1;
    case RAID1:
    case RAID0_1:
        // TODO: Mirror recovery
        break;
    case RAID4:
        if (failed_count > 1 || currMetadata->parrity_disk == disk_num)
            return -1;
        // TODO: Parrity disk recovery
        break;
    case RAID5:
        if (failed_count > 1)
            return -1;
        // TODO: Parrity disk recovery
        break;
    }
    return 0;
    ;
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
            return -2;
        }
        metadata->num_of_disks = (VIRTIO_RAID_DISK_END) / 2;
        metadata->max_blknum = (blockPerDisk * metadata->num_of_disks) - metadata->num_of_disks;

        // Set hotswap disk if one disk fail
        if (((VIRTIO_RAID_DISK_END) & 1) == 1)
            metadata->swap_disk = VIRTIO_RAID_DISK_END;
        break;

    case RAID4:
        metadata->num_of_disks = VIRTIO_RAID_DISK_END - 1;
        metadata->parrity_disk = VIRTIO_RAID_DISK_END;
    case RAID5:
        if (VIRTIO_RAID_DISK_END == 2) {
            kfree(metadata);
            return -2;
        }
        metadata->max_blknum = blockPerDisk * (VIRTIO_RAID_DISK_END - 1);
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
    struct RAIDSuperblock* currMetadata;
    if (load_metadata(&currMetadata) == -1) {
        // RAID is no initialized
        return -2;
    }

    // Check if the block number is valid
    if (block_num < 0 || block_num > currMetadata->max_blknum) {
        return -1; // Number of block is invalid
    }

    struct proc* p = myproc();
    uint64 p_buff = walkaddr(p->pagetable, buffAddr) | (buffAddr & OFFSET_MASK);

    return rw_block(currMetadata, block_num, p_buff, 1);
}

int raid_write_block(uint64 block_num, uint64 buffAddr) {
    struct RAIDSuperblock* currMetadata;

    if (load_metadata(&currMetadata) == -1) {
        // RAID is not initialized
        return -2;
    }

    // Check if the block number is valid
    if (block_num < 0 || block_num > currMetadata->max_blknum) {
        return -1; // Number of block is invalid
    }
    struct proc* p = myproc();
    uint64 p_buff = walkaddr(p->pagetable, buffAddr) | (buffAddr & OFFSET_MASK);

    return rw_block(currMetadata, block_num, p_buff, 0);
}

int raid_fail_disk(uint64 disk_num) {
    if (disk_num < VIRTIO_RAID_DISK_START || disk_num > VIRTIO_RAID_DISK_END) {
        return -1; // Disk number is invalid
    }

    uchar data[BSIZE];
    read_block(disk_num, 0, data);
    struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;

    currMetadata->disk_status = UNHEALTY;                          // Update the raid status in the disk
    raid_device.disk_status[disk_num] = currMetadata->disk_status; // Update the raid device status in cache

    return 0;
}

int raid_repair_disk(uint64 disk_num) {
    if (disk_num < VIRTIO_RAID_DISK_START || disk_num > VIRTIO_RAID_DISK_END) {
        return -2;
    }
    if (get_disk_health(disk_num) == HEALTHY)
        return 0;
    uint64 fail_count = 0;
    // uint64 recovery_count = 0; //will need later for sync // TODO
    for (int i = 1; i <= VIRTIO_RAID_DISK_END; i++) {
        enum DISK_HEALTH disk_status = get_disk_health(i);
        // Ether the disk is healthy or (in recovery, is unhelthy), get_disk_health will load if it's uninitialized
        if (disk_status == UNHEALTY)
            fail_count++;
    }

    uchar data[BSIZE];
    read_block(disk_num, 0, data);

    struct RAIDSuperblock* currMetadata = (struct RAIDSuperblock*)data;

    currMetadata->disk_status = RECOVERY;
    raid_device.disk_status[disk_num] = currMetadata->disk_status;

    handle_recovery(currMetadata, fail_count, disk_num);

    currMetadata->disk_status = HEALTHY;
    raid_device.disk_status[disk_num] = currMetadata->disk_status;

    return 0;
}

int raid_system_info(uint64 blkn, uint64 blks, uint64 diskn) {
    for (int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
        struct RAIDSuperblock* currMetadata;
        if (load_metadata(&currMetadata) == -1) {
            break; // RAID is not initialized
        }

        enum DISK_HEALTH disk_status = get_disk_health(i);
        if (disk_status != HEALTHY) {
            continue;
        }

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
    if (raid_device.is_init != -1) {
        kfree(raid_device.superblock);
    }
    raid_device.is_init = -1;
    raid_device.superblock = 0;
    return 0;
}
