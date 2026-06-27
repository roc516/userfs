/*
 * vfs_file.c - FAT file operations (read/write/truncate)
 *
 * Ported from Linux kernel fs/fat/file.c and fs/fat/inode.c (part)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fat_core.h"

/* -------- Read from a FAT file -------- */

static ssize_t fat_file_read(struct ufs_file *file, void *buf, size_t count,
                              loff_t offset)
{
    struct ufs_inode *inode = file->f_inode;
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    size_t total = 0;
    unsigned char *ptr = (unsigned char *)buf;

    if (offset >= inode->i_size)
        return 0;

    if (offset + (loff_t)count > inode->i_size)
        count = (size_t)(inode->i_size - offset);

    size_t     remaining = count;

    while (remaining > 0) {
        sector_t sector, phys;
        unsigned long mapped_blocks;
        int err;
        size_t to_read;
        loff_t block_offset;

        /* Calculate sector */
        sector = (sector_t)(offset >> sb->s_blocksize_bits);

        err = fat_bmap(inode, sector, &phys, &mapped_blocks, 0);
        if (err || !phys) break;

        block_offset = offset & (sb->s_blocksize - 1);
        to_read = sb->s_blocksize - (size_t)block_offset;
        if (to_read > remaining) to_read = remaining;

        struct ufs_buf *bh = ufs_bread(sb->s_bdev, phys);
        if (!bh) break;

        memcpy(ptr, bh->b_data + block_offset, to_read);
        ufs_brelse(bh);

        ptr += to_read;
        offset += (loff_t)to_read;
        remaining -= to_read;
        total += to_read;
    }

    return (ssize_t)total;
}

/* -------- Write to a FAT file -------- */

static int fat_add_cluster(struct ufs_inode *inode)
{
    struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
    int cluster, err;

    err = fat_alloc_clusters(inode, &cluster, 1);
    if (err < 0) return err;

    err = fat_chain_add(inode, cluster, 1);
    if (err < 0) {
        fat_free_clusters(inode, cluster);
        return err;
    }

    MSDOS_I(inode)->mmu_private += sbi->cluster_size;
    return cluster;
}

static ssize_t fat_file_write(struct ufs_file *file, const void *buf,
                               size_t count, loff_t offset)
{
    struct ufs_inode *inode = file->f_inode;
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    size_t remaining = count;
    size_t total = 0;
    const unsigned char *ptr = (const unsigned char *)buf;

    while (remaining > 0) {
        sector_t sector, phys;
        unsigned long mapped_blocks;
        int err;
        size_t to_write;
        loff_t block_offset;

        sector = (sector_t)(offset >> sb->s_blocksize_bits);
        err = fat_bmap(inode, sector, &phys, &mapped_blocks, 1);
        if (err || !phys) {
            /* Need to allocate space */

            /* Find which cluster this sector falls in */
            int cluster_idx = (int)(sector / sbi->sec_per_clus);
            int fclus, dclus;

            err = fat_get_cluster(inode, cluster_idx, &fclus, &dclus);
            if (err == FAT_ENT_EOF) {
                /* Allocate new cluster */
                fat_add_cluster(inode);
                continue;  /* retry */
            } else if (err < 0) {
                break;
            }

            if (dclus == 0 && MSDOS_I(inode)->i_start == 0) {
                /* First allocation */
                fat_add_cluster(inode);
                continue;
            }

            /* Try again */
            err = fat_bmap(inode, sector, &phys, &mapped_blocks, 1);
            if (err || !phys) {
                /* Still can't map - allocate clusters */
                int new_cluster;
                err = fat_alloc_clusters(inode, &new_cluster, 1);
                if (err < 0) break;
                err = fat_chain_add(inode, new_cluster, 1);
                if (err < 0) { fat_free_clusters(inode, new_cluster); break; }
                continue;
            }
        }

        block_offset = offset & (sb->s_blocksize - 1);
        to_write = sb->s_blocksize - (size_t)block_offset;
        if (to_write > remaining) to_write = remaining;

        struct ufs_buf *bh = ufs_bread(sb->s_bdev, phys);
        if (!bh) break;

        memcpy(bh->b_data + block_offset, ptr, to_write);
        ufs_mark_buf_dirty(bh);
        ufs_sync_buf(bh);
        ufs_brelse(bh);

        ptr += to_write;
        offset += (loff_t)to_write;
        remaining -= to_write;
        total += to_write;

        /* Update size */
        if (offset > inode->i_size) {
            inode->i_size = offset;
            MSDOS_I(inode)->mmu_private = offset;
        }
    }

    return (ssize_t)total;
}

/* -------- Truncate -------- */

static int fat_free(struct ufs_inode *inode, int skip_start)
{
    int cluster = MSDOS_I(inode)->i_start;
    int err = 0;

    if (!cluster) return 0;

    if (!skip_start) {
        err = fat_free_clusters(inode, cluster);
        if (err == 0) {
            MSDOS_I(inode)->i_start = 0;
            MSDOS_I(inode)->i_logstart = 0;
            MSDOS_I(inode)->mmu_private = 0;
            inode->i_blocks = 0;
            inode->i_size = 0;
        }
    }
    return err;
}

void fat_truncate_blocks(struct ufs_inode *inode, loff_t offset)
{
    if (offset == 0) {
        fat_free(inode, 0);
    }
}

/* -------- Fsync -------- */

static int fat_file_fsync(struct ufs_file *file)
{
    struct ufs_inode *inode = file->f_inode;

    if (inode->i_sb->s_bdev) {
        return ufs_bcache_flush(inode->i_sb->s_bdev);
    }
    return 0;
}

/* -------- setattr / getattr -------- */

int fat_setattr(struct ufs_inode *inode, struct ufs_iattr *attr)
{
    if (attr->ia_valid & UFS_ATTR_SIZE) {
        if (attr->ia_size < inode->i_size) {
            fat_truncate_blocks(inode, attr->ia_size);
        }
        inode->i_size = attr->ia_size;
    }
    return 0;
}

int fat_getattr(struct ufs_inode *inode, struct ufs_stat *stat)
{
    memset(stat, 0, sizeof(*stat));
    stat->st_ino = inode->i_ino;
    stat->st_mode = inode->i_mode;
    stat->st_nlink = inode->i_nlink;
    stat->st_size = inode->i_size;
    stat->st_blksize = inode->i_sb->s_blocksize;
    stat->st_blocks = inode->i_blocks;
    return 0;
}

/* -------- Release / close -------- */

static int fat_file_release(struct ufs_inode *inode, struct ufs_file *file)
{
    return fat_write_inode(inode);
}

/* -------- File operations table -------- */

const struct ufs_file_operations fat_file_operations = {
    .read   = fat_file_read,
    .write  = fat_file_write,
    .fsync  = fat_file_fsync,
    .release = fat_file_release,
};
