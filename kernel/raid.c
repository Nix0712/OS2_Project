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
}

int raid_system_init(enum RAID_TYPE raid_type) {
  struct RAIDSuperblock *metadata = (struct RAIDSuperblock *)kalloc();
  metadata->num_of_disks = VIRTIO_RAID_DISK_END - 1;
  metadata->raid_level = raid_type;
  metadata->blk_size = BSIZE;
  metadata->disk_status = HEALTHY;
  switch (raid_type) {
  case RAID0:
    metadata->max_blknum =
        (((128 * 1024) / BSIZE) * (VIRTIO_RAID_DISK_END - 1)) -
        (VIRTIO_RAID_DISK_END - 1);
    break;
  case RAID1:
  case RAID0_1:
    break;
  case RAID4:
  case RAID5:
    break;
  }
  for (int i = VIRTIO_RAID_DISK_START; i < VIRTIO_RAID_DISK_END; i++) {
    metadata->disk_id = i;
    // Write into the first block of disk i
    write_block(i, 0, (uchar *)metadata);
  }
  kfree(metadata);
  return 0;
}

int raid_fail_disk(uint64 disk_num) {
  if (disk_num < VIRTIO_RAID_DISK_START || disk_num >= VIRTIO_RAID_DISK_END) {
    return -1;
  }
  return 0;
}

int raid_system_info(uint64 blkn, uint64 blks, uint64 diskn) {
  for (int i = VIRTIO_RAID_DISK_START; i < VIRTIO_RAID_DISK_END; i++) {
    uchar data[BSIZE];
    read_block(i, 0, data);
    acquire(&raid_device.disks[i].disk_lock);

    struct RAIDSuperblock *currMetadata = (struct RAIDSuperblock *)data;
    if (currMetadata->disk_status != HEALTHY) {
      release(&raid_device.disks[i].disk_lock);
      continue;
    }
    struct proc *p = myproc();
    uint64 mask = (1 << 12) - 1;
    uint64 p_blkn = walkaddr(p->pagetable, blkn) | (blkn & mask);
    uint64 p_blksz = walkaddr(p->pagetable, blks) | (blks & mask);
    uint64 p_diskn = walkaddr(p->pagetable, diskn) | (diskn & mask);
    *(uint *)p_blkn = currMetadata->max_blknum;
    *(uint *)p_blksz = currMetadata->blk_size;
    *(uint *)p_diskn = currMetadata->num_of_disks;
    release(&raid_device.disks[i].disk_lock);
    return 0;
  }
  // All disks are in fail state
  return -1;
}
