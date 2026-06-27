/*
 * misc.c - FAT misc operations (time, chain, error handling)
 *
 * Ported from Linux kernel fs/fat/misc.c
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fat_core.h"
#include <time.h>
#include <stdarg.h>

#ifndef FAT_UPDATE_ATIME
#define FAT_UPDATE_ATIME    (1u << 0)
#define FAT_UPDATE_CMTIME   (1u << 1)
#endif

/* -------- Time conversion constants -------- */
#define SECS_PER_MIN    60
#define SECS_PER_HOUR   (60 * 60)
#define SECS_PER_DAY    (SECS_PER_HOUR * 24)
#define DAYS_DELTA      (365 * 10 + 2)
#define YEAR_2100       120
#define IS_LEAP_YEAR(y) (!((y) & 3) && (y) != YEAR_2100)

static long days_in_year[] = {
    0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0,
};

static inline int fat_tz_offset(const struct msdos_sb_info *sbi)
{
    /* In userspace, we default to UTC if tz_set not set */
    return (sbi->options.tz_set ? -sbi->options.time_offset : 0) * SECS_PER_MIN;
}

void fat_time_fat2unix(struct msdos_sb_info *sbi, struct ufs_timespec *ts,
                       __le16 __time, __le16 __date, u8 time_cs)
{
    u16 time = le16_to_cpu(__time), date = le16_to_cpu(__date);
    int64_t second;
    long day, leap_day, month, year;

    year  = date >> 9;
    month = (date >> 5) & 0xf;
    if (month < 1) month = 1;
    day   = date & 0x1f;
    if (day < 1) day = 1;
    day--;

    leap_day = (year + 3) / 4;
    if (year > YEAR_2100)
        leap_day--;
    if (IS_LEAP_YEAR(year) && month > 2)
        leap_day++;

    second = (time & 0x1f) << 1;
    second += ((time >> 5) & 0x3f) * SECS_PER_MIN;
    second += (time >> 11) * SECS_PER_HOUR;
    second += (int64_t)(year * 365 + leap_day
               + days_in_year[month] + day
               + DAYS_DELTA) * SECS_PER_DAY;

    second += fat_tz_offset(sbi);

    if (time_cs) {
        ts->tv_sec = second + (time_cs / 100);
        ts->tv_nsec = (time_cs % 100) * 10000000;
    } else {
        ts->tv_sec = second;
        ts->tv_nsec = 0;
    }
}

void fat_time_unix2fat(struct msdos_sb_info *sbi, struct ufs_timespec *ts,
                       __le16 *time, __le16 *date, u8 *time_cs)
{
    int64_t sec = ts->tv_sec + fat_tz_offset(sbi);
    struct tm tm;
    time_t t = (time_t)sec;

    /* Use localtime_r equivalent - gmtime_r since we already applied offset */
    gmtime_r(&t, &tm);

    if (tm.tm_year < 80) {  /* before 1980 */
        *time = 0;
        *date = cpu_to_le16((0 << 9) | (1 << 5) | 1);
        if (time_cs) *time_cs = 0;
        return;
    }
    if (tm.tm_year > 127) {  /* after 2107 */
        *time = cpu_to_le16((23 << 11) | (59 << 5) | 29);
        *date = cpu_to_le16((127 << 9) | (12 << 5) | 31);
        if (time_cs) *time_cs = 199;
        return;
    }

    tm.tm_mon++;      /* 0-11 -> 1-12 */
    tm.tm_sec >>= 1;  /* 0-59 -> 0-29 (2sec) */

    *time = cpu_to_le16((u16)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec));
    *date = cpu_to_le16((u16)(tm.tm_year << 9 | tm.tm_mon << 5 | tm.tm_mday));
    if (time_cs)
        *time_cs = (u8)((ts->tv_sec & 1) * 100 + ts->tv_nsec / 10000000);
}

static inline struct ufs_timespec fat_timespec_trunc_2secs(struct ufs_timespec ts)
{
    struct ufs_timespec ret = { ts.tv_sec & ~1ULL, 0 };
    return ret;
}

struct ufs_timespec fat_truncate_atime(const struct msdos_sb_info *sbi,
                                        const struct ufs_timespec *ts)
{
    int64_t seconds = ts->tv_sec - fat_tz_offset(sbi);
    s32 remainder;

    remainder = (s32)(seconds % SECS_PER_DAY);
    seconds = seconds + fat_tz_offset(sbi) - remainder;

    struct ufs_timespec ret = { seconds, 0 };
    return ret;
}

void fat_truncate_time(struct ufs_inode *inode, struct ufs_timespec *now,
                       unsigned int flags)
{
    if (inode->i_ino == MSDOS_ROOT_INO)
        return;

    /* In our simplified version, just update size and mode times */
    if (flags & FAT_UPDATE_ATIME) {
        /* atime truncated to day boundary */
    }
    if (flags & FAT_UPDATE_CMTIME) {
        /* mtime/ctime truncated to 2 sec */
    }
    (void)now;
}

/* -------- FAT cluster chain -------- */

int fat_chain_add(struct ufs_inode *inode, int new_dclus, int nr_cluster)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    int ret, new_fclus, last;

    last = new_fclus = 0;
    if (MSDOS_I(inode)->i_start) {
        int fclus, dclus;
        ret = fat_get_cluster(inode, FAT_ENT_EOF, &fclus, &dclus);
        if (ret < 0) return ret;
        new_fclus = fclus + 1;
        last = dclus;
    }

    if (last) {
        struct fat_entry fatent;
        fatent_init(&fatent);
        ret = fat_ent_read(inode, &fatent, last);
        if (ret >= 0) {
            ret = fat_ent_write(inode, &fatent, new_dclus, 1);
            fatent_brelse(&fatent);
        }
        if (ret < 0) return ret;
    } else {
        MSDOS_I(inode)->i_start = new_dclus;
        MSDOS_I(inode)->i_logstart = new_dclus;
        fat_sync_inode(inode);
    }

    inode->i_blocks += (unsigned int)nr_cluster << (sbi->cluster_bits - 9);
    return 0;
}

/* -------- FSINFO flush (FAT32) -------- */

int fat_clusters_flush(struct ufs_super_block *sb)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct ufs_buf *bh;
    struct fat_boot_fsinfo *fsinfo;

    if (!is_fat32(sbi)) return 0;

    bh = ufs_bread(sb->s_bdev, sbi->fsinfo_sector);
    if (!bh) {
        fat_msg(sb, "bread failed in fat_clusters_flush");
        return -UFS_EIO;
    }

    fsinfo = (struct fat_boot_fsinfo *)bh->b_data;
    if (!IS_FSINFO(fsinfo)) {
        fat_msg(sb, "Invalid FSINFO signature: 0x%08x, 0x%08x (sector = %lu)",
               le32_to_cpu(fsinfo->signature1),
               le32_to_cpu(fsinfo->signature2),
               sbi->fsinfo_sector);
    } else {
        if (sbi->free_clusters != (unsigned int)-1)
            fsinfo->free_clusters = cpu_to_le32(sbi->free_clusters);
        if (sbi->prev_free != (unsigned int)-1)
            fsinfo->next_cluster = cpu_to_le32(sbi->prev_free);
        ufs_mark_buf_dirty(bh);
    }
    ufs_brelse(bh);
    return 0;
}

int fat_sync_bhs(struct ufs_buf **bhs, int nr_bhs)
{
    return ufs_sync_bufs(bhs, nr_bhs);
}

/* -------- Simple logging -------- */

void __fat_fs_error(struct ufs_super_block *sb, int report, const char *fmt, ...)
{
    va_list args;
    char buf[256];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    fat_msg(sb, "ERROR: %s", buf);

    struct fat_mount_options *opts = &MSDOS_SB(sb)->options;
    if (opts->errors == FAT_ERRORS_RO && !(sb->s_flags & 0x0001)) {
        sb->s_flags |= 0x0001;
        fat_msg(sb, "Filesystem has been set read-only");
    }
}
