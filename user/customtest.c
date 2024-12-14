#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void print_info(){
  uint disk_num, block_num, block_size;
  info_raid(&block_num, &block_size, &disk_num);
  printf("%d %d %d\n",block_num,block_size,disk_num);
}

int main(int argc, char *argv[]){
  if(argc == 2 && strcmp(argv[1], "-i") == 0)
    init_raid(RAID0);

  if(argc == 2 && strcmp(argv[1], "-q") == 0)
    print_info();
  exit(0);
}
