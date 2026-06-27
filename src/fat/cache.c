/*
 * cache.c - FAT cluster chain cache
 *
 * Ported from Linux kernel fs/fat/cache.c
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fat_core.h"

#define FAT_MAX_CACHE  8

static struct fat_cache *fat_cache_alloc(void)
{
    struct fat_cache *cache = (struct fat_cache *)malloc(sizeof(*cache));
    if (cache) {
        memset(cache, 0, sizeof(*cache));
        INIT_LIST_HEAD(&cache->cache_list);
    }
    return cache;
}

static void fat_cache_free(struct fat_cache *cache)
{
    if (cache)
        free(cache);
}

static inline void fat_cache_update_lru(struct ufs_inode *inode,
                                         struct fat_cache *cache)
{
    if (MSDOS_I(inode)->cache_lru.next != &cache->cache_list)
        list_move(&cache->cache_list, &MSDOS_I(inode)->cache_lru);
}

static int fat_cache_lookup(struct ufs_inode *inode, int fclus,
                             struct fat_cache_id *cid,
                             int *cached_fclus, int *cached_dclus)
{
    static struct fat_cache nohit = { .fcluster = 0, };
    struct fat_cache *hit = &nohit, *p;
    int offset = -1;

    pthread_mutex_lock(&MSDOS_I(inode)->cache_lru_lock);
    list_for_each_entry(p, &MSDOS_I(inode)->cache_lru, cache_list) {
        if (p->fcluster <= fclus && hit->fcluster < p->fcluster) {
            hit = p;
            if ((hit->fcluster + hit->nr_contig) < fclus) {
                offset = hit->nr_contig;
            } else {
                offset = fclus - hit->fcluster;
                break;
            }
        }
    }
    if (hit != &nohit) {
        fat_cache_update_lru(inode, hit);
        cid->id = MSDOS_I(inode)->cache_valid_id;
        cid->nr_contig = hit->nr_contig;
        cid->fcluster = hit->fcluster;
        cid->dcluster = hit->dcluster;
        *cached_fclus = cid->fcluster + offset;
        *cached_dclus = cid->dcluster + offset;
    }
    pthread_mutex_unlock(&MSDOS_I(inode)->cache_lru_lock);
    return offset;
}

static struct fat_cache *fat_cache_merge(struct ufs_inode *inode,
                                          struct fat_cache_id *new)
{
    struct fat_cache *p;

    list_for_each_entry(p, &MSDOS_I(inode)->cache_lru, cache_list) {
        if (p->fcluster == new->fcluster) {
            if (new->nr_contig > p->nr_contig)
                p->nr_contig = new->nr_contig;
            return p;
        }
    }
    return NULL;
}

static void fat_cache_add(struct ufs_inode *inode, struct fat_cache_id *new)
{
    struct fat_cache *cache;

    if (new->fcluster == -1)
        return;

    pthread_mutex_lock(&MSDOS_I(inode)->cache_lru_lock);
    if (new->id != FAT_CACHE_VALID &&
        new->id != MSDOS_I(inode)->cache_valid_id)
        goto out;

    cache = fat_cache_merge(inode, new);
    if (cache == NULL) {
        struct list_head *p;

        if (MSDOS_I(inode)->nr_caches < FAT_MAX_CACHE) {
            MSDOS_I(inode)->nr_caches++;
            pthread_mutex_unlock(&MSDOS_I(inode)->cache_lru_lock);

            struct fat_cache *tmp = fat_cache_alloc();
            if (!tmp) {
                pthread_mutex_lock(&MSDOS_I(inode)->cache_lru_lock);
                MSDOS_I(inode)->nr_caches--;
                pthread_mutex_unlock(&MSDOS_I(inode)->cache_lru_lock);
                return;
            }

            pthread_mutex_lock(&MSDOS_I(inode)->cache_lru_lock);
            cache = fat_cache_merge(inode, new);
            if (cache != NULL) {
                MSDOS_I(inode)->nr_caches--;
                fat_cache_free(tmp);
                goto out_update_lru;
            }
            cache = tmp;
        } else {
            p = MSDOS_I(inode)->cache_lru.prev;
            cache = list_entry(p, struct fat_cache, cache_list);
        }
        cache->fcluster = new->fcluster;
        cache->dcluster = new->dcluster;
        cache->nr_contig = new->nr_contig;
    }
out_update_lru:
    fat_cache_update_lru(inode, cache);
out:
    pthread_mutex_unlock(&MSDOS_I(inode)->cache_lru_lock);
}

static void __fat_cache_inval_inode(struct ufs_inode *inode)
{
    struct msdos_inode_info *i = MSDOS_I(inode);
    struct fat_cache *cache;

    while (!list_empty(&i->cache_lru)) {
        cache = list_entry(i->cache_lru.next, struct fat_cache, cache_list);
        list_del_init(&cache->cache_list);
        i->nr_caches--;
        fat_cache_free(cache);
    }
    i->cache_valid_id++;
    if (i->cache_valid_id == FAT_CACHE_VALID)
        i->cache_valid_id++;
}

void fat_cache_inval_inode(struct ufs_inode *inode)
{
    pthread_mutex_lock(&MSDOS_I(inode)->cache_lru_lock);
    __fat_cache_inval_inode(inode);
    pthread_mutex_unlock(&MSDOS_I(inode)->cache_lru_lock);
}

static inline int cache_contiguous(struct fat_cache_id *cid, int dclus)
{
    cid->nr_contig++;
    return ((cid->dcluster + cid->nr_contig) == dclus);
}

static inline void cache_init(struct fat_cache_id *cid, int fclus, int dclus)
{
    cid->id = FAT_CACHE_VALID;
    cid->fcluster = fclus;
    cid->dcluster = dclus;
    cid->nr_contig = 0;
}

int fat_get_cluster(struct ufs_inode *inode, int cluster,
                    int *fclus, int *dclus)
{
    struct ufs_super_block *sb = inode->i_sb;
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    const int limit = (int)(sb->s_maxbytes >> sbi->cluster_bits);
    struct fat_entry fatent;
    struct fat_cache_id cid;
    int nr;

    if (MSDOS_I(inode)->i_start == 0) {
        *fclus = 0;
        *dclus = 0;
        return -UFS_EIO;
    }

    *fclus = 0;
    *dclus = MSDOS_I(inode)->i_start;
    if (!fat_valid_entry(sbi, *dclus)) {
        fat_fs_error_ratelimit(sb,
            "%s: invalid start cluster (i_pos %lld, start %08x)",
            __func__, MSDOS_I(inode)->i_pos, *dclus);
        return -UFS_EIO;
    }
    if (cluster == 0)
        return 0;

    if (fat_cache_lookup(inode, cluster, &cid, fclus, dclus) < 0)
        cache_init(&cid, -1, -1);

    fatent_init(&fatent);
    while (*fclus < cluster) {
        if (*fclus > limit) {
            fat_fs_error_ratelimit(sb,
                "%s: detected cluster chain loop (i_pos %lld)",
                __func__, MSDOS_I(inode)->i_pos);
            nr = -UFS_EIO;
            goto out;
        }

        nr = fat_ent_read(inode, &fatent, *dclus);
        if (nr < 0)
            goto out;
        else if (nr == FAT_ENT_FREE) {
            fat_fs_error_ratelimit(sb,
                "%s: invalid cluster chain (i_pos %lld)",
                __func__, MSDOS_I(inode)->i_pos);
            nr = -UFS_EIO;
            goto out;
        } else if (nr >= (int)max_fat(inode->i_sb)) {
            fat_cache_add(inode, &cid);
            goto out;
        }
        (*fclus)++;
        *dclus = nr;
        if (!cache_contiguous(&cid, *dclus))
            cache_init(&cid, *fclus, *dclus);
    }
    nr = 0;
    fat_cache_add(inode, &cid);
out:
    fatent_brelse(&fatent);
    return nr;
}

static int fat_bmap_cluster(struct ufs_inode *inode, int cluster)
{
    struct ufs_super_block *sb = inode->i_sb;
    int ret, fclus, dclus;

    if (MSDOS_I(inode)->i_start == 0)
        return 0;

    ret = fat_get_cluster(inode, cluster, &fclus, &dclus);
    if (ret < 0)
        return ret;
    else if (ret >= (int)max_fat(sb)) {
        /* Normal end-of-chain — return 0 so caller knows there's no data */
        return 0;
    }
    return dclus;
}

int fat_bmap(struct ufs_inode *inode, sector_t sector, sector_t *phys,
             unsigned long *mapped_blocks, int create)
{
    struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
    int cluster, offset;

    *phys = 0;
    *mapped_blocks = 0;

    if (!is_fat32(sbi) && (inode->i_ino == MSDOS_ROOT_INO)) {
        if (sector < (sbi->dir_entries >> sbi->dir_per_block_bits)) {
            *phys = sector + sbi->dir_start;
            *mapped_blocks = 1;
        }
        return 0;
    }

    if (MSDOS_I(inode)->i_start == 0)
        return 0;

    cluster = (int)(sector >> (sbi->cluster_bits - 9));  /* blocknr->cluster */
    offset  = (int)(sector & (sbi->sec_per_clus - 1));

    cluster = fat_bmap_cluster(inode, cluster);
    if (cluster < 0)
        return cluster;
    else if (cluster) {
        *phys = fat_clus_to_blknr(sbi, cluster) + offset;
        *mapped_blocks = sbi->sec_per_clus - (unsigned long)offset;
    }

    return 0;
}
