#include "types.h"
#include "riscv.h"
#include "defs.h"


uint64
sys_init_raid(void){

  printf("INIT THE RAID\n");
  return 0;
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
  printf("SYS INFO RAID\n");
  return 0;
}

uint64
sys_destroy_raid(void){
  printf("DESTROY RAID\n");
  return 0;
}
