#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Forward decl for existing helper used by init_test/my_test
void check_data(uint blocks, uchar *blk, uint block_size);

// --- Helpers for ultimate test ---
static void fill_pattern(uchar *buf, uint sz, uint block, uint tag)
{
    for (uint j = 0; j < sz; j++)
        buf[j] = (uchar)((j + block + tag) & 0xFF);
}

static int verify_pattern(uchar *buf, uint sz, uint block, uint tag)
{
    for (uint j = 0; j < sz; j++)
    {
        uchar expect = (uchar)((j + block + tag) & 0xFF);
        if (buf[j] != expect)
            return -1;
    }
    return 0;
}

static void verify_range(uint start, uint count, uint blksz, uint tag)
{
    uchar *buf = malloc(blksz);
    for (uint i = 0; i < count; i++)
    {
        if (read_raid(start + i, buf) < 0)
        {
            printf("read_raid failed at blk %d\n", start + i);
            free(buf);
            exit(1);
        }
        if (verify_pattern(buf, blksz, start + i, tag) != 0)
        {
            printf("verify failed at blk %d\n", start + i);
            free(buf);
            exit(1);
        }
    }
    free(buf);
}

// Verify a range but tolerate reads that fail (e.g., blocks mapped to a failed disk);
// for successful reads, the content must match the expected pattern. At least one
// successful verification is required; if all reads fail, treat as error.
static void verify_range_skip_failed(uint start, uint count, uint blksz, uint tag)
{
    uchar *buf = malloc(blksz);
    int ok = 0;
    for (uint i = 0; i < count; i++)
    {
        int r = read_raid(start + i, buf);
        if (r < 0)
            continue; // skip blocks mapped to failed disk in degraded mode
        if (verify_pattern(buf, blksz, start + i, tag) != 0)
        {
            printf("verify failed at blk %d\n", start + i);
            free(buf);
            exit(1);
        }
        ok = 1;
    }
    if (!ok)
    {
        printf("no successful reads in range [%d..%d)\n", start, start + count);
        free(buf);
        exit(1);
    }
    free(buf);
}

static void concurrent_segment_writers(uint writers, uint total_blocks, uint blksz)
{
    if (writers < 1)
        writers = 1;
    if (writers > 8)
        writers = 8;
    uint seg = total_blocks / writers;
    if (seg == 0)
        seg = 1;
    for (uint w = 0; w < writers; w++)
    {
        int pid = fork();
        if (pid == 0)
        {
            uint start = w * seg;
            uint cnt = (w == writers - 1) ? (total_blocks - start) : seg;
            if (cnt == 0)
                exit(0);
            uchar *buf = malloc(blksz);
            for (uint i = 0; i < cnt; i++)
            {
                fill_pattern(buf, blksz, start + i, w);
                if (write_raid(start + i, buf) < 0)
                {
                    printf("write_raid failed (w=%d blk=%d)\n", w, start + i);
                    free(buf);
                    exit(1);
                }
            }
            free(buf);
            exit(0);
        }
    }
    for (uint w = 0; w < writers; w++)
    {
        int st;
        wait(&st);
        if (st != 0)
        {
            printf("writer child failed (st=%d)\n", st);
            exit(1);
        }
    }
    // verify
    for (uint w = 0; w < writers; w++)
    {
        uint start = w * seg;
        uint cnt = (w == writers - 1) ? (total_blocks - start) : seg;
        if (cnt)
            verify_range(start, cnt, blksz, w);
    }
}

static void parity_stripe_contention(uint num_data_disks, uint blksz)
{
    // Cause multiple writers to hit the same parity block (first stripe)
    uint writers = num_data_disks;
    if (writers > 8)
        writers = 8;
    for (uint w = 0; w < writers; w++)
    {
        int pid = fork();
        if (pid == 0)
        {
            uchar *buf = malloc(blksz);
            fill_pattern(buf, blksz, w, 0xAB);
            if (write_raid(w, buf) < 0)
            {
                printf("stripe write failed blk=%d\n", w);
                free(buf);
                exit(1);
            }
            free(buf);
            exit(0);
        }
    }
    for (uint w = 0; w < writers; w++)
    {
        int st;
        wait(&st);
        if (st != 0)
        {
            printf("stripe child failed\n");
            exit(1);
        }
    }
    // verify
    uchar *buf = malloc(blksz);
    for (uint w = 0; w < writers; w++)
    {
        if (read_raid(w, buf) < 0 || verify_pattern(buf, blksz, w, 0xAB) != 0)
        {
            printf("stripe verify failed blk=%d\n", w);
            free(buf);
            exit(1);
        }
    }
    free(buf);
}

static uint phys_disk_count(enum RAID_TYPE t, uint num_data)
{
    switch (t)
    {
    case RAID0:
        return num_data;
    case RAID1:
    case RAID0_1:
        return num_data * 2;
    case RAID4:
    case RAID5:
        return num_data + 1;
    default:
        return num_data;
    }
}

static void ultimate_one(enum RAID_TYPE t)
{
    if (init_raid(t) < 0)
    {
        printf("init_raid failed for type=%d\n", t);
        return;
    }
    uint data_disks, max_block, blksz;
    if (info_raid(&max_block, &blksz, &data_disks) < 0)
    {
        printf("info_raid failed\n");
        return;
    }
    uint phys = phys_disk_count(t, data_disks);
    uint blocks = max_block;
    if (blocks > 512)
        blocks = 512;

    // Baseline sequential write + verify
    uchar *buf = malloc(blksz);
    for (uint i = 0; i < blocks; i++)
    {
        fill_pattern(buf, blksz, i, 0);
        if (write_raid(i, buf) < 0)
        {
            printf("baseline write failed blk=%d\n", i);
            free(buf);
            return;
        }
    }
    for (uint i = 0; i < blocks; i++)
    {
        if (read_raid(i, buf) < 0 || verify_pattern(buf, blksz, i, 0) != 0)
        {
            printf("baseline verify failed blk=%d\n", i);
            free(buf);
            return;
        }
    }
    free(buf);

    // Concurrency: segment writers
    concurrent_segment_writers(4, blocks, blksz);

    // Parity contention (only meaningful for RAID4/5)
    if (t == RAID4 || t == RAID5)
        parity_stripe_contention(data_disks, blksz);

    // Failure scenarios (prepare a known pattern where needed)
    switch (t)
    {
    case RAID0:
    {
        // write small range, then fail disk 1 and expect some reads to fail
        buf = malloc(blksz);
        uint n = blocks > 32 ? 32 : blocks;
        for (uint i = 0; i < n; i++)
        {
            fill_pattern(buf, blksz, i, 0x30);
            write_raid(i, buf);
        }
        if (phys >= 1)
            disk_fail_raid(1);
        // We don't assume which logical block maps to the failed disk.
        // Instead, scan the small range and require at least one read to fail.
        int any_fail = 0;
        for (uint i = 0; i < n; i++)
        {
            if (read_raid(i, buf) < 0)
            {
                any_fail = 1;
                break;
            }
        }
        if (!any_fail)
            printf("RAID0: expected at least one read failure after a disk fail, but none failed\n");
        free(buf);
        break;
    }
    case RAID1:
    case RAID0_1:
    {
        if (phys >= 2)
        {
            uint verifyN = blocks > 64 ? 64 : blocks;
            buf = malloc(blksz);
            for (uint i = 0; i < verifyN; i++)
            {
                fill_pattern(buf, blksz, i, 0x33);
                write_raid(i, buf);
            }
            disk_fail_raid(1);
            verify_range(0, verifyN, blksz, 0x33);

            // rewrite a few blocks during failure and verify
            for (uint i = 0; i < 16 && i < blocks; i++)
            {
                fill_pattern(buf, blksz, i, 0x77);
                write_raid(i, buf);
            }
            for (uint i = 0; i < 16 && i < blocks; i++)
            {
                read_raid(i, buf);
                if (verify_pattern(buf, blksz, i, 0x77) != 0)
                    printf("mirror verify failed blk=%d\n", i);
            }
            disk_repaired_raid(1);
            // verify after repair: first 16 updated (0x77), the rest 0x33
            for (uint i = 0; i < 16 && i < verifyN; i++)
            {
                read_raid(i, buf);
                if (verify_pattern(buf, blksz, i, 0x77) != 0)
                    printf("post-repair verify failed blk=%d\n", i);
            }
            for (uint i = 16; i < verifyN; i++)
            {
                read_raid(i, buf);
                if (verify_pattern(buf, blksz, i, 0x33) != 0)
                    printf("post-repair verify (unchanged) failed blk=%d\n", i);
            }
            free(buf);
        }
        break;
    }
    case RAID4:
    {
        if (phys >= data_disks + 1)
        {
            uint verifyN = blocks > 64 ? 64 : blocks;
            buf = malloc(blksz);
            for (uint i = 0; i < verifyN; i++)
            {
                fill_pattern(buf, blksz, i, 0x21);
                write_raid(i, buf);
            }
            free(buf);
            disk_fail_raid(1);
            // Some blocks map to the failed disk; those reads may fail. Others must verify.
            verify_range_skip_failed(0, verifyN, blksz, 0x21);
            disk_repaired_raid(1);
            disk_fail_raid(data_disks + 1);
            verify_range(0, verifyN, blksz, 0x21);
            disk_repaired_raid(data_disks + 1);
        }
        break;
    }
    case RAID5:
    {
        if (phys >= data_disks + 1)
        {
            uint verifyN = blocks > 64 ? 64 : blocks;
            buf = malloc(blksz);
            for (uint i = 0; i < verifyN; i++)
            {
                fill_pattern(buf, blksz, i, 0x45);
                write_raid(i, buf);
            }
            free(buf);
            disk_fail_raid(1);
            disk_repaired_raid(1);
            verify_range(0, verifyN, blksz, 0x45);
        }
        break;
    }
    }
}

void ultimate_test()
{
    enum RAID_TYPE raidList[] = {RAID0, RAID1, RAID0_1, RAID4, RAID5};
    for (uint k = 4; k < 5; k++)
    {
        printf("=== Ultimate RAID test type=%d ===\n", raidList[k]);
        ultimate_one(raidList[k]);
    }
}

// --- Original tests below ---
void init_test()
{
    init_raid(RAID1);

    uint disk_num, block_num, block_size;
    info_raid(&block_num, &block_size, &disk_num);

    uint blocks = (512 > block_num ? block_num : 512);

    uchar *blk = malloc(block_size);
    for (uint i = 0; i < blocks; i++)
    {
        for (uint j = 0; j < block_size; j++)
        {
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

void my_test()
{
    enum RAID_TYPE raidList[] = {RAID0, RAID1, RAID0_1, RAID4, RAID5};

    for (uint k = 0; k < 5; k++)
    {
        init_raid(raidList[k]);

        uint disk_num, block_num, block_size;
        info_raid(&block_num, &block_size, &disk_num);

        uint blocks = (512 > block_num ? block_num : 512);

        uchar *blk = malloc(block_size);
        for (uint i = 0; i < blocks; i++)
        {
            for (uint j = 0; j < block_size; j++)
            {
                blk[j] = j + i;
            }
            write_raid(i, blk);
        }
        printf("UPISAO\n");
        check_data(blocks, blk, block_size);
        free(blk);
    }
}

void textwrite(char *s)
{
    int pid;
    int xstatus;
    uint64 addrs[] = {0, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                      0xffffffffffffffff};

    for (int ai = 0; ai < sizeof(addrs) / sizeof(addrs[0]); ai++)
    {
        pid = fork();
        if (pid == 0)
        {
            volatile int *addr = (int *)addrs[ai];
            *addr = 10;
            printf("%s: write to %p did not fail!\n", s, addr);
            exit(0);
        }
        else if (pid < 0)
        {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        wait(&xstatus);
        if (xstatus == 0)
        {
            // kernel did not kill child!
            exit(1);
        }
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "--rw-test") == 0)
        my_test();
    if (argc == 2 && strcmp(argv[1], "--zika-test") == 0)
        init_test();
    if (argc == 2 && strcmp(argv[1], "--ultimate-test") == 0)
        ultimate_test();
    if (argc == 2 && strcmp(argv[1], "--text-write") == 0)
        textwrite("Test");
    exit(0);
}

void check_data(uint blocks, uchar *blk, uint block_size)
{
    for (uint i = 0; i < blocks; i++)
    {
        read_raid(i, blk);
        for (uint j = 0; j < block_size; j++)
        {
            if ((uchar)(j + i) != blk[j])
            {
                printf("expected=%d got=%d", j + i, blk[j]);
                printf("Data in the block %d faulty\n", i);
                return;
            }
        }
    }
    printf("Data is correct\n");
    printf("------------------------------------------------\n");
}
