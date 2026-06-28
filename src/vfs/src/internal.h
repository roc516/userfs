/*
 * internal.h - VFS internal definitions
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _UFS_INTERNAL_H
#define _UFS_INTERNAL_H

#include <pthread.h>
#include "vfs/ufs.h"

#define UFS_MAX_INODE_HASH  (1 << 8)

/*
 * Inode hashtable - for iget/iput tracking
 */
struct ufs_inode_hashtable {
    struct ufs_inode *head;
};

extern pthread_mutex_t ufs_inode_hash_lock;
extern struct ufs_inode_hashtable ufs_inode_hashtable[UFS_MAX_INODE_HASH];

static inline unsigned long ufs_inode_hash(unsigned long ino)
{
    return ino & (UFS_MAX_INODE_HASH - 1);
}

/*
 * Inode refcount management
 */
void ufs_inode_addref(struct ufs_inode *inode);
void ufs_inode_delref(struct ufs_inode *inode);

/*
 * Path resolution helpers
 */
int ufs_path_walk(struct ufs_super_block *sb, struct ufs_inode *dir,
                  const char *pathname, struct ufs_inode **out);
int ufs_path_split(const char *path, char *parent, int parent_size,
                    const char **name);

/*
 * Super block management
 */
int ufs_super_init(struct ufs_super_block *sb, struct ufs_bdev *bdev,
                   struct ufs_filesystem_type *fs_type);
void ufs_super_destroy(struct ufs_super_block *sb);

/*
 * File operations (internal)
 */
int ufs_file_open(struct ufs_inode *inode, struct ufs_file **file_out,
                  int flags);
int ufs_file_close(struct ufs_file *file);
ssize_t ufs_file_read(struct ufs_file *file, void *buf, size_t count);
ssize_t ufs_file_write(struct ufs_file *file, const void *buf, size_t count);
loff_t ufs_file_seek(struct ufs_file *file, loff_t offset, int whence);
int ufs_file_fsync(struct ufs_file *file);

/*
 * Inode flags
 * Inode flags
 */
#define UFS_I_NEW       0x0001
#define UFS_I_DIRTY     0x0002

/*
 * Super block flags
 */
#define UFS_SB_RDONLY   0x0001
#define UFS_SB_DIRTY    0x0002

#endif /* _UFS_INTERNAL_H */
