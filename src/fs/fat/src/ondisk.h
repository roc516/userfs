/*
 * ondisk.h - FAT12/16/32 on-disk structures
 *
 * Ported from Linux kernel include/uapi/linux/msdos_fs.h
 *
 * SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 */

#ifndef _UFS_FAT_ONDISK_H
#define _UFS_FAT_ONDISK_H

#include "vfs/ufs_types.h"
#include <string.h>

/* -------- Little-endian types -------- */
typedef u16 __le16;
typedef u32 __le32;

/* -------- Media / sector constants -------- */
#ifndef SECTOR_SIZE
#define SECTOR_SIZE     512
#endif
#define SECTOR_BITS     9

#define MSDOS_DPB       (SECTOR_SIZE / sizeof(struct msdos_dir_entry))
#define MSDOS_DPB_BITS  4
#define MSDOS_DPS       (SECTOR_SIZE / sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS  4
#define MSDOS_LONGNAME  256
#define CF_LE_W(v)      le16_to_cpu(v)
#define CF_LE_L(v)      le32_to_cpu(v)
#define CT_LE_W(v)      cpu_to_le16(v)
#define CT_LE_L(v)      cpu_to_le32(v)

#define MSDOS_ROOT_INO     1
#define MSDOS_FSINFO_INO   2

#define MSDOS_DIR_BITS  5   /* log2(sizeof(struct msdos_dir_entry)) */

#define FAT_MAX_DIR_ENTRIES     (65536)
#define FAT_MAX_DIR_SIZE        (FAT_MAX_DIR_ENTRIES << MSDOS_DIR_BITS)

/* -------- Attribute bits -------- */
#define ATTR_NONE       0
#define ATTR_RO         1
#define ATTR_HIDDEN     2
#define ATTR_SYS        4
#define ATTR_VOLUME     8
#define ATTR_DIR        16
#define ATTR_ARCH       32

#define ATTR_UNUSED     (ATTR_VOLUME | ATTR_ARCH | ATTR_SYS | ATTR_HIDDEN)
#define ATTR_EXT        (ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)

#define CASE_LOWER_BASE 8
#define CASE_LOWER_EXT  16

#define DELETED_FLAG    0xe5
#define IS_FREE(n)      (!*(n) || *(n) == DELETED_FLAG)

#define FAT_LFN_LEN     255
#define MSDOS_NAME      11
#define MSDOS_SLOTS     21
#define MSDOS_DOT       ".          "
#define MSDOS_DOTDOT    "..         "

#define FAT_START_ENT   2

#define MAX_FAT12       0xFF4
#define MAX_FAT16       0xFFF4
#define MAX_FAT32       0x0FFFFFF6

#define BAD_FAT12       0xFF7
#define BAD_FAT16       0xFFF7
#define BAD_FAT32       0x0FFFFFF7

#define EOF_FAT12       0xFFF
#define EOF_FAT16       0xFFFF
#define EOF_FAT32       0x0FFFFFFF

#define FAT_ENT_FREE    0
#define FAT_ENT_BAD     BAD_FAT32
#define FAT_ENT_EOF     EOF_FAT32

#define FAT_FSINFO_SIG1 0x41615252
#define FAT_FSINFO_SIG2 0x61417272
#define IS_FSINFO(x)    (le32_to_cpu((x)->signature1) == FAT_FSINFO_SIG1 \
                         && le32_to_cpu((x)->signature2) == FAT_FSINFO_SIG2)

#define FAT_STATE_DIRTY 0x01

#define FAT_ERRORS_CONT     1
#define FAT_ERRORS_PANIC    2
#define FAT_ERRORS_RO       3

#define FAT_NFS_STALE_RW    1
#define FAT_NFS_NOSTALE_RO  2

/* -------- On-disk structures -------- */

/* Boot sector */
struct fat_boot_sector {
    u8      ignored[3];
    u8      system_id[8];
    u8      sector_size[2];
    u8      sec_per_clus;
    __le16  reserved;
    u8      fats;
    u8      dir_entries[2];
    u8      sectors[2];
    u8      media;
    __le16  fat_length;
    __le16  secs_track;
    __le16  heads;
    __le32  hidden;
    __le32  total_sect;

    union {
        struct {
            u8      drive_number;
            u8      state;
            u8      signature;
            u8      vol_id[4];
            u8      vol_label[MSDOS_NAME];
            u8      fs_type[8];
        } fat16;

        struct {
            __le32  length;
            __le16  flags;
            u8      version[2];
            __le32  root_cluster;
            __le16  info_sector;
            __le16  backup_boot;
            __le16  reserved2[6];
            u8      drive_number;
            u8      state;
            u8      signature;
            u8      vol_id[4];
            u8      vol_label[MSDOS_NAME];
            u8      fs_type[8];
        } fat32;
    };
};

/* FSINFO block (FAT32) */
struct fat_boot_fsinfo {
    __le32  signature1;
    __le32  reserved1[120];
    __le32  signature2;
    __le32  free_clusters;
    __le32  next_cluster;
    __le32  reserved2[4];
};

/* Directory entry (32 bytes) */
struct msdos_dir_entry {
    u8      name[MSDOS_NAME];
    u8      attr;
    u8      lcase;
    u8      ctime_cs;
    __le16  ctime;
    __le16  cdate;
    __le16  adate;
    __le16  starthi;
    __le16  time, date, start;
    __le32  size;
};

/* Long filename slot (32 bytes) */
struct msdos_dir_slot {
    u8      id;
    u8      name0_4[10];
    u8      attr;
    u8      reserved;
    u8      alias_checksum;
    u8      name5_10[12];
    __le16  start;
    u8      name11_12[4];
};

/* -------- Byte order helpers -------- */
/* We assume little-endian host (x86), use memcpy for alignment safety */

static inline u16 le16_to_cpu(__le16 v)
{
    u16 val;
    memcpy(&val, &v, sizeof(val));
    return val;
}

static inline __le16 cpu_to_le16(u16 v)
{
    __le16 val;
    memcpy(&val, &v, sizeof(val));
    return val;
}

static inline u32 le32_to_cpu(__le32 v)
{
    u32 val;
    memcpy(&val, &v, sizeof(val));
    return val;
}

static inline __le32 cpu_to_le32(u32 v)
{
    __le32 val;
    memcpy(&val, &v, sizeof(val));
    return val;
}

#endif /* _UFS_FAT_ONDISK_H */
