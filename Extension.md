# VFS 项目拓展指南

本文档基于当前项目架构，梳理可行的拓展方向，按优先级和难度分级。

---

## 一、前提：完成桩代码（Layer 3/4/5）

以下文件目前只有 TODO 骨架，必须先完成才能开展后续拓展：

| 文件 | 功能 |
|------|------|
| `kernel/igetput.cpp` | iget() 内存 inode 获取、iput() 引用计数释放与回写 |
| `kernel/name.cpp` | namei() 按名查找目录项、iname() 寻找空目录项 |
| `kernel/dir.cpp` | _dir() 列目录、mkdir() 创建子目录、chdir() 切换目录 |
| `kernel/access.cpp` | 权限检查（owner/group/other × r/w/x） |
| `kernel/creat.cpp` | 文件创建 |
| `kernel/open.cpp` | 文件打开 |
| `kernel/close.cpp` | 文件关闭 |
| `kernel/delete.cpp` | 文件删除 |
| `kernel/rdwt.cpp` | 文件读写 |

同时修复 DESIGN.md §10 列出的 10 个已知 Bug。

---

## 二、核心文件系统增强

### 2.1 多级间接索引

**难度**: 中 | **涉及文件**: `filesys.h`, `balloc.cpp`, `bfree.cpp`, `rdwt.cpp`

当前 `di_addr[10]` 只支持 10 个直接块，最大文件 5KB。增加间接索引：

- **一级间接块**: 一个块存 256 个块号指针（512B / 2B = 256），最大文件 128KB
- **二级间接块**: 一层指向 256 个一级间接块，最大文件 32MB
- `di_addr[0..6]` 为直接块，`di_addr[7]` 为一级间接，`di_addr[8]` 为二级间接，`di_addr[9]` 保留

修改 `vfs_read()`/`vfs_write()` 中的块号解析逻辑，以及 `iput()` 中 `di_number==0` 时回收所有块的逻辑。

### 2.2 文件时间戳

**难度**: 低 | **涉及文件**: `filesys.h`, `creat.cpp`, `rdwt.cpp`, `igetput.cpp`

在 `dinode` 中增加三个字段：

```cpp
unsigned long di_atime;  // 最后访问时间
unsigned long di_mtime;  // 最后修改时间
unsigned long di_ctime;  // 创建时间
```

`dinode` 从 32B 变为 38B（可按 512B/sector 对齐微调）。创建文件时设置三个时间，读取时更新 `di_atime`，写入时更新 `di_mtime`。在 `_dir()` 中展示时间戳。

### 2.3 硬链接

**难度**: 低 | **涉及文件**: `kernel/link.cpp`（新增）, `kernel/delete.cpp`, `filesys.h`

`dinode.di_number` 已预留链接计数。实现 `link(target, linkname)` 命令：

1. `namei()` 找到目标文件 inode 号
2. `iget()` 获取 inode，`di_number++`
3. `iname()` 在新目录项中写入链接名，指向同一 inode
4. `iput()` 回写

删除时只减少 `di_number`，减到 0 才真正回收。

### 2.4 符号链接（软链接）

**难度**: 中 | **涉及文件**: `filesys.h`, `kernel/symlink.cpp`（新增）, `kernel/namei.cpp`

增加 `DISYMLINK` 文件类型。创建符号链接时分配 inode + 数据块，将目标路径字符串存入数据块。`namei()` 解析路径时遇到符号链接则读取目标路径继续解析。设置最大递归深度（如 8 层）防止循环。

### 2.5 rename 重命名

**难度**: 低 | **涉及文件**: `kernel/rename.cpp`（新增）, `user/shell.cpp`

实现 `rename(oldname, newname)`：
- 同目录内：直接修改 `d_name`
- 跨目录：在新目录创建目录项 → 更新 `..` 父目录指向 → 删除旧目录项

### 2.6 文件截断

**难度**: 低 | **涉及文件**: `kernel/rdwt.cpp`, `user/shell.cpp`

实现 `ftruncate(fd, newsize)`——释放多余数据块，更新 `di_size`。

### 2.7 文件系统一致性检查（fsck）

**难度**: 高 | **涉及文件**: `kernel/fsck.cpp`（新增）

检查项目：
- 扫描 inode 区，统计每块被引用次数，与空闲块栈比对
- 检测"孤儿块"（既不在文件也不在空闲栈中）
- 检测"双重分配块"（同一块被多个 inode 引用）
- 修复 `.` 和 `..` 目录项指向是否正确
- 检测 inode 链接计数与实际引用次数是否一致

---

## 三、性能与可靠性

### 3.1 LRU Inode 缓存

**难度**: 中 | **涉及文件**: `igetput.cpp`, `filesys.h`

当前 `iget()` 方案是每次 miss 就 `new inode`，无上限。改进：

- 预设固定大小的 inode 池（如 64 个）
- 维护 LRU 链表：每次访问将 inode 移到链表头部
- 池满时回收链表尾部的 inode（i_count==0 的直接写回并复用）
- 避免无限内存增长，且与 Hash 查找兼容

### 3.2 块缓冲区缓存

**难度**: 中 | **涉及文件**: `kernel/buffer.cpp`（新增）, `balloc.cpp`, `rdwt.cpp`

在磁盘 I/O 与上层之间加一层缓冲：

- 维护 N 个缓冲区（每个 512B），按块号 Hash
- 延迟写：修改后的缓冲区标记为 dirty，由 `halt()` 或定期刷盘
- 预读：顺序读取时提前加载下一块

### 3.3 超级块冗余备份

**难度**: 低 | **涉及文件**: `format.cpp`, `install.cpp`, `halt.cpp`

在磁盘末尾（如倒数第 1 块）写入超级块的备份副本。`install()` 加载时若主超级块损坏则自动恢复。每次更新超级块时同时更新两份。

### 3.4 写前日志（WAL）

**难度**: 高 | **涉及文件**: `kernel/journal.cpp`（新增）

在磁盘预留日志区域（如最后 50 块）：
1. 修改元数据前，先将"意图"写入日志
2. 日志记录完成后，执行实际修改
3. `install()` 时检查日志，重放未完成的操作
4. 日志满时做 checkpoint

---

## 四、Shell 与用户体验

### 4.1 cp 文件拷贝

**难度**: 低 | **涉及文件**: `user/shell.cpp`

```
cp <src> <dst>
```

流程：open 源文件 → creat 目标文件 → 循环 read/write → close 两者。

### 4.2 cat 查看文件

**难度**: 低 | **涉及文件**: `user/shell.cpp`

```
cat <filename>
```

流程：open → read 到缓冲区 → 打印到 stdout → close。

### 4.3 mv 文件移动

**难度**: 低 | **涉及文件**: `user/shell.cpp`

```
mv <src> <dst>
```

同目录内用 rename，跨目录先 cp 再 delete。

### 4.4 多级路径解析

**难度**: 中 | **涉及文件**: `kernel/name.cpp`

当前 `namei()` 只能在当前目录查找。实现完整的路径解析：

- 绝对路径 `/etc/passwd`：从根目录 inode 开始逐级查找
- 相对路径 `../foo/bar`：从当前目录开始，处理 `..` 回退
- 路径分隔符 `/`，最大路径深度 32 层

### 4.5 I/O 重定向

**难度**: 低 | **涉及文件**: `user/shell.cpp`

```
cat foo.txt > bar.txt    (覆盖写入)
cat foo.txt >> bar.txt   (追加写入)
cat < foo.txt             (从文件读取)
```

shell 解析 `>` / `<` / `>>` 操作符，将命令的输入/输出重定向到指定文件。

### 4.6 管道

**难度**: 中 | **涉及文件**: `user/shell.cpp`

```
ls | grep foo
```

用临时文件连接两个命令：第一个命令输出到临时文件 → 第二个命令从临时文件读取 → 删除临时文件。

### 4.7 chmod 修改权限

**难度**: 低 | **涉及文件**: `kernel/chmod.cpp`（新增）, `user/shell.cpp`

```
chmod <mode> <filename>
```

`mode` 为八进制数字（如 755）。iget → 检查是否为 owner/root → 修改 `di_mode` 权限位 → iput。

### 4.8 chown 修改所有者

**难度**: 低 | **涉及文件**: `kernel/chown.cpp`（新增）, `user/shell.cpp`

```
chown <uid> <filename>
```

仅 root 可执行。修改 `di_uid`/`di_gid`。

### 4.9 用户管理命令

**难度**: 低 | **涉及文件**: `user/log.cpp`, `user/shell.cpp`

```
useradd <uid> <gid> <password>
userdel <uid>
passwd <uid> <newpass>
```

修改密码文件（inode 3 对应的数据块），root 权限检查。

---

## 五、高级特性

### 5.1 磁盘配额

**难度**: 高 | **涉及文件**: `filesys.h`, `balloc.cpp`, `ialloc.cpp`, `kernel/quota.cpp`（新增）

在超级块或独立配额文件中记录每个用户的块/文件数上限。`balloc()` 和 `ialloc()` 分配前检查配额。需要维护当前使用量的准确计数。

### 5.2 文件锁

**难度**: 中 | **涉及文件**: `filesys.h`, `kernel/flock.cpp`（新增）

在 `file` 结构中增加 `f_lock` 字段，支持共享锁和排他锁。`open()` 时检查目标文件是否已被排他锁定。

### 5.3 快照与写时复制（COW）

**难度**: 高 | **涉及文件**: 几乎所有

为数据块维护引用计数。快照时复制 inode 但不复制数据块——只增加块引用计数。写入时若引用计数 >1，先分配新块再写入。极大增加复杂度，超出课程设计范围。

### 5.4 网络 VFS 服务

**难度**: 高 | **涉及文件**: 整个项目结构调整

将虚拟磁盘的 `fopen`/`fread`/`fwrite` 替换为 socket I/O，实现客户端-服务端架构：
- 服务端持有虚拟磁盘文件，处理块读写请求
- 客户端通过 socket 发送块号 → 服务端返回块数据
- 需要设计简单的网络协议

---

## 六、代码质量

### 6.1 单元测试框架

**难度**: 低 | **涉及文件**: `test/`（新增目录）

因为磁盘是普通宿主机文件，测试流程可以是：
1. `format()` 创建全新虚拟磁盘
2. 调用被测试的 API
3. 通过 `fseek`/`fread` 直接读取磁盘文件验证数据结构
4. 测试结束删除 `filesystem` 文件

### 6.2 错误码标准化

**难度**: 中 | **涉及文件**: `filesys.h`, 全部 kernel 文件

替换到处散落的 `printf` 错误信息 + `return -1`/`return nullptr` 模式：

```cpp
enum vfs_error {
    E_VFS_OK = 0,
    E_VFS_NOENT,     // 文件不存在
    E_VFS_NOSPC,     // 磁盘满
    E_VFS_NOPERM,    // 权限不足
    E_VFS_EXIST,     // 文件已存在
    E_VFS_NOTDIR,    // 不是目录
    E_VFS_ISDIR,     // 是目录
    E_VFS_NFILE,     // 打开文件过多
    // ...
};
```

### 6.3 `g_fd` 的 RAII 封装

**难度**: 低 | **涉及文件**: `filesys.h`, `format.cpp`, `install.cpp`, `halt.cpp`

将裸 `FILE* g_fd` 封装为 RAII 类，自动在析构时 `fclose`，避免忘记关闭导致数据丢失。

---

## 七、推荐实施路线

```
第 1 步: 完成桩代码 (Layer 3/4/5) ─── 系统可用
          ↓
第 2 步: 修复 10 个已知 Bug        ─── 系统正确
          ↓
第 3 步: 多级路径解析 + cp/cat/mv   ─── Shell 可用
          ↓
第 4 步: 间接索引 + 文件时间戳      ─── 打破 5KB 限制
          ↓
第 5 步: 硬链接 + 符号链接 + rename ─── 功能完善
          ↓
第 6 步: LRU 缓存 + 超级块备份      ─── 生产可用
          ↓
第 7 步: fsck + 日志                ─── 企业级可靠性
```

每一步都可以作为一个独立的里程碑提交。前 5 步适合课程设计范围，第 6-7 步可作为额外加分项。
