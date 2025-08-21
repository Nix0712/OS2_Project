#ifndef _RAID_SYSTEM_H_
#define _RAID_SYSTEM_H_

#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"

#define OFFSET_MASK 0x0000000000000FFF

enum RAID_DISK_ROLE
{
    DATA_DISK,
    PARITY_DISK,
    OTHER_TYPE
};
enum DISK_HEALTH
{
    HEALTHY = 1,
    UNHEALTY,
    RECOVERY,
    UNITIALIZED
};

struct RAIDSuperblock
{
    enum RAID_TYPE raid_level;
    enum DISK_HEALTH disk_status;
    int parrity_disk;
    int swap_disk;
    uint max_blknum;
    uint blk_size;
    uint num_of_disks;
};

struct RAIDDisks
{
    struct sleeplock disk_lock; // sleeplock: OK to hold across I/O that may sleep
};

struct RAIDDevice
{
    int is_init; // state: 0 it's not initilazed, state: 1 it is
    enum DISK_HEALTH disk_status[VIRTIO_RAID_DISK_END + 1];
    struct RAIDSuperblock *superblock; // Metadata for the RAID type, cached
    struct RAIDDisks disks[VIRTIO_RAID_DISK_END + 1];
    struct spinlock metadata_lock; // protects is_init/superblock/disk_status cache
};

void init_raid_device();

int raid_system_init(enum RAID_TYPE raid_type);
int raid_read_block(uint64 blkn, uint64 buffAddr);
int raid_write_block(uint64 blkn, uint64 buffAddr);
int raid_fail_disk(uint64 disk_num);
int raid_repair_disk(uint64 disk_num);
int raid_system_info(uint64 blkn, uint64 blks, uint64 diskn);
int raid_system_destroy();

#endif
