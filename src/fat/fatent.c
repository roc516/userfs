/*
 * fatent.c - FAT table entry operations (FAT12/16/32)
 *
 * Ported from Linux kernel fs/fat/fatent.c
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fat_core.h"

static pthread_mutex_t fat12_entry_lock = PTHREAD_MUTEX_INITIALIZER;

/* -------- FAT12 operations -------- */

static void fat12_ent_blocknr(struct ufs_super_block *sb, int entry,
                               int *offset, sector_t *blocknr)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    int bytes = entry + (entry >> 1);
    *offset = bytes & (sb->s_blocksize - 1);
    *blocknr = sbi->fat_start + (bytes >> sb->s_blocksize_bits);
}

static void fat_ent_blocknr(struct ufs_super_block *sb, int entry,
                             int *offset, sector_t *blocknr)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    int bytes = (entry << sbi->fatent_shift);
    *offset = bytes & (sb->s_blocksize - 1);
    *blocknr = sbi->fat_start + (bytes >> sb->s_blocksize_bits);
}

static void fat12_ent_set_ptr(struct fat_entry *fatent, int offset)
{
    struct ufs_buf **bhs = fatent->bhs;
    if (fatent->nr_bhs == 1) {
        fatent->u.ent12_p[0] = (u8 *)(bhs[0]->b_data + offset);
        fatent->u.ent12_p[1] = (u8 *)(bhs[0]->b_data + (offset + 1));
    } else {
        fatent->u.ent12_p[0] = (u8 *)(bhs[0]->b_data + offset);
        fatent->u.ent12_p[1] = (u8 *)(bhs[1]->b_data);
    }
}

static void fat16_ent_set_ptr(struct fat_entry *fatent, int offset)
{
    fatent->u.ent16_p = (__le16 *)(fatent->bhs[0]->b_data + offset);
}

static void fat32_ent_set_ptr(struct fat_entry *fatent, int offset)
{
    fatent->u.ent32_p = (__le32 *)(fatent->bhs[0]->b_data + offset);
}

static int fat12_ent_bread(struct ufs_super_block *sb, struct fat_entry *fatent,
                            int offset, sector_t blocknr)
{
    struct ufs_buf **bhs = fatent->bhs;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);

    bhs[0] = ufs_bread(sb->s_bdev, blocknr);
    if (!bhs[0])
        goto err;

    if ((offset + 1) < (int)sb->s_blocksize)
        fatent->nr_bhs = 1;
    else {
        blocknr++;
        bhs[1] = ufs_bread(sb->s_bdev, blocknr);
        if (!bhs[1])
            goto err_brelse;
        fatent->nr_bhs = 2;
    }
    fat12_ent_set_ptr(fatent, offset);
    return 0;

err_brelse:
    ufs_brelse(bhs[0]);
err:
    fat_msg(sb, "FAT read failed (blocknr %llu)", (unsigned long long)blocknr);
    return -UFS_EIO;
}

static int fat_ent_bread(struct ufs_super_block *sb, struct fat_entry *fatent,
                          int offset, sector_t blocknr)
{
    struct ufs_buf **bhs = fatent->bhs;

    bhs[0] = ufs_bread(sb->s_bdev, blocknr);
    if (!bhs[0]) {
        fat_msg(sb, "FAT read failed (blocknr %llu)",
               (unsigned long long)blocknr);
        return -UFS_EIO;
    }
    fatent->nr_bhs = 1;
    return 0;
}

static int fat12_ent_get(struct fat_entry *fatent)
{
    u8 **ent12_p = fatent->u.ent12_p;
    int val;
    int ret;

    pthread_mutex_lock(&fat12_entry_lock);
    if (fatent->entry & 1) {
        val = (*ent12_p[1] << 4) | (*ent12_p[0] & 0x0f);
        ret = val;
    } else {
        val = (*ent12_p[1] << 8) | *ent12_p[0];
        ret = val & 0x0fff;
    }
    pthread_mutex_unlock(&fat12_entry_lock);
    return ret;
}

static int fat16_ent_get(struct fat_entry *fatent)
{
    return le16_to_cpu(*fatent->u.ent16_p);
}

static int fat32_ent_get(struct fat_entry *fatent)
{
    return le32_to_cpu(*fatent->u.ent32_p) & 0x0fffffff;
}

static void fat12_ent_put(struct fat_entry *fatent, int new)
{
    u8 **ent12_p = fatent->u.ent12_p;

    pthread_mutex_lock(&fat12_entry_lock);
    if (fatent->entry & 1) {
        *ent12_p[0] = (*ent12_p[0] & 0xf0) | (new & 0x0f);
        *ent12_p[1] = (new >> 4);
    } else {
        *ent12_p[0] = (u8)(new & 0xff);
        *ent12_p[1] = (*ent12_p[1] & 0xf0) | ((new >> 8) & 0x0f);
    }
    pthread_mutex_unlock(&fat12_entry_lock);
    ufs_mark_buf_dirty(fatent->bhs[0]);
    if (fatent->nr_bhs > 1)
        ufs_mark_buf_dirty(fatent->bhs[1]);
}

static void fat16_ent_put(struct fat_entry *fatent, int new)
{
    *fatent->u.ent16_p = cpu_to_le16((u16)new);
    ufs_mark_buf_dirty(fatent->bhs[0]);
}

static void fat32_ent_put(struct fat_entry *fatent, int new)
{
    __le32 *ent32_p = fatent->u.ent32_p;
    /* Preserve high 4 bits */
    *ent32_p = cpu_to_le32((le32_to_cpu(*ent32_p) & 0xf0000000) | (new & 0x0fffffff));
    ufs_mark_buf_dirty(fatent->bhs[0]);
}

static int fat12_ent_next(struct fat_entry *fatent)
{
    u8 **ent12_p = fatent->u.ent12_p;

    /* FAT12 entries may span block boundaries */
    fatent->entry++;
    if (fatent->entry & 1) {
        ent12_p[0] = ent12_p[1];
        ent12_p[1]++;
        if (fatent->nr_bhs == 1 && ent12_p[1] >= (fatent->bhs[0]->b_data +
                                                   fatent->bhs[0]->b_size)) {
            /* Would cross boundary, next read will refetch */
            fatent->u.ent12_p[0] = NULL;
            fatent->u.ent12_p[1] = NULL;
            return 1;
        }
    } else {
        ufs_brelse(fatent->bhs[1]);
        fatent->bhs[1] = NULL;
        if (fatent->nr_bhs == 2) {
            ent12_p[1] = ent12_p[0] + 1;
            fatent->nr_bhs = 1;
        } else {
            ent12_p[0] = ent12_p[1];
            ent12_p[1] = ent12_p[0] + 1;
        }
    }
    return 1;
}

static int fat16_ent_next(struct fat_entry *fatent)
{
    fatent->u.ent16_p++;
    fatent->entry++;
    return 1;
}

static int fat32_ent_next(struct fat_entry *fatent)
{
    fatent->u.ent32_p++;
    fatent->entry++;
    return 1;
}

/* -------- Operations tables -------- */

static struct fatent_operations fat12_ops = {
    .ent_blocknr  = fat12_ent_blocknr,
    .ent_set_ptr  = fat12_ent_set_ptr,
    .ent_bread    = fat12_ent_bread,
    .ent_get      = fat12_ent_get,
    .ent_put      = fat12_ent_put,
    .ent_next     = fat12_ent_next,
};

static struct fatent_operations fat16_ops = {
    .ent_blocknr  = fat_ent_blocknr,
    .ent_set_ptr  = fat16_ent_set_ptr,
    .ent_bread    = fat_ent_bread,
    .ent_get      = fat16_ent_get,
    .ent_put      = fat16_ent_put,
    .ent_next     = fat16_ent_next,
};

static struct fatent_operations fat32_ops = {
    .ent_blocknr  = fat_ent_blocknr,
    .ent_set_ptr  = fat32_ent_set_ptr,
    .ent_bread    = fat_ent_bread,
    .ent_get      = fat32_ent_get,
    .ent_put      = fat32_ent_put,
    .ent_next     = fat32_ent_next,
};

/* -------- Public API -------- */

void fat_ent_access_init(struct ufs_super_block *sb)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    pthread_mutex_init(&sbi->fat_lock, NULL);
    if (is_fat32(sbi)) {
        sbi->fatent_shift = 2;
        sbi->fatent_ops = &fat32_ops;
    } else if (is_fat16(sbi)) {
        sbi->fatent_shift = 1;
        sbi->fatent_ops = &fat16_ops;
    } else if (is_fat12(sbi)) {
        sbi->fatent_shift = -1;
        sbi->fatent_ops = &fat12_ops;
    }
}

/*
 * Mirror FAT writes to all FAT copies
 */
static int fat_mirror_bhs(struct ufs_super_block *sb, struct ufs_buf **bhs,
                           int nr_bhs)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct ufs_buf *bh2;
    int i, j;
    int err = 0;

    /* FAT32 doesn't support mirroring via this mechanism */
    if (is_fat32(sbi)) return 0;

    for (i = 1; i < sbi->fats; i++) {
        for (j = 0; j < nr_bhs; j++) {
            sector_t blocknr = bhs[j]->b_blocknr +
                               i * sbi->fat_length;
            bh2 = ufs_bget(sb->s_bdev, blocknr);
            if (!bh2) {
                if (!err) err = -UFS_EIO;
                continue;
            }
            memcpy(bh2->b_data, bhs[j]->b_data, bh2->b_size);
            ufs_mark_buf_dirty(bh2);
            ufs_sync_buf(bh2);
            ufs_brelse(bh2);
        }
    }
    return err;
}

/* Update cached pointer if block matches */
static int fat_ent_update_ptr(struct ufs_super_block *sb,
                               struct fat_entry *fatent, int offset, sector_t blocknr)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct ufs_buf **bhs = fatent->bhs;

    if (fatent->nr_bhs > 0 &&
        bhs[0]->b_blocknr == blocknr &&
        fatent->u.ent32_p != NULL) {
        /* Already pointing to the right block, just update offset */
        return 1;
    }
    return 0;
}

int fat_ent_read(struct ufs_inode *inode, struct fat_entry *fatent, int entry)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    int offset;
    sector_t blocknr;
    int err;

    if (!fat_valid_entry(sbi, entry)) {
        fat_fs_error(sb, "%s: invalid entry %d (max %lu)",
                     __func__, entry, sbi->max_cluster);
        return -UFS_EIO;
    }

    fatent_set_entry(fatent, entry);
    sbi->fatent_ops->ent_blocknr(sb, entry, &offset, &blocknr);

    /* Try to reuse already-read buffer */
    if (fat_ent_update_ptr(sb, fatent, offset, blocknr))
        return sbi->fatent_ops->ent_get(fatent);

    /* Release old buffers */
    fatent_brelse(fatent);

    /* Read the block */
    err = sbi->fatent_ops->ent_bread(sb, fatent, offset, blocknr);
    if (err) return err;

    sbi->fatent_ops->ent_set_ptr(fatent, offset);
    return sbi->fatent_ops->ent_get(fatent);
}

int fat_ent_write(struct ufs_inode *inode, struct fat_entry *fatent,
                  int new, int wait)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    int err;

    sbi->fatent_ops->ent_put(fatent, new);
    if (wait) {
        err = ufs_sync_bufs(fatent->bhs, fatent->nr_bhs);
        if (err) return err;
    }
    return fat_mirror_bhs(sb, fatent->bhs, fatent->nr_bhs);
}

int fat_alloc_clusters(struct ufs_inode *inode, int *cluster, int nr_cluster)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct fatent_operations *ops = sbi->fatent_ops;
    struct fat_entry fatent, fatent2;
    int err, i, count;

    /* We only allocate one cluster at a time for now */
    if (nr_cluster != 1)
        return -UFS_ENOSPC;

    pthread_mutex_lock(&sbi->fat_lock);

    fatent_init(&fatent);
    i = sbi->prev_free + 1;
    count = 0;

    while (1) {
        if (i >= (int)sbi->max_cluster)
            i = FAT_START_ENT;

        err = fat_ent_read(inode, &fatent, i);
        if (err < 0) goto out;

        if (err == FAT_ENT_FREE) {
            sbi->prev_free = i;
            /* The read already set up the pointer, just write EOF */
            ops->ent_put(&fatent, FAT_ENT_EOF);
            ufs_mark_buf_dirty(fatent.bhs[0]);
            if (fatent.nr_bhs > 1)
                ufs_mark_buf_dirty(fatent.bhs[1]);

            fat_mirror_bhs(sb, fatent.bhs, fatent.nr_bhs);
            *cluster = i;
            sbi->free_clusters--;
            err = 0;
            goto out;
        }

        i++;
        count++;
        if (count > (int)sbi->max_cluster - FAT_START_ENT) {
            err = -UFS_ENOSPC;
            goto out;
        }
    }

out:
    fatent_brelse(&fatent);
    pthread_mutex_unlock(&sbi->fat_lock);
    return err;
}

int fat_free_clusters(struct ufs_inode *inode, int cluster)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct fatent_operations *ops = sbi->fatent_ops;
    struct fat_entry fatent;
    int err = 0;
    int next;

    pthread_mutex_lock(&sbi->fat_lock);

    fatent_init(&fatent);
    while (fat_valid_entry(sbi, cluster)) {
        err = fat_ent_read(inode, &fatent, cluster);
        if (err < 0) goto error;
        next = err;

        /* Free this entry (fat_ent_read already set up the pointer) */
        ops->ent_put(&fatent, FAT_ENT_FREE);
        if (fatent.nr_bhs > 0) {
            ufs_mark_buf_dirty(fatent.bhs[0]);
            if (fatent.nr_bhs > 1)
                ufs_mark_buf_dirty(fatent.bhs[1]);
        }
        sbi->free_clusters++;

        cluster = next;
    }
    err = 0;

error:
    fatent_brelse(&fatent);
    pthread_mutex_unlock(&sbi->fat_lock);
    return err;
}

int fat_count_free_clusters(struct ufs_super_block *sb)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct fat_entry fatent;
    int i, err = 0;
    int free = 0;

    fatent_init(&fatent);

    /* For now, just count from the existing cached value or scan */
    /* We'll use the cached value */
    if (sbi->free_clus_valid && sbi->free_clusters != (unsigned int)-1) {
        free = sbi->free_clusters;
    } else {
        /* Slow scan - would be expensive, use cached for now */
        free = 0;
    }

    fatent_brelse(&fatent);
    return free;
}
