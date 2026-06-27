/*
 * dir.c - FAT directory entry operations
 *
 * Ported from Linux kernel fs/fat/dir.c
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fat_core.h"
#include <strings.h>

/* -------- Helper: get next dir entry -------- */

static int fat__get_entry(struct ufs_inode *dir, loff_t *pos,
                          struct ufs_buf **bh, struct msdos_dir_entry **de)
{
    struct ufs_super_block *sb = dir->i_sb;
    sector_t phys, iblock;
    unsigned long mapped_blocks;
    int err, offset;

next:
    ufs_brelse(*bh);
    *bh = NULL;
    iblock = (sector_t)(*pos >> sb->s_blocksize_bits);
    err = fat_bmap(dir, iblock, &phys, &mapped_blocks, 0);
    if (err || !phys)
        return -1;  /* beyond EOF or error */

    *bh = ufs_bread(sb->s_bdev, phys);
    if (!*bh) {
        fat_msg(sb, "Directory bread(block %llu) failed",
               (unsigned long long)phys);
        *pos = (iblock + 1) << sb->s_blocksize_bits;
        goto next;
    }

    offset = (int)(*pos & (sb->s_blocksize - 1));
    *pos += sizeof(struct msdos_dir_entry);
    *de = (struct msdos_dir_entry *)((*bh)->b_data + offset);
    return 0;
}

static inline int fat_get_entry(struct ufs_inode *dir, loff_t *pos,
                                struct ufs_buf **bh,
                                struct msdos_dir_entry **de)
{
    if (*bh && *de &&
       (*de - (struct msdos_dir_entry *)(*bh)->b_data) <
                MSDOS_SB(dir->i_sb)->dir_per_block - 1) {
        *pos += sizeof(struct msdos_dir_entry);
        (*de)++;
        return 0;
    }
    return fat__get_entry(dir, pos, bh, de);
}

/* -------- Short name parsing -------- */

static int fat_parse_short(struct ufs_super_block *sb,
                            struct msdos_dir_entry *de,
                            unsigned char *name, int *p_len)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    int i, len;

    /* Build short name: base + '.' + ext (if ext non-blank) */
    for (i = 0; i < 8; i++) {
        if (de->name[i] == ' ')
            break;
        if (de->name[i] == 0x05)  /* 0x05 is encoding for 0xE5 (DELETED) */
            name[i] = 0xE5;
        else
            name[i] = de->name[i];
    }
    len = i;

    /* Extension */
    if (de->name[8] != ' ') {
        name[len++] = '.';
        for (i = 8; i < 11; i++) {
            if (de->name[i] == ' ')
                break;
            name[len++] = de->name[i];
        }
    }

    name[len] = '\0';
    if (p_len) *p_len = len;
    return 0;
}

/* -------- Long filename slot parsing -------- */

#define MAX_LFN_SLOTS  20

static int fat_parse_long(struct ufs_inode *dir, loff_t *pos,
                           struct ufs_buf **bh,
                           struct msdos_dir_entry **de,
                           wchar_t *unicode, int *unicode_len)
{
    struct msdos_dir_slot *slot;
    int slot_count, i, j, id;

    slot_count = 0;
    while (1) {
        slot = (struct msdos_dir_slot *)*de;
        id = slot->id;

        if (id & 0x40) {
            /* This is the last long entry (start of sequence) */
            slot_count = id & 0x3f;
            if (slot_count > MAX_LFN_SLOTS)
                return -UFS_EIO;
            *unicode_len = 0;
        } else if (id == 0) {
            /* End of LFN entries */
            break;
        } else {
            /* Continue accumulating */
        }

        /* Extract 13 chars from this slot */
        for (i = 0; i < 5; i++) {
            u16 c = le16_to_cpu(((__le16 *)slot->name0_4)[i]);
            if (c == 0) goto done;
            unicode[(*unicode_len)++] = (wchar_t)c;
        }
        for (i = 0; i < 6; i++) {
            u16 c = le16_to_cpu(((__le16 *)slot->name5_10)[i]);
            if (c == 0) goto done;
            unicode[(*unicode_len)++] = (wchar_t)c;
        }
        for (i = 0; i < 2; i++) {
            u16 c = le16_to_cpu(((__le16 *)slot->name11_12)[i]);
            if (c == 0) goto done;
            unicode[(*unicode_len)++] = (wchar_t)c;
        }

        if (fat_get_entry(dir, pos, bh, (struct msdos_dir_entry **)&slot) < 0)
            break;
    }

done:
    unicode[*unicode_len] = 0;
    return slot_count;
}

static int uni16_to_utf8(const wchar_t *uni, int len,
                          unsigned char *utf8, int utf8_len)
{
    int i, pos = 0;
    for (i = 0; i < len && uni[i] && pos < utf8_len - 4; i++) {
        unsigned int c = uni[i];
        if (c < 0x80) {
            utf8[pos++] = (unsigned char)c;
        } else if (c < 0x800) {
            utf8[pos++] = 0xC0 | (c >> 6);
            utf8[pos++] = 0x80 | (c & 0x3F);
        } else {
            utf8[pos++] = 0xE0 | (c >> 12);
            utf8[pos++] = 0x80 | ((c >> 6) & 0x3F);
            utf8[pos++] = 0x80 | (c & 0x3F);
        }
    }
    utf8[pos] = '\0';
    return pos;
}

static int utf8_to_uni16(const unsigned char *utf8, int len,
                          wchar_t *uni, int uni_len)
{
    int i = 0, pos = 0;
    while (i < len && pos < uni_len - 1) {
        unsigned int c = (unsigned char)utf8[i];
        if (c < 0x80) {
            uni[pos++] = (wchar_t)c;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= len) break;
            uni[pos++] = (wchar_t)(((c & 0x1F) << 6) | (utf8[i+1] & 0x3F));
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= len) break;
            uni[pos++] = (wchar_t)(((c & 0x0F) << 12) |
                                   ((utf8[i+1] & 0x3F) << 6) |
                                   (utf8[i+2] & 0x3F));
            i += 3;
        } else {
            i++; /* skip invalid */
        }
    }
    uni[pos] = 0;
    return pos;
}

/* -------- Search -------- */

int fat_search_long(struct ufs_inode *inode, const unsigned char *name,
                    int name_len, struct fat_slot_info *sinfo)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct ufs_buf *bh = NULL;
    struct msdos_dir_entry *de;
    loff_t pos;
    int err = -UFS_ENOENT;

    pos = 0;
    while (1) {
        err = fat_get_entry(inode, &pos, &bh, &de);
        if (err < 0) {
            err = -UFS_ENOENT;
            goto out;
        }

        /* Check for free entry (we need slot info for creation) */
        if (IS_FREE(de->name)) {
            if (sinfo && sinfo->i_pos == 0) {
                sinfo->slot_off = pos - sizeof(struct msdos_dir_entry);
                sinfo->bh = bh;
                sinfo->de = de;
                sinfo->nr_slots = 1;
                /* Continue searching for an existing entry */
            }
            continue;
        }

        /* Skip volume label */
        if (de->attr == ATTR_VOLUME)
            continue;

        /* Check if this is a long filename entry */
        if (de->attr == ATTR_EXT) {
            /* Skip VFAT long name entries - we match by short name */
            continue;
        }

        /* Short name match */
        if (de->attr & ATTR_VOLUME)
            continue;

        /* Compare with name */
        {
            unsigned char sname[13];
            int slen;
            fat_parse_short(sb, de, sname, &slen);
            if (strncasecmp((const char *)sname, (const char *)name, name_len) == 0
                && (int)strlen((const char *)sname) == name_len) {
                if (sinfo) {
                    sinfo->i_pos = pos - sizeof(struct msdos_dir_entry);
                    sinfo->slot_off = pos - sizeof(struct msdos_dir_entry);
                    sinfo->nr_slots = 1;
                    sinfo->de = de;
                    sinfo->bh = bh;
                    bh = NULL;
                }
                err = 0;
                goto out;
            }
        }
    }

out:
    ufs_brelse(bh);
    return err;
}

int fat_scan(struct ufs_inode *dir, const unsigned char *name,
             struct fat_slot_info *sinfo)
{
    /* fat_scan is just a convenience wrapper using pre-formatted name */
    return fat_search_long(dir, name, strlen((const char *)name), sinfo);
}

int fat_scan_logstart(struct ufs_inode *dir, int i_logstart,
                      struct fat_slot_info *sinfo)
{
    struct ufs_buf *bh = NULL;
    struct msdos_dir_entry *de;
    loff_t pos = 0;
    int err;

    while (1) {
        err = fat_get_entry(dir, &pos, &bh, &de);
        if (err < 0) {
            err = -UFS_ENOENT;
            break;
        }
        if (IS_FREE(de->name))
            continue;
        if (de->attr == ATTR_VOLUME || de->attr == ATTR_EXT)
            continue;

        if (fat_get_start(MSDOS_SB(dir->i_sb), de) == i_logstart) {
            if (sinfo) {
                sinfo->i_pos = pos - sizeof(struct msdos_dir_entry);
                sinfo->de = de;
                sinfo->bh = bh;
                bh = NULL;
            }
            err = 0;
            break;
        }
    }
    ufs_brelse(bh);
    return err;
}

int fat_get_dotdot_entry(struct ufs_inode *dir, struct ufs_buf **bh,
                          struct msdos_dir_entry **de)
{
    loff_t pos = 0;

    /* First two entries are "." and ".." */
    if (fat_get_entry(dir, &pos, bh, de) < 0)
        return -UFS_EIO;
    if (fat_get_entry(dir, &pos, bh, de) < 0)
        return -UFS_EIO;

    return 0;
}

int fat_dir_empty(struct ufs_inode *dir)
{
    struct ufs_buf *bh = NULL;
    struct msdos_dir_entry *de;
    loff_t pos = 0;
    int err;

    /* Skip "." and ".." */
    if (fat_get_entry(dir, &pos, &bh, &de) < 0) return 1;
    if (fat_get_entry(dir, &pos, &bh, &de) < 0) { ufs_brelse(bh); return 1; }

    while (1) {
        err = fat_get_entry(dir, &pos, &bh, &de);
        if (err < 0) { ufs_brelse(bh); return 1; }
        if (!IS_FREE(de->name)) {
            ufs_brelse(bh);
            return 0;
        }
    }
}

int fat_subdirs(struct ufs_inode *dir)
{
    struct ufs_buf *bh = NULL;
    struct msdos_dir_entry *de;
    loff_t pos = 0;
    int count = 0;

    if (fat_get_entry(dir, &pos, &bh, &de) < 0) goto out;
    if (fat_get_entry(dir, &pos, &bh, &de) < 0) goto out;

    while (fat_get_entry(dir, &pos, &bh, &de) >= 0) {
        if (!IS_FREE(de->name) && (de->attr & ATTR_DIR) &&
            de->name[0] != '.') {
            count++;
        }
    }
out:
    ufs_brelse(bh);
    return count;
}

/* -------- Add entries -------- */

int fat_add_entries(struct ufs_inode *dir, void *slots, int nr_slots,
                    struct fat_slot_info *sinfo)
{
    struct ufs_super_block *sb = dir->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct ufs_buf *bh = NULL;
    struct msdos_dir_entry *de;
    loff_t pos;
    int err, free_slots = 0;
    loff_t free_pos = 0;

    /* Find contiguous free slots */
    pos = 0;
    while (1) {
        int slot_off;
        err = fat_get_entry(dir, &pos, &bh, &de);
        if (err < 0) {
            /* End of directory reached. We need to extend the directory
             * by adding new clusters. For simplicity, attempt to allocate
             * and add one cluster. */
            break;
        }

        if (IS_FREE(de->name)) {
            if (free_slots == 0) {
                free_pos = pos - sizeof(struct msdos_dir_entry);
                if (sinfo) sinfo->slot_off = free_pos;
            }
            free_slots++;
            if (free_slots >= nr_slots) {
                /* Enough free slots found */
                goto write_entries;
            }
        } else {
            free_slots = 0;
        }
    }

    /* Need to extend directory */
    {
        int cluster;
        err = fat_alloc_clusters(dir, &cluster, 1);
        if (err < 0) { ufs_brelse(bh); return err; }

        err = fat_chain_add(dir, cluster, 1);
        if (err < 0) { fat_free_clusters(dir, cluster); ufs_brelse(bh); return err; }

        /* Zero out the new cluster */
        sector_t blknr = fat_clus_to_blknr(sbi, cluster);
        int i;
        for (i = 0; i < sbi->sec_per_clus; i++) {
            struct ufs_buf *zbh = ufs_bget(sb->s_bdev, blknr + i);
            if (zbh) {
                memset(zbh->b_data, 0, zbh->b_size);
                ufs_mark_buf_dirty(zbh);
                ufs_sync_buf(zbh);
                ufs_brelse(zbh);
            }
        }

        /* Now the first slot of the extended area */
        free_pos = dir->i_size;
        dir->i_size += sbi->cluster_size;
        free_slots = sbi->cluster_size / sizeof(struct msdos_dir_entry);
        if (sinfo) sinfo->slot_off = free_pos;
    }

write_entries:
    ufs_brelse(bh);

    /* Write the entries - free_pos is a logical byte offset, 
     * need fat_bmap to convert to physical block */
    {
        int i;
        for (i = 0; i < nr_slots; i++) {
            struct msdos_dir_entry *slot = &((struct msdos_dir_entry *)slots)[i];
            struct ufs_buf *wbh;
            sector_t phys;
            sector_t iblock;
            unsigned long mapped;
            int offset;

            iblock = (sector_t)((free_pos + i * sizeof(*slot)) >> sb->s_blocksize_bits);
            offset = (int)((free_pos + i * sizeof(*slot)) & (sb->s_blocksize - 1));

            err = fat_bmap(dir, iblock, &phys, &mapped, 0);
            if (err < 0) return err;
            if (phys == 0) return -UFS_EIO;

            wbh = ufs_bread(sb->s_bdev, phys);
            if (!wbh) return -UFS_EIO;

            memcpy(wbh->b_data + offset, slot, sizeof(*slot));
            ufs_mark_buf_dirty(wbh);
            ufs_sync_buf(wbh);
            ufs_brelse(wbh);
        }
    }

    if (sinfo) {
        sinfo->i_pos = free_pos;
        sinfo->nr_slots = nr_slots;
        /* Re-read for sinfo->de */
        sector_t iblock2 = (sector_t)(sinfo->slot_off >> sb->s_blocksize_bits);
        int offset = (int)(sinfo->slot_off & (sb->s_blocksize - 1));
        sector_t phys2;
        unsigned long mapped2;
        if (fat_bmap(dir, iblock2, &phys2, &mapped2, 0) == 0 && phys2) {
            sinfo->bh = ufs_bread(sb->s_bdev, phys2);
            if (sinfo->bh)
                sinfo->de = (struct msdos_dir_entry *)(sinfo->bh->b_data + offset);
        }
    }

    return 0;
}

int fat_remove_entries(struct ufs_inode *dir, struct fat_slot_info *sinfo)
{
    struct ufs_super_block *sb = dir->i_sb;
    int i;

    for (i = 0; i < sinfo->nr_slots; i++) {
        loff_t pos = sinfo->slot_off + i * sizeof(struct msdos_dir_entry);
        sector_t iblock = (sector_t)(pos >> sb->s_blocksize_bits);
        int offset = (int)(pos & (sb->s_blocksize - 1));
        struct ufs_buf *bh;
        sector_t phys;
        unsigned long mapped;
        int err;

        err = fat_bmap(dir, iblock, &phys, &mapped, 0);
        if (err < 0 || phys == 0) continue;

        bh = ufs_bread(sb->s_bdev, phys);
        if (!bh) continue;

        ((struct msdos_dir_entry *)(bh->b_data + offset))->name[0] = DELETED_FLAG;
        ufs_mark_buf_dirty(bh);
        ufs_sync_buf(bh);
        ufs_brelse(bh);
    }

    return 0;
}

/* -------- Allocate new directory -------- */

int fat_alloc_new_dir(struct ufs_inode *dir, struct ufs_timespec *ts)
{
    struct ufs_super_block *sb = dir->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    struct msdos_dir_entry *de;
    struct ufs_buf *bh;
    sector_t blknr;
    int cluster, err;
    u16 time, date;

    err = fat_alloc_clusters(dir, &cluster, 1);
    if (err < 0) return err;

    blknr = fat_clus_to_blknr(sbi, cluster);
    bh = ufs_bget(sb->s_bdev, blknr);
    if (!bh) { fat_free_clusters(dir, cluster); return -UFS_ENOMEM; }

    memset(bh->b_data, 0, bh->b_size);

    /* "." entry */
    de = (struct msdos_dir_entry *)bh->b_data;
    memset(de->name, ' ', MSDOS_NAME);
    de->name[0] = '.';
    de->attr = ATTR_DIR;
    fat_set_start(de, cluster);
    if (ts) {
        fat_time_unix2fat(sbi, ts, &time, &date, NULL);
        de->time = cpu_to_le16(time);
        de->date = cpu_to_le16(date);
        de->ctime = cpu_to_le16(time);
        de->cdate = cpu_to_le16(date);
    }

    /* ".." entry */
    de++;
    memset(de->name, ' ', MSDOS_NAME);
    de->name[0] = '.';
    de->name[1] = '.';
    de->attr = ATTR_DIR;
    fat_set_start(de, MSDOS_I(dir)->i_start);
    if (ts) {
        de->time = cpu_to_le16(time);
        de->date = cpu_to_le16(date);
        de->ctime = cpu_to_le16(time);
        de->cdate = cpu_to_le16(date);
    }

    ufs_mark_buf_dirty(bh);
    ufs_sync_buf(bh);
    ufs_brelse(bh);

    return cluster;
}

/* -------- Read directory (iterate) -------- */

static int fat_readdir(struct ufs_file *file, struct ufs_dir_context *ctx)
{
    struct ufs_inode *inode = file->f_inode;
    struct ufs_buf *bh = NULL;
    struct msdos_dir_entry *de;
    loff_t pos = 0;
    int err;

    while (1) {
        err = fat_get_entry(inode, &pos, &bh, &de);
        if (err < 0) break;

        if (IS_FREE(de->name))
            continue;
        if (de->attr == ATTR_VOLUME || de->attr == ATTR_EXT)
            continue;

        /* Build entry name */
        struct ufs_dirent dirent;
        unsigned char sname[256];

        fat_parse_short(inode->i_sb, de, sname, NULL);
        memset(&dirent, 0, sizeof(dirent));
        strncpy(dirent.d_name, (const char *)sname, sizeof(dirent.d_name) - 1);
        dirent.d_type = (de->attr & ATTR_DIR) ? UFS_DT_DIR : UFS_DT_REG;

        if (ctx->callback(ctx, &dirent) != 0)
            break;
    }

    ufs_brelse(bh);
    return 0;
}

/* -------- File operations table for directories -------- */

const struct ufs_file_operations fat_dir_operations = {
    .iterate    = fat_readdir,
};
