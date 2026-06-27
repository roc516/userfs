# VFS 层完整度分析

> 生成时间：2026-06-27
> 分析对象：`/data/src/org/userfs/src/vfs/` + `include/`

## 概述

UserFS VFS 层是一个**用户空间** VFS 框架，提供类似 Linux VFS 的抽象来从磁盘镜像或块设备访问文件系统。目前只内置了 FAT12/16/32 驱动。

整个 VFS 实现了最精简的"骨架"——所有操作都是同步的，无阻塞 I/O，无内存映射，无页缓存，无命名空间。

---

## 1. Inode 管理 (`inode.c` + `ufs_fs.h` + `internal.h`)

### 已实现

- `struct ufs_inode`：inode 号、模式、大小、链接数、块数、标志、超级块指针、i_op / i_fop 虚函数表、私有数据指针
- `ufs_iget()`：通过 inode 号在哈希表中查找
- `ufs_inode_addref()` / `ufs_inode_delref()`：操作 `i_nlink`（但语义混淆——同用于内存引用和磁盘链接计数）
- `ufs_inode_insert_hash()` / `ufs_inode_remove_hash()`：256 桶固定大小哈希表，`pthread_mutex_t` 保护

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **真正的引用计数** | `i_count` 跟踪内存引用，`iput()` 递减直至释放 | `i_nlink` 被复用为引用计数和磁盘链接计数，语义混淆。`iput()` 声明但**未实现** |
| **inode 回写** | `write_inode` + `I_DIRTY` + 定期刷写线程 | `UFS_I_DIRTY` 标志定义了但从未被设置或检查 |
| **inode 生命周期** | alloc_inode → fill → evict → destroy 三阶段 | VFS 层**从不调用** `alloc_inode/destroy_inode` |
| **哈希表 next 指针** | 专用 `i_hash` 链表节点 | **滥用** `i_private` 作为哈希 next 指针，与文件系统私有数据冲突 |
| **按需填充** | `iget()` 缓存未命中时分配新 inode 并调用 `read_inode` | `ufs_iget()` **只做哈希查找**，未找到返回 NULL |
| **inode 回收** | `prune_icache()` / `shrink_icache_memory()` | **完全不回收 inode** |
| **时间戳** | `i_atime`, `i_mtime`, `i_ctime` | `struct ufs_inode` 中**没有时间戳字段** |

---

## 2. Dentry 缓存 (`dentry.c` + `ufs_fs.h`)

### 已实现

- `ufs_dentry_match()`：简单的 `strcmp` 包装
- `ufs_path_split()`：将路径拆分为父目录路径和最终组件名

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **Dentry 数据结构** | `struct dentry`（d_name, d_parent, d_inode, d_subdirs, d_child, d_lru, d_hash, d_count 等） | **完全缺失**。没有 `struct ufs_dentry` 或类似结构体 |
| **Dentry 缓存 (dcache)** | 巨大的 dentry 哈希树，加速路径解析，处理 `..`，管理挂载点 | **没有 dentry 缓存**。路径解析每次都从根 inode 开始逐组件调用 `lookup()` |
| **负缓存** | 缓存"不存在"的 dentry 以避免重复磁盘访问 | 不存在。每次路径查找都访问磁盘 |
| **别名处理** | 同一个 inode 可有多个 dentry（硬链接），维护 `i_dentry` 列表 | 不存在 |
| **`..` 处理** | `d_parent` 指针，`..` 解析 O(1) 无需磁盘访问 | `path.c:71-73` 注释明确 `/* 没有 parent 指针，处理起来很棘手 */` |


---

## 3. 超级块管理 (`super.c` + `ufs_fs.h`)

### 已实现

- `struct ufs_super_block`：魔数、块大小、最大字节数、块设备、私有数据、操作表、文件系统类型、标志、ID 字符串
- `ufs_super_init()`：零初始化并设置基本字段
- `ufs_super_destroy()`：调用 `put_super` → 释放私有数据 → 关闭块设备
- `struct ufs_super_operations`：`alloc_inode`、`free_inode`、`write_inode`、`statfs`、`put_super`

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **已挂载文件系统列表** | 全局 `super_blocks` 列表 + `s_list` 链表 | 不存在。每个 `ufs_mount()` 独立分配，无法枚举已挂载 |
| **`kill_sb` 回调** | `kill_sb` 在卸载时由 VFS 调用 | 定义了但在 `ufs_umount()` 或 `ufs_super_destroy()` 中**从未被调用** |
| **挂载选项** | 从内核选项字符串解析传递给 `fill_super` | 没有挂载选项解析。`ufs_mount()` 只接受 `device` + `fstype` |
| **读写状态检查** | `sb_rdonly()` 宏，每次写入前检查 | `UFS_SB_RDONLY` 定义了但任何公共 API 都不检查它 |
| **文件描述符限值** | `s_maxbytes` 限制 | `s_maxbytes` 被设置但**从未被 `read/write/seek` 检查** |

---

## 4. 文件操作 (`file.c` + `ufs_fs.h` + `ufs.h`)

### 已实现

- `struct ufs_file`：inode 指针、操作表、当前位置、标志、私有数据
- `struct ufs_file_operations`：`read/write/open/release/fsync/iterate`
- `ufs_file_open()` / `ufs_file_close()` / `ufs_file_read()` / `ufs_file_write()` / `ufs_file_seek()` / `ufs_file_fsync()`
- 公共 API 作为简单的一对一封装

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **`llseek` 钩子** | 独立的 `llseek` 指针 | seek 硬编码在 `ufs_file_seek()` 中，**无法被文件系统覆盖** |
| **`read_iter` / `write_iter`** | `iov_iter` 用于页向量和 AIO | 不存在。只有基本的 `(buf, count, offset)` |
| **`mmap`** | `mmap` → `nopage`/`fault` 按需调页 | 不存在 |
| **`splice_read` / `splice_write`** | 管道和文件之间零拷贝传输 | 不存在 |
| **`flush`** | `close()` 触发但可能被 dup 延迟 | 不存在 |
| **`fallocate` / `ftruncate`** | 预分配和截断 | 不存在 |
| **`copy_file_range`** | 服务器端复制 | 不存在 |
| **`fadvise`** | I/O 提示传递给页面缓存 | 不存在 |
| **并发读/写锁** | `i_rwsem` 信号量 | 无锁，假设单线程 |
| **`f_pos` 溢出保护** | 范围检查 | 只检查 `< 0`——不检查 `> s_maxbytes` |
| **`O_DIRECT` / `O_SYNC` 等** | 标志影响 VFS 行为 | `f_flags` 存在但 VFS 代码中**从未检查** |

---

## 5. 路径解析 (`path.c`)

### 已实现

- `ufs_path_walk()`：逐组件遍历，处理前导斜杠、中间/尾部斜杠、`.` 组件，受 `ENAMETOOLONG`/`ENOTDIR` 保护，支持创建意图
- `ufs_path_resolve()`：获取根 inode 后调用 `ufs_path_walk`
- `ufs_path_parent()`：解析父目录和最终组件

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **符号链接追踪** | 递归解析 `symlink`（最多 40 次），调用 `follow_link` | **完全缺失** |
| **挂载点穿越** | 自动穿越到已安装文件系统的根目录 | 不存在（但用户空间可接受） |
| **`AT_FDCWD` / cwd** | 相对当前工作目录解析 | cwd 概念**不存在** |
| **`LOOKUP_PARENT/CREATE/OPEN` 等** | 标志向文件系统传达上下文 | 临时处理，无正式标志系统 |
| **RCU 模式查找** | `rcu-walk` 允许无锁路径解析 | 全部持锁 |
| **权限检查** | 每个组件检查执行/读权限 | **完全缺失** |
| **根目录查找引用泄露** | 无泄露 | `path.c:109-111` 注释 "leak for now - FIXME" |

---

## 6. 块 I/O (`block_io.c` + `ufs_block_io.h`)

### 已实现

- `struct ufs_bdev`：fd、扇区大小、总扇区数、只读标志、设备路径
- `struct ufs_buf`：块号、大小、数据指针、脏标志、最新标志、引用计数、链表指针
- 块设备打开/关闭/读取/写入（`pread`/`pwrite`）
- 缓冲区缓存：哈希表 + LRU 替换（最多 4096 个缓冲区，64 桶）
- `ufs_bread/sb_bread`、`ufs_bget/sb_getblk`、`ufs_brelse`、`ufs_mark_buf_dirty` + `ufs_sync_buf`、`ufs_bcache_flush`、`ufs_breadahead`

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **bdev 到 bcache 映射** | `bd_inode` → `bd_mapping`，缓冲区直接嵌入 | 脆弱的**全局数组** `ufs_bcache_map[16]`，硬限制 16 个设备 |
| **页面缓存集成** | 缓冲区缓存基于页面，通过 `address_space_operations` | 独立基于块的 malloc 分配。没有 `readpage/writepage` |
| **页面大小块处理** | 块 < 页面大小时用 `buffer_head` 跟踪多个块 | 假设扇区大小 == 块大小（512 字节——硬编码） |
| **块大小可变性** | `sb_min_blocksize` → `set_blocksize` 动态协商 | `s_blocksize_bits` 硬编码为 9（512 字节） |
| **I/O 调度 / 合并** | blk-mq 多队列块层，请求排序和合并 | 直接的同步 `pread`/`pwrite` |
| **分页 I/O** | `bio`、`bio_add_page`、`submit_bio` | 不存在 |
| **`endio` 回调** | 异步完成时调用 `b_end_io` | 全部同步 |
| **writeback 节流** | 脏页基于 `dirty_ratio` 后台比率刷出 | 只显式调用 `ufs_bcache_flush` 时刷出 |
| **`ufs_bdev_open` 死代码** | - | `block_io.c:207-217` 分配后立即释放临时 `bcache` |

---

## 7. API 层 (`api.c` + `ufs.h`)

### 已实现

- `ufs_init/ufs_cleanup`：注册/注销内置文件系统
- `ufs_mount/ufs_umount`
- 文件 API：`ufs_open/create/read/write/close/seek/fsync`
- 目录 API：`ufs_listdir/mkdir/rmdir`
- FS API：`ufs_unlink/rename/stat/statfs`

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **硬链接** | `link(2)` → `vfs_link()` → `i_op->link()` | 未暴露。没有 `ufs_link()` API |
| **符号链接** | `symlink(2)` → `vfs_symlink()` → `i_op->symlink/readlink/follow_link` | 未暴露 |
| **chmod / chown** | `setattr` with `ATTR_MODE/UID/GID` | `ufs_iattr` 支持但**无公共 API 暴露** |
| **截断** | `truncate(2)` → `setattr` with `ATTR_SIZE` | 同上。`ufs_open` 不支持 `O_TRUNC` |
| **`ufs_iput`** | `iput()` 递减引用并触发 `evict_inode` | 在 `ufs.h:101` 声明但**任何地方都没有实现**。所有路径解析泄露 inode |
| **`ufs_create` 的标志** | `open(2)` 接受 `O_CREAT` + 模式 | 只接受 mode——没有 `O_EXCL`、`O_TRUNC` |
| **文件锁定** | `flock(2)` / `fcntl(2)` 锁 | 不存在 |
| **inotify / dnotify** | 文件系统事件通知 | 不存在 |

---

## 8. 文件系统注册 (`register.c`)

### 已实现

- `struct ufs_filesystem_type`：名称、mount、kill_sb、next 链表指针
- `ufs_register_fs()` / `ufs_unregister_fs()` / `ufs_get_fs()`
- `pthread_mutex_t` 线程安全

### 未实现 / 缺陷

| 缺失特性 | Linux VFS 的做法 | UserFS 的状态 |
|---|---|---|
| **模块引用计数** | `THIS_MODULE` 防止挂载后卸载 | 无关但缺乏保护 |
| **`fs_flags`** | `FS_REQUIRES_DEV`、`FS_BINARY_MOUNTDATA` 等 | 无标志字段 |
| **`ufs_get_fs_list()` 竞争** | RCU 或无锁遍历 | 锁外返回内部链表头——调用者可能迭代时释放 |

---

## 9. 整体缺失

### 权限和安全

- **没有权限检查：** `inode_permission()`、ACL、`may_create/delete/open`
- 没有命名空间隔离

### 并发性

- 粗粒度全局锁（整个哈希表一个锁、整个缓存一个锁、整个注册表一个锁）
- **没有 `inode->i_rwsem`**——并发访问无保护
- 没有 RCU / seqlock / `atomic_t`

### 内存管理

- **没有 slab 分配器：** 所有结构体用裸 `calloc/free`
- 没有 shrinker 回收缓存
- 没有页面缓存

### 操作表缺失的钩子

- `struct ufs_inode_operations` 没有：`symlink`、`readlink`、`link`、`getxattr`、`setxattr`、`listxattr`、`permission`、`get_acl`、`tmpfile`
- `struct ufs_file_operations` 没有：`unlocked_ioctl`、`compat_ioctl`——根本没有 ioctl

---

## 10. 修复优先级

### 🔴 最关键的

1. **实现 `ufs_iput()`**：解决 inode 泄露，让 `write_inode` 能正确回写后释放
2. **引入 dentry 缓存**：大幅提升路径解析性能，支持 `..` 的 O(1) 解析
3. **修复 `i_private` 冲突**：为哈希链表使用专用字段而非复用私有指针
4. **添加权限检查骨架**：至少检查 `may_open`/`may_create` 等基本操作

### 🟡 影响大的

5. **添加挂载选项解析**：支持只读挂载、uid/gid 覆盖等
6. **集成 `kill_sb` 回调**：确保文件系统类型正确清理
7. **检查 `s_maxbytes`**：所有 read/write/seek 路径验证限值
8. **inode 时间戳**：在 `struct ufs_inode` 中添加 `i_atime/mtime/ctime`

### 🔵 锦上添花

9. **并发控制**：替换全局锁为 inode 级别 `i_rwsem`
10. **page cache 集成**：替换独立缓冲区缓存为页面缓存
