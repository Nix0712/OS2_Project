#ifndef _RAID_SYSTEM_H_
#define _RAID_SYSTEM_H_

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"

enum RAID_DISK_ROLE { DATA_DISK, PARITY_DISK, OTHER_TYPE };
enum DISK_HEALTH { HEALTHY, UNHEALTY, RECOVERY };

struct RAIDSuperblock{
  enum RAID_TYPE raid_level;
  enum DISK_HEALTH disk_status;
  uint max_blknum;
  uint blk_size;
  uint num_of_disks;
  uint disk_id;
};

struct RAIDDisks{
  struct spinlock disk_lock;
};

struct RAIDDevice{
  uint64 is_init; // state: 0 it's not initilazed, state: 1 it is
  struct RAIDDisks disks[VIRTIO_RAID_DISK_END-VIRTIO_RAID_DISK_START];
};


void init_raid_device();

int raid_system_init(enum RAID_TYPE raid_type);
int raid_system_info(uint64 blkn, uint64 blks, uint64 diskn);

#endif
