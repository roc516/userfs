/*
 * ufs_fs.h - VFS core structures and operations
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _UFS_FS_H
#define _UFS_FS_H

#include "ufs_types.h"
#include "ufs_block_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------- Forward declarations -------- */
struct ufs_inode;
struct ufs_super_block;
struct ufs_file;
struct ufs_filesystem_type;

/* -------- Operation tables (function pointer vtables) -------- */

/* Super block operations */
struct ufs_super_operations {
    struct ufs_inode *(*alloc_inode)(struct ufs_super_block *sb);
    void (*free_inode)(struct ufs_inode *inode);
    void (*destroy_inode)(struct ufs_inode *inode);
    int (*write_inode)(struct ufs_inode *inode);
    int (*statfs)(struct ufs_super_block *sb, struct ufs_statfs *buf);
    void (*put_super)(struct ufs_super_block *sb);
    int (*sync_fs)(struct ufs_super_block *sb, int wait);
};

/* Inode operations */
struct ufs_inode_operations {
    int (*lookup)(struct ufs_inode *dir, const char *name,
                  struct ufs_inode **out);
    int (*create)(struct ufs_inode *dir, const char *name, umode_t mode,
                  struct ufs_inode **out);
    int (*unlink)(struct ufs_inode *dir, const char *name);
    int (*mkdir)(struct ufs_inode *dir, const char *name, umode_t mode,
                 struct ufs_inode **out);
    int (*rmdir)(struct ufs_inode *dir, const char *name);
    int (*rename)(struct ufs_inode *old_dir, const char *old_name,
                  struct ufs_inode *new_dir, const char *new_name);
    int (*setattr)(struct ufs_inode *inode, struct ufs_iattr *attr);
    int (*getattr)(struct ufs_inode *inode, struct ufs_stat *stat);
};

/* File operations */
struct ufs_file_operations {
    ssize_t (*read)(struct ufs_file *file, void *buf, size_t count,
                    loff_t offset);
    ssize_t (*write)(struct ufs_file *file, const void *buf, size_t count,
                     loff_t offset);
    int (*open)(struct ufs_inode *inode, struct ufs_file *file);
    int (*release)(struct ufs_inode *inode, struct ufs_file *file);
    int (*fsync)(struct ufs_file *file);
    int (*iterate)(struct ufs_file *file, struct ufs_dir_context *ctx);
};

/* -------- Core VFS structures -------- */

/* Super block */
struct ufs_super_block {
    unsigned long           s_magic;
    unsigned int            s_blocksize;
    unsigned int            s_blocksize_bits;
    loff_t                  s_maxbytes;
    struct ufs_bdev        *s_bdev;
    void                   *s_fs_info;        /* filesystem private data */
    struct ufs_super_operations *s_op;
    struct ufs_filesystem_type  *s_fs_type;
    int                     s_flags;
    char                    s_id[32];
};

/* Inode */
struct ufs_inode {
    unsigned long           i_ino;
    umode_t                 i_mode;
    loff_t                  i_size;
    unsigned int            i_nlink;
    unsigned int            i_blocks;
    unsigned int            i_flags;
    struct ufs_super_block *i_sb;
    struct ufs_inode_operations  *i_op;
    struct ufs_file_operations   *i_fop;
    void                   *i_private;    /* filesystem private data */
    unsigned int            i_generation;
};

/* File handle */
struct ufs_file {
    struct ufs_inode            *f_inode;
    struct ufs_file_operations  *f_op;
    loff_t                       f_pos;
    int                          f_flags;
    void                        *f_private;
};

/* Filesystem type registration */
struct ufs_filesystem_type {
    const char                    *name;
    int (*mount)(struct ufs_bdev *bdev, struct ufs_super_block **sb_out);
    void (*kill_sb)(struct ufs_super_block *sb);
    struct ufs_filesystem_type *next;    /* internal, linked list */
};

/* -------- Filesystem registration API -------- */
int ufs_register_fs(struct ufs_filesystem_type *fs);
int ufs_unregister_fs(struct ufs_filesystem_type *fs);
struct ufs_filesystem_type *ufs_get_fs(const char *name);
struct ufs_filesystem_type *ufs_get_fs_list(void);

#ifdef __cplusplus
}
#endif

#endif /* _UFS_FS_H */
