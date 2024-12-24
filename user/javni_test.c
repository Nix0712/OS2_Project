#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void check_data(uint blocks, uchar* blk, uint block_size);

void init_test() {
    init_raid(RAID1);

    uint disk_num, block_num, block_size;
    info_raid(&block_num, &block_size, &disk_num);

    uint blocks = (512 > block_num ? block_num : 512);

    uchar* blk = malloc(block_size);
    for (uint i = 0; i < blocks; i++) {
        for (uint j = 0; j < block_size; j++) {
            blk[j] = j + i;
        }
        write_raid(i, blk);
    }

    check_data(blocks, blk, block_size);

    disk_fail_raid(1);

    check_data(blocks, blk, block_size);

    disk_repaired_raid(1);

    check_data(blocks, blk, block_size);

    free(blk);
}

void my_test() {
    enum RAID_TYPE raidList[] = {RAID0, RAID1, RAID0_1, RAID4, RAID5};

    for (uint k = 0; k < 5; k++) {
        init_raid(raidList[k]);

        uint disk_num, block_num, block_size;
        info_raid(&block_num, &block_size, &disk_num);

        uint blocks = (512 > block_num ? block_num : 512);

        uchar* blk = malloc(block_size);
        for (uint i = 0; i < blocks; i++) {
            for (uint j = 0; j < block_size; j++) {
                blk[j] = j + i;
            }
            write_raid(i, blk);
        }
        printf("UPISAO\n");
        check_data(blocks, blk, block_size);
        free(blk);
    }
}

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "--rw-test") == 0)
        my_test();
    if (argc == 2 && strcmp(argv[1], "--zika-test") == 0)
        init_test();
    exit(0);
}

void check_data(uint blocks, uchar* blk, uint block_size) {
    for (uint i = 0; i < blocks; i++) {
        read_raid(i, blk);
        for (uint j = 0; j < block_size; j++) {
            if ((uchar)(j + i) != blk[j]) {
                printf("expected=%d got=%d", j + i, blk[j]);
                printf("Data in the block %d faulty\n", i);
                return;
            }
        }
    }
    printf("Data is correct\n");
    printf("------------------------------------------------\n");
}
