/*
 * fat_fs.c - FAT filesystem type registration and mount
 *
 * Ported from Linux kernel fs/fat/inode.c
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdlib.h>
#include <string.h>
#include "fat_core.h"
#include "internal.h"

/* -------- Read BPB from boot sector -------- */

struct fat_bios_param_block {
    u16     fat_sector_size;
    u8      fat_sec_per_clus;
    u16     fat_reserved;
    u8      fat_fats;
    u16     fat_dir_entries;
    u16     fat_sectors;
    u16     fat_fat_length;
    u32     fat_total_sect;

    u8      fat16_state;
    u32     fat16_vol_id;

    u32     fat32_length;
    u32     fat32_root_cluster;
    u16     fat32_info_sector;
    u8      fat32_state;
    u32     fat32_vol_id;
};

static int fat_read_bpb(struct ufs_bdev *bdev, struct fat_bios_param_block *bpb)
{
    struct ufs_buf *bh;
    struct fat_boot_sector *bs;


    memset(bpb, 0, sizeof(*bpb));

    bh = ufs_bread(bdev, 0);
    if (!bh) return -UFS_EIO;

    bs = (struct fat_boot_sector *)bh->b_data;

    /* Validate boot sector */
    if (bs->ignored[0] != 0xEB && bs->ignored[0] != 0xE9 &&
        bs->ignored[0] != 0xE8) {
        /* Not a valid FAT boot sector */
        ufs_brelse(bh);
        return -UFS_ENOENT;
    }

    bpb->fat_sector_size  = le16_to_cpu(*(__le16 *)bs->sector_size);
    bpb->fat_sec_per_clus = bs->sec_per_clus;
    bpb->fat_reserved     = le16_to_cpu(bs->reserved);
    bpb->fat_fats         = bs->fats;
    bpb->fat_dir_entries  = le16_to_cpu(*(__le16 *)bs->dir_entries);
    bpb->fat_sectors      = le16_to_cpu(*(__le16 *)bs->sectors);
    bpb->fat_fat_length   = le16_to_cpu(bs->fat_length);
    bpb->fat_total_sect   = le32_to_cpu(bs->total_sect);

    /* If total_sect == 0, try sectors field */
    if (bpb->fat_total_sect == 0)
        bpb->fat_total_sect = bpb->fat_sectors;

    /* Determine if FAT32 by checking fat_length == 0 */
    if (bpb->fat_fat_length == 0 && bpb->fat_total_sect > 0) {
        /* Likely FAT32 */
        bpb->fat32_length       = le32_to_cpu(bs->fat32.length);
        bpb->fat32_root_cluster = le32_to_cpu(bs->fat32.root_cluster);
        bpb->fat32_info_sector  = le16_to_cpu(bs->fat32.info_sector);
        bpb->fat32_state        = bs->fat32.state;
        bpb->fat32_vol_id       = le32_to_cpu(*(__le32 *)bs->fat32.vol_id);
        bpb->fat16_state        = 0;
        bpb->fat16_vol_id       = 0;
    } else {
        /* FAT12/FAT16 */
        bpb->fat16_state  = bs->fat16.state;
        bpb->fat16_vol_id = le32_to_cpu(*(__le32 *)bs->fat16.vol_id);
    }

    ufs_brelse(bh);
    return 0;
}

/* -------- Fill in msdos_sb_info -------- */

static int fat_set_sb_info(struct ufs_super_block *sb,
                            struct fat_bios_param_block *bpb)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    unsigned int logical_sector_size = bpb->fat_sector_size;
    unsigned int total_sectors;
    unsigned long root_dir_sectors, total_clusters;

    if (logical_sector_size < 512 || logical_sector_size > 4096)
        return -UFS_EINVAL;

    sb->s_blocksize = logical_sector_size;
    sb->s_blocksize_bits = 9;
    while ((1UL << sb->s_blocksize_bits) < logical_sector_size)
        sb->s_blocksize_bits++;
    sb->s_bdev->sector_size = logical_sector_size;

    /* Set sector size for cache */
    sbi->sec_per_clus = bpb->fat_sec_per_clus;
    sbi->cluster_size = sbi->sec_per_clus * logical_sector_size;
    sbi->cluster_bits = 0;
    while ((1UL << sbi->cluster_bits) < sbi->cluster_size)
        sbi->cluster_bits++;

    sbi->fats = bpb->fat_fats;
    sbi->fat_start = bpb->fat_reserved;

    if (bpb->fat_fat_length) {
        /* FAT12/FAT16 */
        sbi->fat_length = bpb->fat_fat_length;
        sbi->dir_entries = bpb->fat_dir_entries;
        sbi->dir_start = sbi->fat_start + sbi->fats * sbi->fat_length;
        sbi->root_cluster = 0;

        total_sectors = bpb->fat_total_sect;
        root_dir_sectors = (sbi->dir_entries * sizeof(struct msdos_dir_entry)
                           + logical_sector_size - 1) / logical_sector_size;
        sbi->data_start = sbi->dir_start + root_dir_sectors;

        total_clusters = (total_sectors - sbi->data_start) / sbi->sec_per_clus;
        sbi->max_cluster = total_clusters + FAT_START_ENT;

        /* Determine FAT bits */
        if (total_clusters < 4085)
            sbi->fat_bits = 12;
        else if (total_clusters < 65525)
            sbi->fat_bits = 16;
        else
            sbi->fat_bits = 32;

        if (bpb->fat16_vol_id)
            sbi->vol_id = bpb->fat16_vol_id;
    } else {
        /* FAT32 */
        sbi->fat_length = bpb->fat32_length;
        sbi->dir_entries = 0;
        sbi->dir_start = sbi->fat_start + sbi->fats * sbi->fat_length;

        total_sectors = bpb->fat_total_sect;
        root_dir_sectors = 0;
        sbi->data_start = sbi->dir_start;
        sbi->root_cluster = bpb->fat32_root_cluster;

        total_clusters = (total_sectors - sbi->data_start) / sbi->sec_per_clus;
        sbi->max_cluster = total_clusters + FAT_START_ENT;
        sbi->fat_bits = 32;
        sbi->fsinfo_sector = bpb->fat32_info_sector;

        if (bpb->fat32_vol_id)
            sbi->vol_id = bpb->fat32_vol_id;
    }

    sbi->dir_per_block = sb->s_blocksize / sizeof(struct msdos_dir_entry);
    sbi->dir_per_block_bits = 0;
    while ((1UL << sbi->dir_per_block_bits) < (unsigned int)sbi->dir_per_block)
        sbi->dir_per_block_bits++;

    sbi->prev_free = FAT_START_ENT;
    sbi->free_clusters = (unsigned int)-1;
    sbi->free_clus_valid = 0;

    /* Default mount options */
    memset(&sbi->options, 0, sizeof(sbi->options));
    sbi->options.fs_uid = 0;
    sbi->options.fs_gid = 0;
    sbi->options.fs_fmask = 0;
    sbi->options.fs_dmask = 0;
    sbi->options.rodir = 1;
    sbi->options.dotsOK = 1;

    pthread_mutex_init(&sbi->fat_lock, NULL);
    pthread_mutex_init(&sbi->nfs_build_inode_lock, NULL);
    pthread_mutex_init(&sbi->s_lock, NULL);

    return 0;
}

/* -------- Setup function for VFAT -------- */

static void vfat_setup(struct ufs_super_block *sb)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    sbi->dir_ops = &vfat_dir_inode_operations;
    sbi->options.isvfat = 1;
    sbi->options.utf8 = 1;
}

/* -------- Fills the super block -------- */

int fat_fill_super(struct ufs_super_block *sb,
                   void (*setup)(struct ufs_super_block *))
{
    struct fat_bios_param_block bpb;
    struct msdos_sb_info *sbi;
    struct ufs_inode *root_inode;
    int err;

    err = fat_read_bpb(sb->s_bdev, &bpb);
    if (err) return err;

    sbi = (struct msdos_sb_info *)calloc(1, sizeof(*sbi));
    if (!sbi) return -UFS_ENOMEM;
    sb->s_fs_info = sbi;

    /* Set defaults before BPB parsing */
    sbi->fsinfo_sector = 1;  /* Typically sector 1 for FAT32 */

    err = fat_set_sb_info(sb, &bpb);
    if (err) { free(sbi); sb->s_fs_info = NULL; return err; }

    /* Need to re-set s_fs_info since fat_set_sb_info uses MSDOS_SB */
    sb->s_fs_info = sbi;

    sb->s_magic = 0x4d44;  /* MSDOS_SUPER_MAGIC */
    sb->s_op = NULL;  /* We'll set this later */

    /* Default setup (VFAT style) */
    if (setup) setup(sb);

    /* Set super operations */
    sb->s_op = &fat_sops;

    /* Initialize FAT entry access */
    fat_ent_access_init(sb);

    /* Read FSINFO for free cluster count (FAT32 only) */
    if (is_fat32(sbi)) {
        struct ufs_buf *info_bh;
        info_bh = ufs_bread(sb->s_bdev, sbi->fsinfo_sector);
        if (info_bh) {
            u32 free_count = le32_to_cpu(*(__le32 *)(info_bh->b_data + 488));
            u32 next_free = le32_to_cpu(*(__le32 *)(info_bh->b_data + 492));
            if (free_count > 0 && free_count < sbi->max_cluster)
                sbi->free_clusters = (unsigned int)free_count;
            sbi->prev_free = next_free > FAT_START_ENT ? (int)next_free : FAT_START_ENT;
            ufs_brelse(info_bh);
        }
    }

    /* Create root inode */
    root_inode = fat_alloc_inode(sb);
    if (!root_inode) { free(sbi); sb->s_fs_info = NULL; return -UFS_ENOMEM; }

    root_inode->i_ino = MSDOS_ROOT_INO;
    root_inode->i_mode = S_IFDIR | 0755;
    root_inode->i_nlink = 1;

    if (is_fat32(sbi)) {
        MSDOS_I(root_inode)->i_start = sbi->root_cluster;
        root_inode->i_size = sbi->cluster_size;
    } else {
        MSDOS_I(root_inode)->i_start = 0;
        root_inode->i_size = sbi->dir_entries * sizeof(struct msdos_dir_entry);
    }
    MSDOS_I(root_inode)->i_logstart = MSDOS_I(root_inode)->i_start;
    MSDOS_I(root_inode)->mmu_private = sbi->cluster_size;

    root_inode->i_op = (struct ufs_inode_operations *)sbi->dir_ops;
    root_inode->i_fop = (struct ufs_file_operations *)&fat_dir_operations;

    fat_attach(root_inode, 0);

    /* Scan FAT to count free clusters if FSINFO wasn't available (FAT12/16) */
    if (sbi->free_clusters == (unsigned int)-1) {
        unsigned long free_count = 0;
        struct fat_entry fatent;
        int entry;

        fatent_init(&fatent);
        for (entry = FAT_START_ENT; entry < (int)sbi->max_cluster; entry++) {
            int ret = fat_ent_read(root_inode, &fatent, entry);
            if (ret == FAT_ENT_FREE)
                free_count++;
        }
        fatent_brelse(&fatent);
        sbi->free_clusters = (unsigned int)free_count;
        if (free_count > 0)
            sbi->free_clus_valid = 1;
    }

    /* Set as root */
    sb->s_fs_info = sbi;
    snprintf(sb->s_id, sizeof(sb->s_id), "ufs:fat");

    return 0;
}

/* -------- Mount callback for VFAT -------- */

static int fat_mount(struct ufs_bdev *bdev, struct ufs_super_block **sb_out)
{
    struct ufs_super_block *sb = *sb_out;
    int err;

    /* Initialize the super block */
    ufs_super_init(sb, bdev, &ufs_fat_fs_type);

    /* Fill the FAT-specific parts */
    err = fat_fill_super(sb, vfat_setup);
    if (err) {
        ufs_log_err("FAT mount failed: %d", err);
        return err;
    }

    return 0;
}

/* -------- Kill super block -------- */

static void fat_kill_sb(struct ufs_super_block *sb)
{
    if (!sb) return;

    /* Flush FSINFO if dirty */
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    if (sbi && is_fat32(sbi))
        fat_clusters_flush(sb);

    /* Evict all inodes - simplified */
    /* Free msdos_sb_info */
    if (sbi) {
        pthread_mutex_destroy(&sbi->fat_lock);
        pthread_mutex_destroy(&sbi->nfs_build_inode_lock);
        pthread_mutex_destroy(&sbi->s_lock);
        free(sbi);
        sb->s_fs_info = NULL;
    }
}

/* -------- Filesystem type registration -------- */

struct ufs_filesystem_type ufs_fat_fs_type = {
    .name    = "vfat",
    .mount   = fat_mount,
    .kill_sb = fat_kill_sb,
    .next    = NULL,
};
