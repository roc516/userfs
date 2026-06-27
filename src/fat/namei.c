/*
 * namei.c - VFAT name/inode operations
 *
 * Ported from Linux kernel fs/fat/namei_vfat.c
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fat_core.h"
#include <ctype.h>

/* -------- Helpers -------- */

static int vfat_build_sfn(const char *name, int len,
                          unsigned char *sfn, int *sfn_len,
                          struct fat_mount_options *opts)
{
    int i, j, dot_pos = -1;

    /* Find last dot */
    for (i = len - 1; i >= 0; i--) {
        if (name[i] == '.') { dot_pos = i; break; }
    }

    /* Build 8-char base (name part before dot) */
    int base_len = (dot_pos >= 0) ? dot_pos : len;
    if (base_len > 8) base_len = 8;

    memset(sfn, ' ', 11);
    for (i = 0; i < base_len; i++) {
        unsigned char c = (unsigned char)toupper(name[i]);
        if (c == ' ' || c == '.') c = '_';
        sfn[i] = c;
    }

    /* Build 3-char extension */
    if (dot_pos >= 0) {
        int ext_len = len - dot_pos - 1;
        if (ext_len > 3) ext_len = 3;
        for (i = 0; i < ext_len; i++)
            sfn[8 + i] = (unsigned char)toupper(name[dot_pos + 1 + i]);
    }

    *sfn_len = 11;
    return 0;
}

/* -------- Lookup -------- */

static int vfat_lookup(struct ufs_inode *dir, const char *name,
                       struct ufs_inode **out)
{
    struct ufs_super_block *sb = dir->i_sb;
    struct fat_slot_info sinfo;
    int err;

    memset(&sinfo, 0, sizeof(sinfo));
    err = fat_search_long(dir, (const unsigned char *)name,
                          (int)strlen(name), &sinfo);
    if (err < 0) return err;

    struct ufs_inode *inode = fat_build_inode(sb, sinfo.de, sinfo.i_pos);
    if (!inode) return -UFS_ENOMEM;

    ufs_brelse(sinfo.bh);
    *out = inode;
    return 0;
}

/* -------- Create -------- */

static int vfat_create(struct ufs_inode *dir, const char *name, umode_t mode,
                       struct ufs_inode **out)
{
    struct ufs_super_block *sb = dir->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct ufs_inode *inode;
    struct msdos_dir_entry de;
    unsigned char sfn[11];
    int sfn_len, err;
    u16 fat_time2, fat_date2;
    struct ufs_timespec ts;

    /* Build 8.3 short name */
    vfat_build_sfn(name, (int)strlen(name), sfn, &sfn_len, &sbi->options);

    /* Allocate inode */
    inode = fat_alloc_inode(sb);
    if (!inode) return -UFS_ENOMEM;

    /* Allocate first cluster */
    int cluster;
    err = fat_alloc_clusters(inode, &cluster, 1);
    if (err < 0) { fat_free_inode(inode); return err; }

    /* Build directory entry */
    memset(&de, 0, sizeof(de));
    memcpy(de.name, sfn, 11);
    de.attr = ATTR_ARCH;
    de.ctime_cs = 0;

    ts.tv_sec = time(NULL);
    ts.tv_nsec = 0;
    fat_time_unix2fat(sbi, &ts, &fat_time2, &fat_date2, NULL);

    de.time = cpu_to_le16(fat_time2);
    de.date = cpu_to_le16(fat_date2);
    de.ctime = cpu_to_le16(fat_time2);
    de.cdate = cpu_to_le16(fat_date2);
    de.adate = cpu_to_le16(fat_date2);
    fat_set_start(&de, cluster);
    de.size = cpu_to_le32(0);

    /* Add directory entry to parent */
    struct fat_slot_info sinfo_create;
    memset(&sinfo_create, 0, sizeof(sinfo_create));
    err = fat_add_entries(dir, &de, 1, &sinfo_create);
    if (err < 0) {
        fat_free_clusters(inode, cluster);
        fat_free_inode(inode);
        return err;
    }

    inode->i_mode = S_IFREG | 0644;
    inode->i_size = 0;
    MSDOS_I(inode)->i_start = cluster;
    MSDOS_I(inode)->i_logstart = cluster;
    MSDOS_I(inode)->mmu_private = 0;
    MSDOS_I(inode)->i_pos = sinfo_create.slot_off;
    MSDOS_I(inode)->i_parent_dir_ino = dir->i_ino;
    inode->i_op = NULL;
    inode->i_fop = (struct ufs_file_operations *)&fat_file_operations;

    fat_attach(inode, sinfo_create.slot_off);

    ufs_brelse(sinfo_create.bh);
    *out = inode;
    return 0;
}

/* -------- Unlink (remove file) -------- */

static int vfat_unlink(struct ufs_inode *dir, const char *name)
{
    struct fat_slot_info sinfo;
    struct ufs_inode *inode;
    int err;

    memset(&sinfo, 0, sizeof(sinfo));
    err = fat_search_long(dir, (const unsigned char *)name,
                          (int)strlen(name), &sinfo);
    if (err < 0) return err;

    inode = fat_build_inode(dir->i_sb, sinfo.de, sinfo.i_pos);
    if (!inode) { ufs_brelse(sinfo.bh); return -UFS_ENOMEM; }

    /* Free clusters */
    if (MSDOS_I(inode)->i_start)
        fat_free_clusters(inode, MSDOS_I(inode)->i_start);

    /* Remove directory entry */
    err = fat_remove_entries(dir, &sinfo);
    ufs_brelse(sinfo.bh);

    inode->i_nlink = 0;
    return err;
}

/* -------- Mkdir -------- */

static int vfat_mkdir(struct ufs_inode *dir, const char *name, umode_t mode,
                      struct ufs_inode **out)
{
    struct ufs_super_block *sb = dir->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct ufs_inode *inode;
    struct msdos_dir_entry de;
    unsigned char sfn[11];
    int sfn_len, cluster, err;
    u16 fat_time2, fat_date2;
    struct ufs_timespec ts;

    vfat_build_sfn(name, (int)strlen(name), sfn, &sfn_len, &sbi->options);

    inode = fat_alloc_inode(sb);
    if (!inode) return -UFS_ENOMEM;

    ts.tv_sec = time(NULL);
    ts.tv_nsec = 0;

    cluster = fat_alloc_new_dir(dir, &ts);
    if (cluster < 0) { fat_free_inode(inode); return cluster; }

    memset(&de, 0, sizeof(de));
    memcpy(de.name, sfn, 11);
    de.attr = ATTR_DIR;
    fat_time_unix2fat(sbi, &ts, &fat_time2, &fat_date2, NULL);
    de.time = cpu_to_le16(fat_time2);
    de.date = cpu_to_le16(fat_date2);
    de.ctime = cpu_to_le16(fat_time2);
    de.cdate = cpu_to_le16(fat_date2);
    de.adate = cpu_to_le16(fat_date2);
    fat_set_start(&de, cluster);
    de.size = cpu_to_le32(0);

    err = fat_add_entries(dir, &de, 1, NULL);
    if (err < 0) {
        fat_free_clusters(inode, cluster);
        fat_free_inode(inode);
        return err;
    }

    inode->i_mode = S_IFDIR | 0755;
    inode->i_size = sbi->cluster_size;
    MSDOS_I(inode)->i_start = cluster;
    MSDOS_I(inode)->i_logstart = cluster;

    fat_attach(inode, 0);

    if (out) *out = inode;
    return 0;
}

/* -------- Rmdir -------- */

static int vfat_rmdir(struct ufs_inode *dir, const char *name)
{
    struct fat_slot_info sinfo;
    struct ufs_inode *inode;
    int err;

    memset(&sinfo, 0, sizeof(sinfo));
    err = fat_search_long(dir, (const unsigned char *)name,
                          (int)strlen(name), &sinfo);
    if (err < 0) return err;

    inode = fat_build_inode(dir->i_sb, sinfo.de, sinfo.i_pos);
    if (!inode) { ufs_brelse(sinfo.bh); return -UFS_ENOMEM; }

    if (!S_ISDIR(inode->i_mode)) {
        ufs_brelse(sinfo.bh);
        return -UFS_ENOTDIR;
    }

    if (!fat_dir_empty(inode)) {
        ufs_brelse(sinfo.bh);
        return -UFS_ENOTEMPTY;
    }

    if (MSDOS_I(inode)->i_start)
        fat_free_clusters(inode, MSDOS_I(inode)->i_start);

    err = fat_remove_entries(dir, &sinfo);
    ufs_brelse(sinfo.bh);

    inode->i_nlink = 0;
    return err;
}

/* -------- Rename -------- */

static int vfat_rename2(struct ufs_inode *old_dir, const char *old_name,
                        struct ufs_inode *new_dir, const char *new_name)
{
    struct ufs_super_block *sb = old_dir->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct fat_slot_info old_sinfo, new_sinfo;
    struct msdos_dir_entry de;
    unsigned char sfn[11];
    int sfn_len, err;
    u16 fat_time2, fat_date2;
    struct ufs_timespec ts;

    /* Find old entry */
    memset(&old_sinfo, 0, sizeof(old_sinfo));
    err = fat_search_long(old_dir, (const unsigned char *)old_name,
                          (int)strlen(old_name), &old_sinfo);
    if (err < 0) return err;

    /* Check if target exists */
    memset(&new_sinfo, 0, sizeof(new_sinfo));
    err = fat_search_long(new_dir, (const unsigned char *)new_name,
                          (int)strlen(new_name), &new_sinfo);
    if (err == 0) {
        /* Target exists - remove it first */
        /* For now, just unlink */
        fat_remove_entries(new_dir, &new_sinfo);
        ufs_brelse(new_sinfo.bh);
    }

    /* Build new short name */
    vfat_build_sfn(new_name, (int)strlen(new_name), sfn, &sfn_len, &sbi->options);

    /* Copy the directory entry with updated name */
    memcpy(&de, old_sinfo.de, sizeof(de));
    memcpy(de.name, sfn, 11);

    ts.tv_sec = time(NULL);
    ts.tv_nsec = 0;
    fat_time_unix2fat(sbi, &ts, &fat_time2, &fat_date2, NULL);
    de.time = cpu_to_le16(fat_time2);
    de.date = cpu_to_le16(fat_date2);

    /* Add to new dir */
    err = fat_add_entries(new_dir, &de, 1, NULL);
    if (err < 0) { ufs_brelse(old_sinfo.bh); return err; }

    /* Remove from old dir */
    err = fat_remove_entries(old_dir, &old_sinfo);
    ufs_brelse(old_sinfo.bh);

    return err;
}

/* -------- Inode operations table -------- */

const struct ufs_inode_operations vfat_dir_inode_operations = {
    .lookup     = vfat_lookup,
    .create     = vfat_create,
    .unlink     = vfat_unlink,
    .mkdir      = vfat_mkdir,
    .rmdir      = vfat_rmdir,
    .rename     = vfat_rename2,
    .setattr    = fat_setattr,
    .getattr    = fat_getattr,
};
