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
    printf("Warrning, there is not enoght disks for RAID system. Init RAID "
           "system will fail.\n");
    return;
  }
  raid_device.is_init =
      0; // Coudl change to array of init vals, for one use of disks metadata
}

int raid_system_init(enum RAID_TYPE raid_type) {
  struct RAIDSuperblock *metadata = (struct RAIDSuperblock *)kalloc();
  metadata->num_of_disks = VIRTIO_RAID_DISK_END;
  metadata->raid_level = raid_type;
  metadata->blk_size = BSIZE;
  metadata->disk_status = HEALTHY;
  metadata->parrity_disk = -1;
  metadata->swap_disk = -1;
  uint64 blockPerDisk = (128 * 1024) / BSIZE;
  switch (raid_type) {
  case RAID0:
    metadata->max_blknum =
        (blockPerDisk * VIRTIO_RAID_DISK_END) - (VIRTIO_RAID_DISK_END);
    break;

  case RAID1:
  case RAID0_1:
    // There is no enough disks for RAID1 or RAID0_1
    if (VIRTIO_RAID_DISK_END == 2) {
      kfree(metadata);
      return -1;
    }
    metadata->max_blknum = blockPerDisk * (VIRTIO_RAID_DISK_END) / 2;
    // Set hotswap disk if one disk fail
    if (((VIRTIO_RAID_DISK_END) & 1) == 1)
      metadata->swap_disk = VIRTIO_RAID_DISK_END;
    break;

  case RAID4:
  case RAID5:
    if (VIRTIO_RAID_DISK_END == 2) {
      kfree(metadata);
      return -1;
    }
    metadata->max_blknum = blockPerDisk * (VIRTIO_RAID_DISK_END - 1);
    metadata->parrity_disk = VIRTIO_RAID_DISK_END;
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

int raid_read_block(uint64 block_num, uint64 buffAddr) {
  // handle if it's not initailized

  // TODO: chage to read from the raid_device on initalization
  uchar data[BSIZE];
  read_block(1, 0, data);
  struct RAIDSuperblock *currMetadata = (struct RAIDSuperblock *)data;

  // TODO: Check if disk is healthy and it's initialized, possible will be
  // function itself

  // Check if the block number is valid
  if (block_num < 0 || block_num > currMetadata->max_blknum) {
    return -1;
  }

  struct proc *p = myproc();
  enum RAID_TYPE raid_type = currMetadata->raid_level;
  uint64 p_buff = walkaddr(p->pagetable, buffAddr) | (buffAddr & OFFSET_MASK);

  *(uint *)p_buff = 1;

  // TODO: Could make a fucntion to use for read and write function out of this
  // switch
  switch (raid_type) {
  case RAID0:
  case RAID1:
  case RAID0_1:
  case RAID4:
  case RAID5:
    break;
  }
  return 0;
}

int raid_write_block(uint64 block_num, uint64 buffAddr) {
  // handle if it's not initailized

  // TODO: chage to read from the raid_device on initalization
  uchar data[BSIZE];
  read_block(1, 0, data);
  struct RAIDSuperblock *currMetadata = (struct RAIDSuperblock *)data;

  // TODO: Check if disk is healthy and it's initialized, possible will be
  // function itself

  // Check if the block number is valid
  if (block_num < 0 || block_num > currMetadata->max_blknum) {
    return -1;
  }

  //TODO:

  // struct proc *p = myproc();
  // enum RAID_TYPE raid_type = currMetadata->raid_level;
  // uint64 p_buff = walkaddr(p->pagetable, buffAddr) | (buffAddr & OFFSET_MASK);


  // uint64 disk_num;
  // uint64 blkc_num;
  // uint64 correct_blkNum = block_num + currMetadata->num_of_disks;
  // uint64 blockPerDisk = (128 * 1024) / BSIZE;
  // switch (raid_type) {
  // case RAID0:
  //   disk_num = (correct_blkNum % currMetadata->num_of_disks) + 1;
  //   blkc_num = block_num / currMetadata->num_of_disks;
  //   write_block(disk_num, blkc_num, (uchar *)p_buff);
  //   break;
  // case RAID1:
  //   // dsk
  //   // blk
  //   write_block(disk_num, blkc_num, (uchar *)p_buff);
  //   write_block(disk_num + currMetadata->num_of_disks, blkc_num,
  //               (uchar *)p_buff); // Write into mirror disk
  //   break;
  // case RAID0_1:
  //   // dsk
  //   // blk
  //   write_block(disk_num, blkc_num, (uchar *)p_buff);
  //   write_block(disk_num + currMetadata->num_of_disks, blkc_num,
  //               (uchar *)p_buff); // Write into mirror disk
  // case RAID4:
  //   // dsk
  //   // blk
  //   write_block(disk_num, blkc_num, (uchar *)p_buff);
  //   // Add write_block for parrity_disk
  // case RAID5:
  //   //dsk
  //   //blk
  //   write_block(disk_num, blkc_num, (uchar *)p_buff);
  //   // Add wirte_blokc for parrity block
  //   break;
  // }

  return 0;
}

int raid_fail_disk(uint64 disk_num) {
  if (disk_num < VIRTIO_RAID_DISK_START || disk_num > VIRTIO_RAID_DISK_END) {
    return -1;
  }
  uchar data[BSIZE];
  read_block(disk_num, 0, data);

  acquire(&raid_device.disks[disk_num].disk_lock);
  struct RAIDSuperblock *currMetadata = (struct RAIDSuperblock *)data;
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
  struct RAIDSuperblock *currMetadata = (struct RAIDSuperblock *)data;
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

    struct RAIDSuperblock *currMetadata = (struct RAIDSuperblock *)data;
    if (currMetadata->disk_status != HEALTHY) {
      release(&raid_device.disks[i].disk_lock);
      continue;
    }
    struct proc *p = myproc();
    uint64 p_blkn = walkaddr(p->pagetable, blkn) | (blkn & OFFSET_MASK);
    uint64 p_blksz = walkaddr(p->pagetable, blks) | (blks & OFFSET_MASK);
    uint64 p_diskn = walkaddr(p->pagetable, diskn) | (diskn & OFFSET_MASK);
    *(uint *)p_blkn = currMetadata->max_blknum;
    *(uint *)p_blksz = currMetadata->blk_size;
    *(uint *)p_diskn = currMetadata->num_of_disks;
    release(&raid_device.disks[i].disk_lock);
    return 0;
  }
  // All disks are in fail state
  return -1;
}
