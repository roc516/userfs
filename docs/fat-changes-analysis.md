# FAT 实现变动分析

> 生成时间：2026-06-27
> 分析对象：内核 `fs/fat/` → 用户空间 `src/fat/`

## 总览

用户空间 FAT 实现是将 Linux 内核 `fs/fat/` 移植到用户空间中的结果。所作更改可分为：

| 类别 | 影响 |
|---|---|
| **内核 API → 用户空间** | `super_block` → `ufs_super_block`，`inode` → `ufs_inode`，`buffer_head` → `ufs_buf`，`file` → `ufs_file`，锁 → `pthread_mutex_t` |
| **移除** | 页面缓存、NLS 字符集、DCache、RCU、NFS 导出、ioctl、fallocate、配额、discard/TRIM、模块基础设施 |
| **简化** | 锁类型、缓存分配 (slab→malloc)、时间处理、VFS 层 |
| **新文件** | `fat_fs.c`（合并 `inode.c` 中的 `fat_fill_super` + 挂载代码） |

### 对应关系

| 内核文件 | 用户空间文件 | 缩减 |
|----------|-------------|:----:|
| `fs/fat/fat.h` | `fat/fat_core.h` | -7% |
| `fs/fat/fatent.c` | `fat/fatent.c` | -47% |
| `fs/fat/dir.c` | `fat/dir.c` | -57% |
| `fs/fat/cache.c` | `fat/cache.c` | -25% |
| `fs/fat/misc.c` | `fat/misc.c` | -36% |
| `fs/fat/inode.c` | `fat/vfs_inode.c` + `fat/fat_fs.c` | -72% |
| `fs/fat/file.c` | `fat/vfs_file.c` | -58% |
| `fs/fat/namei_vfat.c` | `fat/namei.c` | -74% |
| `uapi/linux/msdos_fs.h` | `fat/ondisk.h` | +8% |
| **总计** | **7,549 → 3,396 行** | **-55%** |

---

## 1. 内核 API 替换（全局）

| 内核 | 用户空间 |
|------|----------|
| `struct super_block *` | `struct ufs_super_block *` |
| `struct inode *` | `struct ufs_inode *` |
| `struct buffer_head *` | `struct ufs_buf *` |
| `sb_bread(sb, nr)` | `ufs_bread(sb->s_bdev, nr)` |
| `sb_getblk(sb, nr)` | `ufs_bget(sb->s_bdev, nr)` |
| `brelse(bh)` | `ufs_brelse(bh)` |
| `mark_buffer_dirty_inode(bh, inode)` | `ufs_mark_buf_dirty(bh)` |
| `sync_dirty_buffer(bh)` | `ufs_sync_buf(bh)` |
| `lock_buffer(bh)` / `unlock_buffer(bh)` | 移除（`ufs_bread` 内部处理） |
| `set_buffer_uptodate(bh)` | 移除（始终 uptodate） |
| `spinlock_t` | `pthread_mutex_t` |
| `struct kmem_cache *` | `malloc()` / `free()` |
| `struct timespec64` | `struct ufs_timespec` |

---

## 2. 移除的大型子系统

### 2.1 页面缓存 (page cache)
- **内核**：`address_space_operations`（`fat_aops`）包含 `writepages`、`read_folio`、`readahead`、`write_begin/end`、`direct_IO`、`bmap`
- **用户空间**：完全移除，直接用 `ufs_bread/ufs_bget` 读写扇区

### 2.2 NLS 字符集
- **内核**：`struct nls_table` 动态加载，支持几十种编码（cp437、utf8 等）
- **用户空间**：完全移除。用自带的 `utf8_to_uni16/uni16_to_utf8` 替代

### 2.3 Dentry 缓存
- **内核**：dcache + dentry 操作表 + 负缓存 + 别名处理 (`vfat_revalidate`、`vfat_hash`、`vfat_cmp` 等)
- **用户空间**：完全移除。每次路径解析都从磁盘查找

### 2.4 ioctl
- **内核**：`FAT_IOCTL_GET/SET_ATTRIBUTES`、`FAT_IOCTL_GET_VOLUME_ID`、`FITRIM`
- **用户空间**：完全移除

### 2.5 Direct IO / AIO / fallocate
- **内核**：`read_iter/write_iter` + `direct_IO` 回调 + `fat_fallocate()`
- **用户空间**：完全移除

### 2.6 挂载选项框架
- **内核**：`init_fs_context` + `parse_param` + `reconfigure` + `fat_param_spec[]`
- **用户空间**：完全移除，无挂载选项解析

### 2.7 模块/导出基础设施
- **内核**：`module_init/exit`、`EXPORT_SYMBOL_GPL`、`THIS_MODULE`
- **用户空间**：完全移除

---

## 3. 各文件详细变动

### 3.1 `fat_core.h`（header）

**移除：**
- 所有内核头文件：`<linux/buffer_head.h>`、`<linux/nls.h>`、`<linux/hash.h>`、`<linux/ratelimit.h>` 等
- `kuid_t / kgid_t` → `unsigned short`
- `struct nls_table *nls_disk` 和 `*nls_io`
- `spinlock_t inode_hash_lock`、`hlist_head inode_hashtable[]`、`hlist_head dir_hashtable[]`
- `struct rcu_head rcu`（RCU 延迟释放）
- `struct ratelimit_state ratelimit`
- `struct rw_semaphore truncate_lock`
- `struct timespec64 i_crtime`
- `struct hlist_node i_fat_hash`、`i_dir_hash`
- `wchar_t` 转换工具函数
- 模式辅助函数
- 许多外部声明：`fat_bmap`（不同签名）、`fat_trim_fs`、ioctl、NFS 导出操作等

**添加：**
- 用户空间 list_head 实现（`list_add`、`list_del`、`list_for_each_entry` 等）
- `container_of` 宏
- `struct ufs_timespec`
- `i_parent_dir_ino` 字段（用于 write_inode 回退查找）
- `fat_msg()` / `fat_fs_error()` → `fprintf(stderr, ...)`
- `fatent_brelse()` → `ufs_brelse()`

---

### 3.2 `fatent.c`（FAT 表操作）

**简化：**
- **`fat_alloc_clusters()`**：内核分配器批量分配多簇、边分配边镜像所有 FAT 副本、使用 `MAX_BUF_PER_PAGE` 收集。用户空间**每次只分配 1 个簇**（`nr_cluster != 1` → `-ENOSPC`）
- **`fat_free_clusters()`**：内核支持 discard/TRIM + 批量镜像。用户空间逐簇释放
- **`fat_mirror_bhs()`**：内核始终镜像所有 FAT。用户空间**对 FAT32 直接 return 0**（跳过了镜像）
- **`fat_count_free_clusters()`**：内核带预读取的完整 FAT 扫描。用户空间**只返回缓存值**

**移除：**
- `WARN_ON()`、`cond_resched()`、`fatal_signal_pending()`
- `mark_fsinfo_dirty()`（没有 `fsinfo_inode`）
- `sb_rdonly()` 检查（无只读挂载逻辑）
- `inode_needs_sync()`、`sb->s_flags & SB_SYNCHRONOUS` 检查
- 预读：`struct fatent_ra`、`fat_ra_init()`、`fat_ent_reada()`——完全移除
- `fat_trim_fs()` 和 `fat_trim_clusters()`——完全移除

**已知 Bug：**
- `fat12_ent_get()`：奇数/偶数条目移位掩码错误，返回值未屏蔽为 12 位
- `fat12_ent_next()`：完全重写，偶数条目过早释放 `bhs[1]`
- `fat_ent_bread()` 调用了 `ent_bread` 但没有调用 `ops->ent_set_ptr()`

---

### 3.3 `dir.c`（目录操作）

**简化：**
- **`fat_search_long()`**：内核同时搜索 VFAT 长文件名和短文件名，使用校验和验证。用户空间**跳过 ATTR_EXT 条目，只匹配短文件名（8.3）**
- **`fat_parse_short()`**：内核完整 VFAT 短名称转换（NLS、大小写、Unicode）。用户空间简单连接 base + ext、处理 `0x05→0xE5`
- **`fat_add_entries()`**：内核三阶段（搜索→填充→扩展）带对齐检查。用户空间大大简化
- **`fat_remove_entries()`**：内核反向遍历（正确处理 LFN 槽位逆序）。用户空间逐个槽位遍历

**移除：**
- Unicode 转换：`uni16_to_x8()`、`fat_uni_to_x8()`、`fat_short2uni()`、`fat_name_match()`
- `fat_tolower()` 内联函数
- `fat_make_i_pos()` 辅助函数
- `fat_dir_readahead()` 预读优化
- `FAT_IOCTL_FILLDIR_FUNC`、`fat_ioctl_readdir()`、`fat_dir_ioctl()`——ioctl 接口
- `fat_dir_operations` 完整文件操作表（llseek、read、iterate_shared、ioctl、fsync、setlease）
- `__fat_remove_entries()`、`fat_zeroed_cluster()`、`fat_add_new_entries()`

**添加：**
- `uni16_to_utf8()` / `utf8_to_uni16()`——简化的 UTF-16 ↔ UTF-8 转换
- `strncasecmp()`——不区分大小写名称匹配（`#include <strings.h>`）
- 简化的 `fat_readdir()` 迭代器（`struct ufs_dir_context *ctx` + callback）

**已知 Bug：**
- `fat_parse_long()` 使用不安全转换 `(struct msdos_dir_entry **)&slot`
- `fat_search_long()` 在 `sinfo == NULL` 时可能崩溃（不过调用者都传了有效的 sinfo）

---

### 3.4 `cache.c`（簇链缓存）

**简化：**
- slab 分配器 → `malloc` + `memset`
- `spin_lock()` → `pthread_mutex_lock()`
- `fat_cache_init/destroy` → 移除

**移除：**
- `fat_get_mapped_cluster()`——内核详细映射函数
- `is_exceed_eof()`——EOF 检查辅助函数
- `from_bmap` 参数

**已知 Bug：**
- `fat_bmap()` 硬编码 `-9`（扇区移位），应为 `sb->s_blocksize_bits`
- `fat_bmap()` 移除了 EOF 检查——超范围扇区可能返回错误数据
- `fat_get_cluster()` 用 `>= max_fat()` 判断 EOF（内核用 `== FAT_ENT_EOF`）

---

### 3.5 `misc.c`（杂项）

**简化：**
- `__fat_fs_error()`：内核支持 `FAT_ERRORS_PANIC` → `panic()`。用户空间移除 panic，简化只读标志
- `fat_truncate_time()` → **空壳函数**，不实际更新时间字段
- `time64_to_tm()` → `gmtime_r()`

**移除：**
- `fat_update_time()` → 内核调用 `__mark_inode_dirty()`。移除
- `_fat_msg()` → 内核用 printk 索引。用户空间用宏替代

**已知 Bug：**
- `fat_tz_offset()` 默认返回 0（内核读取 `sys_tz.tz_minuteswest`）
- `fat_truncate_time()` 尾部 `(void)now;` 抑制未使用变量警告

---

### 3.6 `vfs_inode.c` + `fat_fs.c`（inode + super）

**`fat_fs.c` 中的新代码**（无直接内核对应）：
- `fat_read_bpb()`——引导扇区解析
- `fat_set_sb_info()`——从 BPB 填充 `msdos_sb_info`
- `vfat_setup()`——设置 `dir_ops` 和 VFAT 选项
- `fat_mount()` / `fat_kill_sb()`——挂载/卸载回调
- `struct ufs_filesystem_type ufs_fat_fs_type`——文件系统注册结构

**`vfs_inode.c` 中的新代码**：
- `i_parent_dir_ino` 跟踪——通过父目录 inode 号定位目录条目
- 简化的 `fat_write_inode()`——搜索 parent dir `i_pos` 或回退到 `fat_scan_logstart()`

**移除**（从内核 `inode.c`）：
- 整个 `address_space_operations` + 页面缓存
- `fat_get_block()` / `__fat_get_block()`——块映射回调
- `fat_block_truncate_page()`
- `fat_direct_IO()`
- `fat_init_fs_context()` / `fat_free_fc()` / `fat_parse_param()` / `fat_reconfigure()`
- `fat_param_spec[]`、`msdos_param_spec[]`、`vfat_param_spec[]`
- `fat_hash_init()` / `dir_hash_init()`
- `fat_evict_inode()`——内核清除页面、释放 EOF 块、同步元数据。用户空间只使缓存失效
- `fat_set_state()`——脏状态管理（引导扇区标记）
- `fat_init_inodecache()` / `fat_destroy_inodecache()`
- `delayed_free()`——RCU 回调
- `calc_fat_clusters()`、`fat_read_static_bpb()`、`fat_floppy_defaults[]`
- `is_exec()`——可执行文件扩展名检测
- `fat_calc_dir_size()`、`fat_validate_dir()`
- `fat_read_root()`——从 BPB 读取根 inode

**已知 Bug：**
- `fat_write_inode()` 写入 `time(NULL)` 而非 inode 存储时间——每次写回破坏时间戳
- `fat_fill_super()` 中 `sb->s_fs_info` 双重赋值（行 212 和 221）
- 空闲簇扫描昂贵——每个簇单独 `fat_ent_read()`，无缓存/预读

---

### 3.7 `vfs_file.c`（文件读写）

**核心变化**：从 page cache 模型重构为直接的扇区 I/O 循环

```
内核：generic_file_read_iter/write_iter → write_begin/write_end → page cache
用户空间：fat_bmap() → ufs_bread() → memcpy() → ufs_brelse()
```

**移除：**
- ioctl：`fat_ioctl_get_attributes()`、`fat_ioctl_set_attributes()`、`fat_generic_ioctl()`
- fallocate：`fat_fallocate()`、`fat_cont_expand()`
- `fat_file_operations`：内核有 `llseek`、`read_iter`、`write_iter`、`mmap_prepare`、`release`、`unlocked_ioctl`、`fsync`、`splice_read`、`fallocate`、`setlease`
  - **用户空间只有** `read`、`write`、`fsync`、`release`
- `fat_sanitize_mode()`、`fat_allow_set_time()`

**简化：**
- `fat_free()`：内核支持 `skip_start != 0` 保留开头簇。**用户空间不支持非零截断**
- `fat_truncate_blocks()`：内核调用 `fat_free(inode, nr_clusters)`。用户空间**只支持 offset=0 的完整释放**
- `fat_setattr()`：内核权限检查、时间设置、模式验证。用户空间**只处理大小更改**

**添加：**
- `fat_file_read()`——直接读取循环
- `fat_file_write()`——直接写入循环，带分配逻辑
- `fat_add_cluster()`——从内核 `inode.c` 复制过来

**已知 Bug：**
- `fat_file_write()` 分配逻辑可能无限循环（分配后 `continue` 不重新查询 `phys`）
- `fat_free()` 对 `skip_start != 0` 只是 return，不释放任何东西

---

### 3.8 `namei.c`（名称操作）

**根本性变化——完整 VFAT 丢失：**
- 内核：`vfat_create_shortname()`（120 行序数字尾算法）+ `vfat_build_slots()`（构建 LFN 槽位）
- **用户空间：`vfat_build_sfn()`（40 行的 toupper + 截断，不创建 LFN）**

结果是：
- **写入新文件时名字会被截断为 8.3 格式**（`ThisIsAVeryLongFileName.txt` → `THISISAV.TXT`）
- **读取时如果文件有 VFAT LFN 条目，`fat_search_long` 跳过 LFN 槽位，只匹配短文件名**

**移除：**
- dentry 操作：`vfat_revalidate()`、`vfat_hash()`、`vfat_cmp()`——整个 DCache 层
- Unicode 转换：`xlate_to_uni()`
- `vfat_build_slots()`——构造长文件名槽位 + 短名称条目
- `vfat_add_entry()`——长条目 + 短条目的完整创建
- `vfat_rename_exchange()`——`RENAME_EXCHANGE` 支持
- `vfat_get_dotdot_de()`、`vfat_sync_ipos()`、`vfat_update_dotdot_de()`、`vfat_update_dir_metadata()`
- `vfat_init_fs_context()`、`vfat_fill_super()`、`vfat_parse_param()`
- `inode_inc_iversion()`、`inc_nlink()`、`drop_nlink()`、`clear_nlink()`、`set_nlink()`

**简化：**
- 重命名：用户空间先移除目标（若有），然后用新名称复制旧条目。内核完整原子操作

---

### 3.9 `ondisk.h`（磁盘结构）

**移除：**
- 所有 ioctl 定义：`VFAT_IOCTL_READDIR_BOTH`、`VFAT_IOCTL_READDIR_SHORT`、`FAT_IOCTL_GET/SET_ATTRIBUTES`、`FAT_IOCTL_GET_VOLUME_ID`
- `CF_LE_W` / `CF_LE_L` / `CT_LE_W` / `CT_LE_L` 宏

**添加：**
- `typedef u16 __le16`、`typedef u32 __le32`
- 字节序辅助函数内联实现：`le16_to_cpu()`、`cpu_to_le16()`、`le32_to_cpu()`、`cpu_to_le32()`——使用 `memcpy` 安全对齐访问
- `FAT_ERRORS_CONT` / `FAT_ERRORS_PANIC` / `FAT_ERRORS_RO` 宏
- `FAT_NFS_STALE_RW` / `FAT_NFS_NOSTALE_RO` 宏

---

## 4. Bug 修复 vs 引入的 Bug

### 4.1 移植过程中修复的 Bug

| # | 位置 | 问题 | 修复 |
|---|------|------|------|
| 1 | `fatent.c` | `fatent_set_entry` 在 `ops->ent_put` 前清空指针 | 移除冗余的 `fatent_set_entry` 调用 |
| 2 | `dir.c` | `fat_add_entries` 用逻辑块号当物理块号 | 添加 `fat_bmap` 转换 |
| 3 | `fat_fs.c` | FAT32 根 inode `i_size` 未初始化 | 添加 `sbi->cluster_size` 赋值 |
| 4 | `vfs_inode.c` | 目录项定位使用 `fat_scan_logstart`（根目录专用） | 重构为使用 `i_pos + i_parent_dir_ino` |
| 5 | `vfs_inode.c` | `fat_build_inode` 覆盖合成 `i_ino` | 移除 `inode->i_ino = (unsigned long)i_pos` |

### 4.2 移植引入的 Bug

| # | 位置 | 描述 | 严重度 |
|---|------|------|:------:|
| 1 | `fatent.c` | `fat12_ent_get()` 奇数/偶数条目移位掩码错误，返回值未屏蔽为 12 位 | 🔴 |
| 2 | `fatent.c` | `fat12_ent_next()` 完全重写，偶数条目过早释放 `bhs[1]` | 🔴 |
| 3 | `fatent.c` | `fat_mirror_bhs()` 对 FAT32 跳过——不镜像 FAT 副本 | 🟡 |
| 4 | `cache.c` | `fat_bmap()` 硬编码 `-9`（假设块大小 512） | 🟡 |
| 5 | `dir.c` | `fat_parse_long()` 不安全转换 `(struct msdos_dir_entry **)&slot` | 🟡 |
| 6 | `vfs_file.c` | `fat_file_write()` 分配循环可能无限循环 | 🔴 |
| 7 | `vfs_file.c` | `fat_truncate_blocks()` 只支持 offset=0 | 🟡 |
| 8 | `vfs_inode.c` | `fat_write_inode()` 写入 `time(NULL)` 而非 inode 时间 | 🟡 |
| 9 | `namei.c` | 从不创建 VFAT 长文件名槽位 | 🔴 |

---

## 5. 后续开发路线

### 第一阶段：功能完整性

1. **实现 VFAT 长文件名写入**（`namei.c`）
   - 移植 `vfat_build_slots()` 构建 LFN 槽位
   - 修改 `fat_add_entries()` 支持多槽位写入
   - 修改 `fat_search_long()` 解析并匹配 LFN

2. **修复 FAT12 FAT16 已知 Bug**（`fatent.c`）
   - 修复 `fat12_ent_get/ent_next` 移位掩码
   - 恢复 `fat_mirror_bhs` 对 FAT32 的镜像支持

3. **修复文件写入无限循环**（`vfs_file.c`）
   - 分配簇后重新调用 `fat_bmap()` 获取 `phys`

4. **修复 `fat_truncate_blocks()`** 支持非零偏移

### 第二阶段：性能优化

5. **簇分配器优化**（`fatent.c`）
   - 支持批量多簇分配
   - 添加预读取支持

6. **空闲簇计数**（`fatent.c`）
   - 移植内核的预读取 FAT 扫描算法

### 第三阶段：兼容性增强

7. **挂载选项解析**（`fat_fs.c`）
   - 支持只读挂载、uid/gid 映射、codepage 选择

8. **时间戳正确性**（`vfs_inode.c` / `misc.c`）
   - 修复 `fat_write_inode` 使用 inode 存储时间而非 `time(NULL)`
   - 修复 `fat_truncate_time` 实际更新时间
   - 修复 `fat_tz_offset` 时区处理

9. **块大小可变性**（`cache.c`）
   - 移除 `-9` 硬编码，使用 `sb->s_blocksize_bits`
