/*
 * ufs_types.h - UserFS base type definitions
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _UFS_TYPES_H
#define _UFS_TYPES_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- Integer types -------- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* -------- File types -------- */
typedef int64_t loff_t;
typedef uint64_t sector_t;

/* -------- Mode bits (subset of POSIX) -------- */
typedef unsigned short umode_t;

#define S_IFMT   00170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_IRWXU  00700
#define S_IRUSR  00400
#define S_IWUSR  00200
#define S_IXUSR  00100
#define S_IRWXG  00070
#define S_IRGRP  00040
#define S_IWGRP  00020
#define S_IXGRP  00010
#define S_IRWXO  00007
#define S_IROTH  00004
#define S_IWOTH  00002
#define S_IXOTH  00001

#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)

/* S_IWUGO is S_IWUSR | S_IWGRP | S_IWOTH */
#define S_IWUGO     (S_IWUSR | S_IWGRP | S_IWOTH)
#define S_IRWXUGO   (S_IRWXU | S_IRWXG | S_IRWXO)
#define S_IRUGO     (S_IRUSR | S_IRGRP | S_IROTH)
#define S_IXUGO     (S_IXUSR | S_IXGRP | S_IXOTH)

/* -------- Directory entry type -------- */
#define UFS_DT_UNKNOWN  0
#define UFS_DT_DIR      1
#define UFS_DT_REG      2

/* -------- Error codes (subset of POSIX) -------- */
#define UFS_EPERM       1
#define UFS_ENOENT      2
#define UFS_EIO         5
#define UFS_ENXIO       6
#define UFS_E2BIG       7
#define UFS_ENOEXEC     8
#define UFS_EBADF       9
#define UFS_ENOMEM      12
#define UFS_EACCES      13
#define UFS_EFAULT      14
#define UFS_EEXIST      17
#define UFS_EXDEV       18
#define UFS_ENOTDIR     20
#define UFS_EISDIR      21
#define UFS_EINVAL      22
#define UFS_ENOSPC      28
#define UFS_EROFS       30
#define UFS_EMLINK      31
#define UFS_ENAMETOOLONG 36
#define UFS_ENOTEMPTY   39
#define UFS_EDQUOT      122
#define UFS_ENODATA     61
#define UFS_ENOSYS      78
#define UFS_EBUSY       16

/* -------- Open flags -------- */
#define UFS_O_RDONLY     0
#define UFS_O_WRONLY     1
#define UFS_O_RDWR       2

/* -------- Public structures -------- */

/* Stat structure */
struct ufs_stat {
    unsigned long   st_ino;
    umode_t         st_mode;
    unsigned int    st_nlink;
    loff_t          st_size;
    unsigned int    st_blksize;
    unsigned long   st_blocks;
};

/* Statfs structure */
struct ufs_statfs {
    unsigned long   f_blocks;
    unsigned long   f_bfree;
    unsigned long   f_bavail;
    unsigned long   f_files;
    unsigned long   f_ffree;
    unsigned long   f_bsize;
    unsigned long   f_namelen;
};

/* iattr for setattr */
struct ufs_iattr {
    unsigned int    ia_valid;
    loff_t          ia_size;
    umode_t         ia_mode;
    unsigned int    ia_uid;
    unsigned int    ia_gid;
};

#define UFS_ATTR_SIZE   0x01
#define UFS_ATTR_MODE   0x02
#define UFS_ATTR_UID    0x04
#define UFS_ATTR_GID    0x08

/* Dir entry for iterate */
struct ufs_dirent {
    unsigned long   d_ino;
    unsigned char   d_type;
    char            d_name[256];
};

/* Dir context for iterate callback */
struct ufs_dir_context {
    void *priv;
    int (*callback)(struct ufs_dir_context *ctx, const struct ufs_dirent *de);
};

#ifdef __cplusplus
}
#endif

#endif /* _UFS_TYPES_H */
