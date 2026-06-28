/*
 * block_io.c - Block I/O abstraction with buffer cache
 *
 * Implements pread/pwrite-based block device access with a simple
 * LRU buffer cache.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "vfs/ufs.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Buffer cache configuration */
#define UFS_BCACHE_NBUCKETS  64
#define UFS_BCACHE_MAX_BUF   4096   /* max buffers in cache */

struct ufs_bcache {
    struct ufs_bdev *bdev;
    struct ufs_buf  *hash[UFS_BCACHE_NBUCKETS];
    struct ufs_buf  *lru_head;      /* LRU list head (most recently used) */
    struct ufs_buf  *lru_tail;      /* LRU list tail (least recently used) */
    int              nr_bufs;
    pthread_mutex_t  lock;
};

/* Hash function for block number */
static inline unsigned int bcache_hash(struct ufs_bcache *bc, sector_t blocknr)
{
    return (unsigned int)(blocknr % UFS_BCACHE_NBUCKETS);
}

/* Get bcache from bdev (stored as private data) */
/* bcache_for() used instead (see below) */

/* Detach buffer from hash chain */
static void buf_detach_hash(struct ufs_bcache *bc, struct ufs_buf *bh)
{
    unsigned int h = bcache_hash(bc, bh->b_blocknr);
    if (bh->b_prev)
        bh->b_prev->b_next = bh->b_next;
    else
        bc->hash[h] = bh->b_next;
    if (bh->b_next)
        bh->b_next->b_prev = bh->b_prev;
    bh->b_next = bh->b_prev = NULL;
}

/* Attach buffer to hash chain */
static void buf_attach_hash(struct ufs_bcache *bc, struct ufs_buf *bh)
{
    unsigned int h = bcache_hash(bc, bh->b_blocknr);
    bh->b_next = bc->hash[h];
    bh->b_prev = NULL;
    if (bc->hash[h])
        bc->hash[h]->b_prev = bh;
    bc->hash[h] = bh;
}

/* Move buffer to MRU end of LRU list */
static void buf_touch_lru(struct ufs_bcache *bc, struct ufs_buf *bh)
{
    /* Remove from current position */
    if (bh->b_lru_prev)
        bh->b_lru_prev->b_lru_next = bh->b_lru_next;
    else
        bc->lru_head = bh->b_lru_next;
    if (bh->b_lru_next)
        bh->b_lru_next->b_lru_prev = bh->b_lru_prev;
    else
        bc->lru_tail = bh->b_lru_prev;

    /* Insert at head (MRU) */
    bh->b_lru_next = bc->lru_head;
    bh->b_lru_prev = NULL;
    if (bc->lru_head)
        bc->lru_head->b_lru_prev = bh;
    bc->lru_head = bh;
    if (!bc->lru_tail)
        bc->lru_tail = bh;
}

/* Evict the least recently used buffer */
static struct ufs_buf *evict_one(struct ufs_bcache *bc)
{
    struct ufs_buf *bh = bc->lru_tail;
    while (bh) {
        struct ufs_buf *prev = bh->b_lru_prev;
        if (bh->b_count == 0) {
            /* Flush if dirty */
            if (bh->b_dirty)
                ufs_sync_buf(bh);
            /* Detach from hash and LRU */
            buf_detach_hash(bc, bh);
            if (bh->b_lru_prev)
                bh->b_lru_prev->b_lru_next = bh->b_lru_next;
            else
                bc->lru_head = bh->b_lru_next;
            if (bh->b_lru_next)
                bh->b_lru_next->b_lru_prev = bh->b_lru_prev;
            else
                bc->lru_tail = bh->b_lru_prev;
            free(bh->b_data);
            free(bh);
            bc->nr_bufs--;
            return NULL;
        }
        bh = prev;
    }
    return bc->lru_tail;  /* all buffers are referenced */
}

/* Allocate a new buffer */
static struct ufs_buf *alloc_buf(struct ufs_bcache *bc,
                                  sector_t blocknr, unsigned int bsize)
{
    struct ufs_buf *bh;

    if (bc->nr_bufs >= UFS_BCACHE_MAX_BUF) {
        if (evict_one(bc))
            return NULL;  /* couldn't evict any */
    }

    bh = (struct ufs_buf *)calloc(1, sizeof(struct ufs_buf));
    if (!bh) return NULL;

    bh->b_data = (char *)malloc(bsize);
    if (!bh->b_data) {
        free(bh);
        return NULL;
    }

    bh->b_blocknr = blocknr;
    bh->b_size = bsize;
    bh->b_bdev = bc->bdev;
    bh->b_count = 1;
    bh->b_uptodate = 0;
    bh->b_dirty = 0;

    buf_attach_hash(bc, bh);

    /* Insert at LRU head */
    bh->b_lru_next = bc->lru_head;
    bh->b_lru_prev = NULL;
    if (bc->lru_head)
        bc->lru_head->b_lru_prev = bh;
    bc->lru_head = bh;
    if (!bc->lru_tail)
        bc->lru_tail = bh;

    bc->nr_bufs++;
    return bh;
}

/* Read data into buffer from device */
static int read_into_buf(struct ufs_buf *bh)
{
    struct ufs_bdev *bdev = bh->b_bdev;
    ssize_t ret;
    loff_t offset;

    if (bh->b_uptodate)
        return 0;

    offset = (loff_t)bh->b_blocknr * bdev->sector_size;
    ret = pread(bdev->fd, bh->b_data, bh->b_size, offset);
    if (ret < 0) {
        ufs_log_err("read error at sector %llu: %s",
                    (unsigned long long)bh->b_blocknr, strerror(errno));
        return -UFS_EIO;
    }
    if ((unsigned int)ret != bh->b_size) {
        ufs_log_err("short read at sector %llu: %zd/%u",
                    (unsigned long long)bh->b_blocknr, ret, bh->b_size);
        return -UFS_EIO;
    }
    bh->b_uptodate = 1;
    return 0;
}

/* -------- Public API -------- */

struct ufs_bdev *ufs_bdev_open(const char *path, int readonly)
{
    struct ufs_bdev *bdev;
    struct ufs_bcache *bc;
    int fd;
    loff_t size;

    fd = open(path, readonly ? O_RDONLY : O_RDWR);
    if (fd < 0) {
        ufs_log_err("cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }

    size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        close(fd);
        ufs_log_err("cannot seek '%s': %s", path, strerror(errno));
        return NULL;
    }

    /* Allocate bdev + bcache together */
    bc = (struct ufs_bcache *)calloc(1, sizeof(struct ufs_bcache));
    if (!bc) {
        close(fd);
        return NULL;
    }

    bdev = &((struct ufs_bdev *)bc)[0];  /* bc IS the bdev */
    /* Actually, let's allocate separately for clarity */
    free(bc);
    bc = NULL;

    bdev = (struct ufs_bdev *)calloc(1, sizeof(struct ufs_bdev));
    if (!bdev) { close(fd); return NULL; }

    bc = (struct ufs_bcache *)calloc(1, sizeof(struct ufs_bcache));
    if (!bc) { free(bdev); close(fd); return NULL; }

    bdev->fd = fd;
    bdev->sector_size = 512;  /* default, can be overridden */
    bdev->total_sectors = size / 512;
    bdev->readonly = readonly;
    bdev->path = strdup(path);
    if (!bdev->path) {
        free(bc); free(bdev); close(fd);
        return NULL;
    }

    bc->bdev = bdev;
    bc->nr_bufs = 0;
    bc->lru_head = bc->lru_tail = NULL;
    memset(bc->hash, 0, sizeof(bc->hash));
    pthread_mutex_init(&bc->lock, NULL);

    /* Store bcache pointer in the bdev itself (acts as bdev private data) */
    /* We'll just use a trick: the bdev pointer is stored in the bcache,
     * and we use get_bcache which casts bdev to bcache. 
     * Actually, let's keep them separate and store bcache as a hidden thing. */

    return bdev;
}

void ufs_bdev_close(struct ufs_bdev *bdev)
{
    if (!bdev) return;
    ufs_bcache_destroy(bdev);
    if (bdev->fd >= 0) close(bdev->fd);
    free(bdev->path);
    free(bdev);
}

loff_t ufs_bdev_nr_sectors(struct ufs_bdev *bdev)
{
    return bdev->total_sectors;
}

int ufs_bdev_read(struct ufs_bdev *bdev, void *buf,
                   sector_t sector, unsigned int nsectors)
{
    ssize_t ret;
    loff_t offset = (loff_t)sector * bdev->sector_size;
    size_t count = (size_t)nsectors * bdev->sector_size;

    ret = pread(bdev->fd, buf, count, offset);
    if (ret < 0) return -UFS_EIO;
    if ((size_t)ret != count) return -UFS_EIO;
    return 0;
}

int ufs_bdev_write(struct ufs_bdev *bdev, const void *buf,
                    sector_t sector, unsigned int nsectors)
{
    ssize_t ret;
    loff_t offset = (loff_t)sector * bdev->sector_size;
    size_t count = (size_t)nsectors * bdev->sector_size;

    if (bdev->readonly) return -UFS_EROFS;
    ret = pwrite(bdev->fd, buf, count, offset);
    if (ret < 0) return -UFS_EIO;
    if ((size_t)ret != count) return -UFS_EIO;
    return 0;
}

/* -------- Buffer cache -------- */

/* We need to store bcache pointer somewhere persistent.
 * Let's use a simple approach: global bcache list keyed by bdev pointer.
 * But for simplicity, we'll use a static hash table.
 */
#define UFS_MAX_BDEVS 16
static struct {
    struct ufs_bdev *bdev;
    struct ufs_bcache *bc;
} ufs_bcache_map[UFS_MAX_BDEVS];
static int ufs_bcache_nr = 0;
static pthread_mutex_t ufs_bcache_map_lock = PTHREAD_MUTEX_INITIALIZER;

static struct ufs_bcache *bcache_for(struct ufs_bdev *bdev)
{
    int i;
    pthread_mutex_lock(&ufs_bcache_map_lock);
    for (i = 0; i < ufs_bcache_nr; i++) {
        if (ufs_bcache_map[i].bdev == bdev) {
            pthread_mutex_unlock(&ufs_bcache_map_lock);
            return ufs_bcache_map[i].bc;
        }
    }
    pthread_mutex_unlock(&ufs_bcache_map_lock);
    return NULL;
}

int ufs_bcache_init(struct ufs_bdev *bdev)
{
    struct ufs_bcache *bc;
    int i;

    if (bcache_for(bdev))
        return 0;  /* already initialized */

    bc = (struct ufs_bcache *)calloc(1, sizeof(struct ufs_bcache));
    if (!bc) return -UFS_ENOMEM;

    bc->bdev = bdev;
    bc->nr_bufs = 0;
    bc->lru_head = bc->lru_tail = NULL;
    memset(bc->hash, 0, sizeof(bc->hash));
    pthread_mutex_init(&bc->lock, NULL);

    pthread_mutex_lock(&ufs_bcache_map_lock);
    if (ufs_bcache_nr >= UFS_MAX_BDEVS) {
        pthread_mutex_unlock(&ufs_bcache_map_lock);
        free(bc);
        return -UFS_ENOMEM;
    }
    i = ufs_bcache_nr++;
    ufs_bcache_map[i].bdev = bdev;
    ufs_bcache_map[i].bc = bc;
    pthread_mutex_unlock(&ufs_bcache_map_lock);

    return 0;
}

int ufs_bcache_flush(struct ufs_bdev *bdev)
{
    struct ufs_bcache *bc = bcache_for(bdev);
    struct ufs_buf *bh;
    int err = 0;

    if (!bc) return 0;

    pthread_mutex_lock(&bc->lock);
    bh = bc->lru_head;
    while (bh) {
        if (bh->b_dirty) {
            int ret = ufs_sync_buf(bh);
            if (ret && !err) err = ret;
        }
        bh = bh->b_lru_next;
    }
    pthread_mutex_unlock(&bc->lock);
    return err;
}

void ufs_bcache_destroy(struct ufs_bdev *bdev)
{
    struct ufs_bcache *bc = bcache_for(bdev);
    struct ufs_buf *bh, *next;
    int i, idx = -1;

    if (!bc) return;

    pthread_mutex_lock(&bc->lock);

    /* Free all buffers */
    bh = bc->lru_head;
    while (bh) {
        next = bh->b_lru_next;
        if (bh->b_dirty) {
            /* Try to flush, but don't fail on error */
            ufs_bdev_write(bdev, bh->b_data,
                           bh->b_blocknr, bh->b_size / bdev->sector_size);
        }
        free(bh->b_data);
        free(bh);
        bh = next;
    }

    pthread_mutex_unlock(&bc->lock);
    pthread_mutex_destroy(&bc->lock);

    /* Remove from map */
    pthread_mutex_lock(&ufs_bcache_map_lock);
    for (i = 0; i < ufs_bcache_nr; i++) {
        if (ufs_bcache_map[i].bdev == bdev) {
            idx = i;
            break;
        }
    }
    if (idx >= 0) {
        ufs_bcache_map[idx] = ufs_bcache_map[--ufs_bcache_nr];
    }
    pthread_mutex_unlock(&ufs_bcache_map_lock);

    free(bc);
}

struct ufs_buf *ufs_bread(struct ufs_bdev *bdev, sector_t blocknr)
{
    struct ufs_bcache *bc = bcache_for(bdev);
    struct ufs_buf *bh;
    unsigned int h;

    if (!bc) return NULL;

    pthread_mutex_lock(&bc->lock);
    h = bcache_hash(bc, blocknr);
    bh = bc->hash[h];
    while (bh) {
        if (bh->b_blocknr == blocknr) {
            bh->b_count++;
            buf_touch_lru(bc, bh);
            pthread_mutex_unlock(&bc->lock);
            if (!bh->b_uptodate) {
                if (read_into_buf(bh) < 0) {
                    ufs_brelse(bh);
                    return NULL;
                }
            }
            return bh;
        }
        bh = bh->b_next;
    }

    /* Not in cache, allocate and read */
    bh = alloc_buf(bc, blocknr, bdev->sector_size);
    if (!bh) {
        pthread_mutex_unlock(&bc->lock);
        return NULL;
    }
    pthread_mutex_unlock(&bc->lock);

    if (read_into_buf(bh) < 0) {
        ufs_brelse(bh);
        return NULL;
    }
    return bh;
}

struct ufs_buf *ufs_bget(struct ufs_bdev *bdev, sector_t blocknr)
{
    struct ufs_bcache *bc = bcache_for(bdev);
    struct ufs_buf *bh;
    unsigned int h;

    if (!bc) return NULL;

    pthread_mutex_lock(&bc->lock);
    h = bcache_hash(bc, blocknr);
    bh = bc->hash[h];
    while (bh) {
        if (bh->b_blocknr == blocknr) {
            bh->b_count++;
            buf_touch_lru(bc, bh);
            pthread_mutex_unlock(&bc->lock);
            return bh;
        }
        bh = bh->b_next;
    }

    bh = alloc_buf(bc, blocknr, bdev->sector_size);
    pthread_mutex_unlock(&bc->lock);
    return bh;
}

int ufs_breadahead(struct ufs_bdev *bdev, sector_t blocknr, int nr)
{
    /* Simple readahead: just read the first block */
    struct ufs_buf *bh = ufs_bread(bdev, blocknr);
    if (!bh) return -UFS_EIO;
    ufs_brelse(bh);
    return 0;
}

void ufs_brelse(struct ufs_buf *bh)
{
    if (!bh) return;
    struct ufs_bcache *bc = bcache_for(bh->b_bdev);
    if (bc) {
        pthread_mutex_lock(&bc->lock);
        if (bh->b_count > 0)
            bh->b_count--;
        pthread_mutex_unlock(&bc->lock);
    }
}

void ufs_mark_buf_dirty(struct ufs_buf *bh)
{
    if (bh) bh->b_dirty = 1;
}

int ufs_sync_buf(struct ufs_buf *bh)
{
    struct ufs_bdev *bdev = bh->b_bdev;
    int ret;

    if (!bh->b_dirty || !bh->b_uptodate)
        return 0;

    ret = ufs_bdev_write(bdev, bh->b_data,
                          bh->b_blocknr, bh->b_size / bdev->sector_size);
    if (ret == 0)
        bh->b_dirty = 0;
    return ret;
}

int ufs_sync_bufs(struct ufs_buf **bhs, int nr_bhs)
{
    int i, err = 0;
    for (i = 0; i < nr_bhs; i++) {
        int ret = ufs_sync_buf(bhs[i]);
        if (ret && !err) err = ret;
    }
    return err;
}
