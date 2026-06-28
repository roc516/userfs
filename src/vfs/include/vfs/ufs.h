/*
 * ufs.h - UserFS public API
 *
 * A userspace file system framework that provides VFS-like abstractions
 * for accessing various filesystem formats from disk images or block devices.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _UFS_H
#define _UFS_H

#include "vfs/ufs_types.h"
#include "vfs/ufs_fs.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- Library lifecycle -------- */

/* Initialize the library (register builtin filesystems) */
int ufs_init(void);

/* Cleanup the library */
void ufs_cleanup(void);

/* -------- Mount / Unmount -------- */

/* Mount a filesystem from a device/image path */
int ufs_mount(const char *device, const char *fstype,
              struct ufs_super_block **sb_out);

/* Unmount a filesystem and release all resources */
int ufs_umount(struct ufs_super_block *sb);

/* -------- File operations -------- */

/* Open a file by path */
int ufs_open(struct ufs_super_block *sb, const char *path,
             struct ufs_file **file_out);
int ufs_create(struct ufs_super_block *sb, const char *path, umode_t mode,
               struct ufs_file **file_out);

/* Read from file at current position */
ssize_t ufs_read(struct ufs_file *file, void *buf, size_t count);

/* Write to file at current position */
ssize_t ufs_write(struct ufs_file *file, const void *buf, size_t count);

/* Close a file */
int ufs_close(struct ufs_file *file);

/* Seek to position */
loff_t ufs_seek(struct ufs_file *file, loff_t offset, int whence);

/* Flush file */
int ufs_fsync(struct ufs_file *file);

/* -------- Directory operations -------- */

/* List directory contents (returns array of names, caller must free) */
int ufs_listdir(struct ufs_super_block *sb, const char *path,
                char ***names, int *count);

/* Create a directory */
int ufs_mkdir(struct ufs_super_block *sb, const char *path, umode_t mode);

/* Remove a directory */
int ufs_rmdir(struct ufs_super_block *sb, const char *path);

/* -------- Filesystem operations -------- */

/* Remove a file */
int ufs_unlink(struct ufs_super_block *sb, const char *path);

/* Rename a file or directory */
int ufs_rename(struct ufs_super_block *sb, const char *oldpath,
               const char *newpath);

/* Get file/directory attributes */
int ufs_stat(struct ufs_super_block *sb, const char *path,
             struct ufs_stat *buf);

/* Get filesystem statistics */
int ufs_statfs(struct ufs_super_block *sb, struct ufs_statfs *buf);

/* -------- Path resolution helper -------- */

/* Resolve a path into its parent directory and final component.
 * Returns: parent inode and leaf name (caller should not free leaf name). */
int ufs_path_parent(struct ufs_super_block *sb, const char *path,
                    struct ufs_inode **parent, const char **name);

/* Resolve a full path to an inode */
int ufs_path_resolve(struct ufs_super_block *sb, const char *path,
                     struct ufs_inode **inode_out);

/* Inode reference counting helpers */
void ufs_iput(struct ufs_inode *inode);
struct ufs_inode *ufs_iget(struct ufs_super_block *sb, unsigned long ino);

/* -------- Logging macros -------- */

#define ufs_log_err(fmt, args...) \
    fprintf(stderr, "UFS ERROR: " fmt "\n", ##args)
#define ufs_log_warn(fmt, args...) \
    fprintf(stderr, "UFS WARN: " fmt "\n", ##args)
#define ufs_log_info(fmt, args...) \
    fprintf(stderr, "UFS INFO: " fmt "\n", ##args)
/* Release: debug messages compiled out. Enable for development:
#define ufs_log_debug(fmt, args...) \
    fprintf(stderr, "UFS DEBUG: " fmt "\n", ##args)
*/
#define ufs_log_debug(fmt, args...) /* no-op */

#ifdef __cplusplus
}
#endif

#endif /* _UFS_H */
