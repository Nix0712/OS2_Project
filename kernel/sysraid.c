#include "raid.h"

uint64 sys_init_raid(void) {
    int raid_type;
    argint(0, &raid_type);
    printf("INIT THE RAID %d \n", raid_type);
    return raid_system_init(raid_type);
}

uint64 sys_read_raid(void) {
    int blkNum;
    uint64 p_buff;
    argint(0, &blkNum);
    argaddr(1, &p_buff);
    // printf("READ THE RAID\n");
    return raid_read_block(blkNum, p_buff);
}

uint64 sys_write_raid(void) {
    int blkNum;
    uint64 p_buff;
    argint(0, &blkNum);
    argaddr(1, &p_buff);
    // printf("WRITE THE RAID\n");
    return raid_write_block(blkNum, p_buff);
}

uint64 sys_disk_fail_raid(void) {
    int disk_num;
    argint(0, &disk_num);
    printf("DISK FAIL RAID\n");
    return raid_fail_disk(disk_num);
}

uint64 sys_disk_repaired_raid(void) {
    int disk_num;
    argint(0, &disk_num);
    printf("DISK REPAIRED RAID\n");
    return raid_repair_disk(disk_num);
}

uint64 sys_info_raid(void) {
    uint64 p_diskNum;
    uint64 p_blkSize;
    uint64 p_blkNum;
    argaddr(2, &p_diskNum);
    argaddr(1, &p_blkSize);
    argaddr(0, &p_blkNum);
    return raid_system_info(p_blkNum, p_blkSize, p_diskNum);
}

uint64 sys_destroy_raid(void) {
    printf("DESTROY RAID\n");
    return raid_system_destroy();
}
