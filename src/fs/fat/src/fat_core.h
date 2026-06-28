/*
 * fat_core.h - FAT filesystem internal header
 *
 * Ported from Linux kernel fs/fat/fat.h
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _UFS_FAT_CORE_H
#define _UFS_FAT_CORE_H

#include <pthread.h>
#include "ondisk.h"
#include "vfs/ufs_fs.h"

/* -------- FAT hash bits -------- */
#define FAT_HASH_BITS   8
#define FAT_HASH_SIZE   (1UL << FAT_HASH_BITS)

/* -------- List helpers (replacing Linux kernel list.h) -------- */
struct list_head {
    struct list_head *next, *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head)
{
    __list_add(new, head, head->next);
}

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
    __list_add(new, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

static inline void list_del_init(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
    __list_del(list->prev, list->next);
    list_add(list, head);
}

static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

#define list_for_each_entry(pos, head, member)                          \
    for (pos = list_entry((head)->next, typeof(*pos), member);          \
         &pos->member != (head);                                        \
         pos = list_entry(pos->member.next, typeof(*pos), member))

/* -------- container_of -------- */
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))
#endif

/* -------- Forward declarations -------- */
struct fat_entry;
struct ufs_timespec;

/* -------- FAT mount options -------- */
struct fat_mount_options {
    unsigned short  fs_uid;
    unsigned short  fs_gid;
    unsigned short  fs_fmask;
    unsigned short  fs_dmask;
    unsigned short  codepage;
    int             time_offset;
    char           *iocharset;
    unsigned short  shortname;
    unsigned char   name_check;
    unsigned char   errors;
    unsigned char   nfs;
    unsigned short  allow_utime;
    unsigned int    quiet:1;
    unsigned int    showexec:1;
    unsigned int    sys_immutable:1;
    unsigned int    dotsOK:1;
    unsigned int    isvfat:1;
    unsigned int    utf8:1;
    unsigned int    unicode_xlate:1;
    unsigned int    numtail:1;
    unsigned int    flush:1;
    unsigned int    nocase:1;
    unsigned int    usefree:1;
    unsigned int    tz_set:1;
    unsigned int    rodir:1;
    unsigned int    discard:1;
    unsigned int    dos1xfloppy:1;
    unsigned int    debug:1;
};

/* -------- FAT entry operations (abstracts FAT12/16/32) -------- */
struct fatent_operations {
    void (*ent_blocknr)(struct ufs_super_block *, int, int *, sector_t *);
    void (*ent_set_ptr)(struct fat_entry *, int);
    int  (*ent_bread)(struct ufs_super_block *, struct fat_entry *,
                      int, sector_t);
    int  (*ent_get)(struct fat_entry *);
    void (*ent_put)(struct fat_entry *, int);
    int  (*ent_next)(struct fat_entry *);
};

/* -------- In-core super block data -------- */
struct msdos_sb_info {
    unsigned short  sec_per_clus;
    unsigned short  cluster_bits;
    unsigned int    cluster_size;
    unsigned char   fats, fat_bits;
    unsigned short  fat_start;
    unsigned long   fat_length;
    unsigned long   dir_start;
    unsigned short  dir_entries;
    unsigned long   data_start;
    unsigned long   max_cluster;
    unsigned long   root_cluster;
    unsigned long   fsinfo_sector;
    pthread_mutex_t fat_lock;
    pthread_mutex_t nfs_build_inode_lock;
    pthread_mutex_t s_lock;
    unsigned int    prev_free;
    unsigned int    free_clusters;
    unsigned int    free_clus_valid;
    struct fat_mount_options options;
    const void     *dir_ops;
    int             dir_per_block;
    int             dir_per_block_bits;
    unsigned int    vol_id;
    int             fatent_shift;
    const struct fatent_operations *fatent_ops;
    unsigned int    dirty;
};

/* -------- FAT entry -------- */
struct fat_entry {
    int entry;
    union {
        u8 *ent12_p[2];
        __le16 *ent16_p;
        __le32 *ent32_p;
    } u;
    int nr_bhs;
    struct ufs_buf *bhs[2];
    struct ufs_inode *fat_inode;
};

/* -------- Cache structures -------- */
struct fat_cache {
    struct list_head cache_list;
    int nr_contig;
    int fcluster;
    int dcluster;
};

struct fat_cache_id {
    unsigned int id;
    int nr_contig;
    int fcluster;
    int dcluster;
};

#define FAT_CACHE_VALID 0

/* -------- In-core inode data -------- */
struct msdos_inode_info {
    pthread_mutex_t cache_lru_lock;
    struct list_head cache_lru;
    int             nr_caches;
    unsigned int    cache_valid_id;
    loff_t          mmu_private;
    int             i_start;
    int             i_logstart;
    int             i_attrs;
    loff_t          i_pos;
    unsigned long   i_parent_dir_ino;   /* inode of parent directory for write_inode */
    struct ufs_inode vfs_inode;
};

/* -------- Slot info for directory operations -------- */
struct fat_slot_info {
    loff_t          i_pos;
    loff_t          slot_off;
    int             nr_slots;
    struct msdos_dir_entry *de;
    struct ufs_buf *bh;
};

/* -------- Helper macros -------- */
static inline struct msdos_sb_info *MSDOS_SB(struct ufs_super_block *sb)
{
    return (struct msdos_sb_info *)sb->s_fs_info;
}

static inline struct msdos_inode_info *MSDOS_I(struct ufs_inode *inode)
{
    return container_of(inode, struct msdos_inode_info, vfs_inode);
}

/* -------- Inline helpers from original fat.h -------- */

static inline int is_fat12(const struct msdos_sb_info *sbi)
{
    return sbi->fat_bits == 12;
}
static inline int is_fat16(const struct msdos_sb_info *sbi)
{
    return sbi->fat_bits == 16;
}
static inline int is_fat32(const struct msdos_sb_info *sbi)
{
    return sbi->fat_bits == 32;
}

static inline u32 max_fat(struct ufs_super_block *sb)
{
    struct msdos_sb_info *sbi = MSDOS_SB(sb);
    return is_fat32(sbi) ? MAX_FAT32 :
           is_fat16(sbi) ? MAX_FAT16 : MAX_FAT12;
}

static inline sector_t fat_clus_to_blknr(struct msdos_sb_info *sbi, int clus)
{
    return ((sector_t)clus - FAT_START_ENT) * sbi->sec_per_clus
            + sbi->data_start;
}

static inline void fat_get_blknr_offset(struct msdos_sb_info *sbi,
                loff_t i_pos, sector_t *blknr, int *offset)
{
    *blknr = i_pos >> sbi->dir_per_block_bits;
    *offset = i_pos & (sbi->dir_per_block - 1);
}

static inline loff_t fat_i_pos_read(struct msdos_sb_info *sbi,
                                     struct ufs_inode *inode)
{
    return MSDOS_I(inode)->i_pos;
}

static inline unsigned char fat_checksum(const unsigned char *name)
{
    unsigned char s = name[0];
    s = (s<<7)+(s>>1)+name[1]; s = (s<<7)+(s>>1)+name[2];
    s = (s<<7)+(s>>1)+name[3]; s = (s<<7)+(s>>1)+name[4];
    s = (s<<7)+(s>>1)+name[5]; s = (s<<7)+(s>>1)+name[6];
    s = (s<<7)+(s>>1)+name[7]; s = (s<<7)+(s>>1)+name[8];
    s = (s<<7)+(s>>1)+name[9]; s = (s<<7)+(s>>1)+name[10];
    return s;
}

static inline int fat_get_start(const struct msdos_sb_info *sbi,
                                const struct msdos_dir_entry *de)
{
    int cluster = le16_to_cpu(de->start);
    if (is_fat32(sbi))
        cluster |= (le16_to_cpu(de->starthi) << 16);
    return cluster;
}

static inline void fat_set_start(struct msdos_dir_entry *de, int cluster)
{
    de->start   = cpu_to_le16((u16)cluster);
    de->starthi = cpu_to_le16((u16)(cluster >> 16));
}

static inline umode_t fat_make_mode(struct msdos_sb_info *sbi,
                                     u8 attrs, umode_t mode)
{
    if (attrs & ATTR_RO && !((attrs & ATTR_DIR) && !sbi->options.rodir))
        mode &= ~S_IWUGO;
    if (attrs & ATTR_DIR)
        return (mode & ~sbi->options.fs_dmask) | S_IFDIR;
    return (mode & ~sbi->options.fs_fmask) | S_IFREG;
}

static inline u8 fat_make_attrs(struct ufs_inode *inode)
{
    struct msdos_inode_info *i = MSDOS_I(inode);
    u8 attrs = i->i_attrs;
    if (S_ISDIR(inode->i_mode))
        attrs |= ATTR_DIR;
    return attrs;
}

static inline int fat_valid_entry(struct msdos_sb_info *sbi, int entry)
{
    return FAT_START_ENT <= entry && entry < (int)sbi->max_cluster;
}

static inline void fatent_init(struct fat_entry *fatent)
{
    fatent->nr_bhs = 0;
    fatent->entry = 0;
    fatent->u.ent32_p = NULL;
    fatent->bhs[0] = fatent->bhs[1] = NULL;
    fatent->fat_inode = NULL;
}

static inline void fatent_set_entry(struct fat_entry *fatent, int entry)
{
    fatent->entry = entry;
    fatent->u.ent32_p = NULL;
}

static inline void fatent_brelse(struct fat_entry *fatent)
{
    int i;
    fatent->u.ent32_p = NULL;
    for (i = 0; i < fatent->nr_bhs; i++)
        ufs_brelse(fatent->bhs[i]);
    fatent->nr_bhs = 0;
    fatent->bhs[0] = fatent->bhs[1] = NULL;
    fatent->fat_inode = NULL;
}

/* -------- Time conversion -------- */
struct ufs_timespec {
    int64_t tv_sec;
    long    tv_nsec;
};

/* -------- Function declarations (from original fat.h) -------- */

/* cache.c */
void fat_cache_inval_inode(struct ufs_inode *inode);
int  fat_get_cluster(struct ufs_inode *inode, int cluster,
                     int *fclus, int *dclus);
int  fat_bmap(struct ufs_inode *inode, sector_t sector, sector_t *phys,
              unsigned long *mapped_blocks, int create);

/* dir.c */
int fat_search_long(struct ufs_inode *inode, const unsigned char *name,
                    int name_len, struct fat_slot_info *sinfo);
int fat_dir_empty(struct ufs_inode *dir);
int fat_subdirs(struct ufs_inode *dir);
int fat_scan(struct ufs_inode *dir, const unsigned char *name,
             struct fat_slot_info *sinfo);
int fat_scan_logstart(struct ufs_inode *dir, int i_logstart,
                      struct fat_slot_info *sinfo);
int fat_get_dotdot_entry(struct ufs_inode *dir, struct ufs_buf **bh,
                         struct msdos_dir_entry **de);
int fat_alloc_new_dir(struct ufs_inode *dir, struct ufs_timespec *ts);
int fat_add_entries(struct ufs_inode *dir, void *slots, int nr_slots,
                    struct fat_slot_info *sinfo);
int fat_remove_entries(struct ufs_inode *dir, struct fat_slot_info *sinfo);

/* fatent.c */
void fat_ent_access_init(struct ufs_super_block *sb);
int  fat_ent_read(struct ufs_inode *inode, struct fat_entry *fatent, int entry);
int  fat_ent_write(struct ufs_inode *inode, struct fat_entry *fatent,
                   int new, int wait);
int  fat_alloc_clusters(struct ufs_inode *inode, int *cluster, int nr_cluster);
int  fat_free_clusters(struct ufs_inode *inode, int cluster);
int  fat_count_free_clusters(struct ufs_super_block *sb);

/* misc.c */
void fat_time_fat2unix(struct msdos_sb_info *sbi, struct ufs_timespec *ts,
                       __le16 __time, __le16 __date, u8 time_cs);
void fat_time_unix2fat(struct msdos_sb_info *sbi, struct ufs_timespec *ts,
                       __le16 *time, __le16 *date, u8 *time_cs);
int  fat_clusters_flush(struct ufs_super_block *sb);
int  fat_chain_add(struct ufs_inode *inode, int new_dclus, int nr_cluster);
int  fat_sync_bhs(struct ufs_buf **bhs, int nr_bhs);
void fat_truncate_time(struct ufs_inode *inode, struct ufs_timespec *now,
                       unsigned int flags);

/* vfs_inode.c */
struct ufs_inode *fat_alloc_inode(struct ufs_super_block *sb);
void fat_free_inode(struct ufs_inode *inode);
void fat_evict_inode(struct ufs_inode *inode);
int fat_write_inode(struct ufs_inode *inode);
int fat_fill_inode(struct ufs_inode *inode, struct msdos_dir_entry *de);
int fat_sync_inode(struct ufs_inode *inode);
void fat_attach(struct ufs_inode *inode, loff_t i_pos);
void fat_detach(struct ufs_inode *inode);
struct ufs_inode *fat_iget(struct ufs_super_block *sb, loff_t i_pos);
struct ufs_inode *fat_build_inode(struct ufs_super_block *sb,
                                   struct msdos_dir_entry *de, loff_t i_pos);
int fat_fill_super(struct ufs_super_block *sb,
                   void (*setup)(struct ufs_super_block *));

/* vfs_file.c */
int fat_setattr(struct ufs_inode *inode, struct ufs_iattr *attr);
int fat_getattr(struct ufs_inode *inode, struct ufs_stat *stat);
void fat_truncate_blocks(struct ufs_inode *inode, loff_t offset);

/* namei.c - VFAT operations */
extern const struct ufs_inode_operations vfat_dir_inode_operations;
extern const struct ufs_file_operations fat_dir_operations;
extern const struct ufs_file_operations fat_file_operations;

/* fat_fs.c */
extern struct ufs_filesystem_type ufs_fat_fs_type;
extern struct ufs_super_operations fat_sops;

/* Inode hash operations (from vfs/inode.c) */
int ufs_inode_insert_hash(struct ufs_inode *inode);
void ufs_inode_remove_hash(struct ufs_inode *inode);

/* helper for printk */
typedef unsigned long long llu;

/* fat_msg equivalent - implemented in misc.c */
void __fat_msg(const char *id, const char *fmt, ...);
#define fat_msg(sb, fmt, args...) \
    __fat_msg((sb)->s_id, fmt, ##args)
#define fat_fs_error(sb, fmt, args...) \
    __fat_msg((sb)->s_id, "ERROR: " fmt, ##args)
#define fat_fs_error_ratelimit(sb, fmt, args...) \
    fat_fs_error(sb, fmt, ##args)

#endif /* _UFS_FAT_CORE_H */
