#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void print_info() {
    uint disk_num, block_num, block_size;
    if (info_raid(&block_num, &block_size, &disk_num) == 0)
        printf("%d %d %d\n", block_num, block_size, disk_num);
    else
        printf("RAID is not initalized\n");

    // uchar* blk = malloc(block_size);
    // free(blk);
}

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "-i") == 0)
        init_raid(RAID0_1);
    if (argc == 2 && strcmp(argv[1], "-q") == 0)
        print_info();
    if (argc == 2 && strcmp(argv[1], "-f") == 0)
        disk_fail_raid(2);
    if (argc == 2 && strcmp(argv[1], "-r") == 0)
        disk_repaired_raid(2);
    if (argc == 2 && strcmp(argv[1], "-d") == 0)
        destroy_raid();
    exit(0);
}
