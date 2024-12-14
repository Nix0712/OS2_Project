#include "raid.h"

uint64
sys_init_raid(void){
  int raid_type;
  argint(0, &raid_type);
  printf("INIT THE RAID %d \n",raid_type);
  return raid_system_init(raid_type);
}

uint64
sys_read_raid(void){
    printf("READ THE RAID\n");
    return 0;
}

uint64
sys_write_raid(void){
  printf("WRITE THE RAID\n");
  return 0;
}

uint64
sys_disk_fail_raid(void){
  printf("DISK FAIL RAID\n");
  return 0;
}

uint64
sys_disk_repaired_raid(void){
  printf("DISK REPAIRED RAID\n");
  return 0;
}

uint64
sys_info_raid(void){
  uint64 p_diskNum;
  uint64 p_blkSize;
  uint64 p_blkNum;
  argaddr(2, &p_diskNum);
  argaddr(1, &p_blkSize);
  argaddr(0, &p_blkNum);
  return raid_system_info(p_blkNum,p_blkSize,p_diskNum);
}

uint64
sys_destroy_raid(void){
  printf("DESTROY RAID\n");
  return 0;
}
