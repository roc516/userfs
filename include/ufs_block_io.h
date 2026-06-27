/*
 * ufs_block_io.h - Block I/O abstraction layer
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _UFS_BLOCK_IO_H
#define _UFS_BLOCK_IO_H

#include "ufs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct ufs_buf;

/* Block device handle - wraps a file descriptor or device */
struct ufs_bdev {
    int         fd;              /* file descriptor */
    unsigned int sector_size;    /* sector size in bytes (usually 512) */
    loff_t      total_sectors;   /* total number of sectors */
    int         readonly;        /* non-zero if opened read-only */
    char        *path;           /* device path (for error messages) */
};

/* Buffer head replacement - represents a cached block */
struct ufs_buf {
    sector_t        b_blocknr;   /* block number on device */
    unsigned int    b_size;      /* size in bytes */
    char           *b_data;      /* pointer to data */
    int             b_dirty;     /* non-zero if modified */
    int             b_uptodate;  /* non-zero if data is valid */
    int             b_count;     /* reference count */
    struct ufs_buf *b_next;      /* hash chain next */
    struct ufs_buf *b_prev;      /* hash chain prev */
    struct ufs_buf *b_lru_next;  /* LRU list next */
    struct ufs_buf *b_lru_prev;  /* LRU list prev */
    struct ufs_bdev *b_bdev;     /* owning block device */
};

/*
 * Block device operations
 */

/* Open a block device or disk image file */
struct ufs_bdev *ufs_bdev_open(const char *path, int readonly);

/* Close a block device */
void ufs_bdev_close(struct ufs_bdev *bdev);

/* Get block device size (in sectors) */
loff_t ufs_bdev_nr_sectors(struct ufs_bdev *bdev);

/* Read sectors from device (raw I/O, bypassing cache) */
int ufs_bdev_read(struct ufs_bdev *bdev, void *buf,
                  sector_t sector, unsigned int nsectors);

/* Write sectors to device (raw I/O, bypassing cache) */
int ufs_bdev_write(struct ufs_bdev *bdev, const void *buf,
                   sector_t sector, unsigned int nsectors);

/*
 * Buffer cache operations (replacement for sb_bread/sb_getblk/brelse)
 */

/* Initialize buffer cache for a device (call after bdev open) */
int ufs_bcache_init(struct ufs_bdev *bdev);

/* Flush all dirty buffers for a device */
int ufs_bcache_flush(struct ufs_bdev *bdev);

/* Destroy buffer cache for a device */
void ufs_bcache_destroy(struct ufs_bdev *bdev);

/* Read a block: returns a referenced buffer (like sb_bread) */
struct ufs_buf *ufs_bread(struct ufs_bdev *bdev, sector_t blocknr);

/* Get a block without reading: returns a referenced buffer (like sb_getblk) */
struct ufs_buf *ufs_bget(struct ufs_bdev *bdev, sector_t blocknr);

/* Read multiple blocks for readahead */
int ufs_breadahead(struct ufs_bdev *bdev, sector_t blocknr, int nr);

/* Release a buffer reference (like brelse) */
void ufs_brelse(struct ufs_buf *bh);

/* Mark buffer as dirty */
void ufs_mark_buf_dirty(struct ufs_buf *bh);

/* Sync a dirty buffer to disk (like sync_dirty_buffer) */
int ufs_sync_buf(struct ufs_buf *bh);

/* Sync multiple buffers */
int ufs_sync_bufs(struct ufs_buf **bhs, int nr_bhs);

/* Check if buffer is uptodate */
static inline int ufs_buf_uptodate(struct ufs_buf *bh)
{
    return bh->b_uptodate;
}

/* Wait for buffer I/O (no-op in sync implementation) */
static inline void ufs_wait_buf(struct ufs_buf *bh)
{
    (void)bh;
}

#ifdef __cplusplus
}
#endif

#endif /* _UFS_BLOCK_IO_H */
