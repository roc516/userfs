/*
 * vfs_inode.c - FAT VFS inode operations
 *
 * Ported from Linux kernel fs/fat/inode.c
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fat_core.h"
#include "internal.h"

/* -------- Inode allocation / free -------- */

struct ufs_inode *fat_alloc_inode(struct ufs_super_block *sb)
{
    static u32 next_ino = 2;  /* Start after root (ino=1) */
    struct msdos_inode_info *i;

    i = (struct msdos_inode_info *)calloc(1, sizeof(*i));
    if (!i) return NULL;

    i->vfs_inode.i_sb = sb;
    i->vfs_inode.i_private = NULL;
    i->vfs_inode.i_ino = next_ino++;
    i->mmu_private = 0;
    i->i_start = 0;
    i->i_logstart = 0;
    i->i_attrs = 0;
    i->i_pos = 0;
    i->nr_caches = 0;
    i->cache_valid_id = 1;
    INIT_LIST_HEAD(&i->cache_lru);
    pthread_mutex_init(&i->cache_lru_lock, NULL);

    return &i->vfs_inode;
}

void fat_free_inode(struct ufs_inode *inode)
{
    struct msdos_inode_info *i = MSDOS_I(inode);
    if (i) free(i);
}

void fat_evict_inode(struct ufs_inode *inode)
{
    fat_cache_inval_inode(inode);
    fat_detach(inode);
    fat_free_inode(inode);
}

/* -------- Attach / Detach (inode hash) -------- */

void fat_attach(struct ufs_inode *inode, loff_t i_pos)
{
    MSDOS_I(inode)->i_pos = i_pos;
    ufs_inode_insert_hash(inode);
}

void fat_detach(struct ufs_inode *inode)
{
    ufs_inode_remove_hash(inode);
    MSDOS_I(inode)->i_pos = 0;
}

/* -------- Build inode from directory entry -------- */

int fat_fill_inode(struct ufs_inode *inode, struct msdos_dir_entry *de)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    int cluster = fat_get_start(sbi, de);

    MSDOS_I(inode)->i_start = cluster;
    MSDOS_I(inode)->i_logstart = cluster;
    MSDOS_I(inode)->i_attrs = de->attr & ATTR_UNUSED;
    MSDOS_I(inode)->mmu_private = le32_to_cpu(de->size);

    inode->i_mode = fat_make_mode(sbi, de->attr, S_IRWXUGO);
    inode->i_size = le32_to_cpu(de->size);
    inode->i_blocks = 0;
    inode->i_nlink = 1;

    /* Set ops tables */
    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = (struct ufs_inode_operations *)sbi->dir_ops;
        inode->i_fop = (struct ufs_file_operations *)&fat_dir_operations;
    } else {
        inode->i_op = NULL;
        inode->i_fop = (struct ufs_file_operations *)&fat_file_operations;
    }

    return 0;
}

struct ufs_inode *fat_build_inode(struct ufs_super_block *sb,
                                   struct msdos_dir_entry *de, loff_t i_pos)
{
    struct ufs_inode *inode;

    inode = fat_alloc_inode(sb);
    if (!inode) return NULL;

    fat_fill_inode(inode, de);
    fat_attach(inode, i_pos);

    return inode;
}

struct ufs_inode *fat_iget(struct ufs_super_block *sb, loff_t i_pos)
{
    return ufs_iget(sb, (unsigned long)i_pos);
}

/* -------- Write inode -------- */

int fat_write_inode(struct ufs_inode *inode)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct msdos_dir_entry *de;
    struct ufs_buf *bh;
    int err;
    u16 fat_time_val, fat_date_val;
    loff_t i_pos = MSDOS_I(inode)->i_pos;
    unsigned long parent_ino = MSDOS_I(inode)->i_parent_dir_ino;
    struct ufs_inode *parent;

    /* Try to use i_pos + parent dir to directly modify the entry */
    if (parent_ino > 0) {
        parent = ufs_iget(sb, parent_ino);
        if (parent) {
            sector_t iblock = (sector_t)(i_pos >> sb->s_blocksize_bits);
            int offset = (int)(i_pos & (sb->s_blocksize - 1));
            sector_t phys;
            unsigned long mapped;

            err = fat_bmap(parent, iblock, &phys, &mapped, 0);
            if (err == 0 && phys) {
                bh = ufs_bread(sb->s_bdev, phys);
                if (bh) {
                    de = (struct msdos_dir_entry *)(bh->b_data + offset);
                    /* Update timestamp */
                    struct ufs_timespec ts;
                    ts.tv_sec = time(NULL);
                    ts.tv_nsec = 0;
                    fat_time_unix2fat(sbi, &ts, &fat_time_val, &fat_date_val, NULL);
                    de->time = cpu_to_le16(fat_time_val);
                    de->date = cpu_to_le16(fat_date_val);
                    /* Update size */
                    de->size = cpu_to_le32((u32)inode->i_size);
                    ufs_mark_buf_dirty(bh);
                    ufs_sync_buf(bh);
                    ufs_brelse(bh);
                    return 0;
                }
            }
        }
    }

    /* Fallback: search root directory by logstart (works for root-level files) */
    /* This also handles older inodes where i_pos might not be set */
    {
        struct fat_slot_info sinfo;
        memset(&sinfo, 0, sizeof(sinfo));
        err = fat_scan_logstart(ufs_iget(sb, 1),
                                MSDOS_I(inode)->i_logstart, &sinfo);
        if (err < 0)
            return 0;  /* Silently skip if can't find */
        de = sinfo.de;
        bh = sinfo.bh;
        if (!de) return -UFS_EIO;

        struct ufs_timespec ts;
        ts.tv_sec = time(NULL);
        ts.tv_nsec = 0;
        fat_time_unix2fat(sbi, &ts, &fat_time_val, &fat_date_val, NULL);
        de->time = cpu_to_le16(fat_time_val);
        de->date = cpu_to_le16(fat_date_val);
        de->size = cpu_to_le32((u32)inode->i_size);
        ufs_mark_buf_dirty(bh);
        ufs_sync_buf(bh);
        ufs_brelse(bh);
    }

    return 0;
}

int fat_sync_inode(struct ufs_inode *inode)
{
    return fat_write_inode(inode);
}

/* -------- Statfs -------- */

static int fat_statfs(struct ufs_super_block *sb, struct ufs_statfs *buf)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);

    memset(buf, 0, sizeof(*buf));
    buf->f_bsize = sb->s_blocksize;
    buf->f_blocks = (unsigned long)((sb->s_bdev->total_sectors *
                    sb->s_bdev->sector_size) / sbi->cluster_size);
    buf->f_bfree = (unsigned long)sbi->free_clusters;
    buf->f_bavail = (unsigned long)sbi->free_clusters;
    buf->f_files = (unsigned long)sbi->max_cluster;
    buf->f_ffree = (unsigned long)sbi->free_clusters;
    buf->f_namelen = 256;
    return 0;
}

/* -------- Super operations for FAT -------- */

struct ufs_super_operations fat_sops = {
    .alloc_inode    = fat_alloc_inode,
    .statfs         = fat_statfs,
};
