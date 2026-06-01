# 虚拟文件系统 (VFS) 设计方案

## 一、项目概述

模拟 UNIX 文件系统，实现多用户、多级目录结构的文件管理功能。系统在宿主机上申请一块内存/文件作为虚拟磁盘，在其上构建文件卷结构，提供命令行交互界面。

**开发小组**: 4人 | **开发周期**: 第13-14周

---

## 二、系统架构（7层）

```
┌─────────────────────────────────────────────────────────┐
│  Layer 7: Shell / 命令解释层                              │
│  ──────────────────────────────                          │
│  命令解析、菜单界面、交互处理、结果输出                      │
│  命令: login logout dir mkdir cd creat open read write    │
│        close delete format halt exit                      │
├─────────────────────────────────────────────────────────┤
│  Layer 6: 用户管理层                                      │
│  ──────────────────────────────                          │
│  login / logout 用户认证                                  │
│  pwd[N] 口令表管理                                        │
│  user[N] 登录用户表管理                                   │
├─────────────────────────────────────────────────────────┤
│  Layer 5: 目录操作层                                      │
│  ──────────────────────────────                          │
│  mkdir / chdir / dir / rmdir                             │
│  namei() 按名查找目录项                                    │
│  iname() 寻找空目录项                                      │
│  路径解析（支持多级路径）                                    │
├─────────────────────────────────────────────────────────┤
│  Layer 4: 文件操作层                                      │
│  ──────────────────────────────                          │
│  creat / open / close / delete / read / write             │
│  access() 权限检查 (owner/group/other × r/w/x)            │
│  sys_ofile[N] 系统打开文件表管理                           │
│  u_ofile[N] 用户打开文件表管理                             │
├─────────────────────────────────────────────────────────┤
│  Layer 3: Inode 管理层                                    │
│  ──────────────────────────────                          │
│  iget(dinodeid) 获取内存inode（Hash查找/磁盘加载）          │
│  iput(pinode) 释放内存inode（引用计数/回写磁盘）            │
│  hinode[128] Hash链表管理                                 │
├─────────────────────────────────────────────────────────┤
│  Layer 2: 块管理层                                        │
│  ──────────────────────────────                          │
│  balloc()/bfree() 空闲盘块分配回收（成组链接法，50块/组）    │
│  ialloc()/ifree() 空闲inode分配回收（超级块栈管理）          │
│  filsys 超级块读写与同步                                   │
├─────────────────────────────────────────────────────────┤
│  Layer 1: 虚拟磁盘层                                      │
│  ──────────────────────────────                          │
│  对宿主机文件 "filesystem" 的块级读写 (512B/块)            │
│  format() 磁盘格式化                                      │
│  install() 系统加载                                       │
└─────────────────────────────────────────────────────────┘
```

---

## 三、虚拟磁盘布局

| 区域 | 偏移地址 | 大小 | 说明 |
|------|---------|------|------|
| 引导块 | 0 | 1 block (512B) | 保留，未使用 |
| 超级块 | 512B (1×BLOCKSIZ) | 1 block | filsys 结构体：空闲块栈 + 空闲inode栈 + 元信息 |
| Inode 区 | 1024B (2×BLOCKSIZ) | 32 blocks (16KB) | 磁盘inode(dinode)，每项32B，共512个inode |
| 数据区 | 17408B (34×BLOCKSIZ) | 512 blocks (256KB) | 目录文件 + 普通文件数据 + 口令文件 |

**磁盘常量**:
- `BLOCKSIZ = 512` — 每块字节数
- `DINODEBLK = 32` — inode区占用块数
- `FILEBLK = 512` — 数据区占用块数
- `DINODESTART = 2*BLOCKSIZ` — inode区起始地址
- `DATASTART = (2+DINODEBLK)*BLOCKSIZ` — 数据区起始地址

---

## 四、核心数据结构

### 4.1 超级块 (struct filsys)
```
s_isize      — inode区总块数
s_fsize      — 数据区总块数
s_nfree      — 当前空闲块栈中空闲块数
s_pfree      — 空闲块栈指针
s_free[50]   — 空闲块号栈（成组链接法）
s_ninode     — 当前空闲inode栈中空闲inode数
s_pinode     — 空闲inode栈指针
s_inode[50]  — 空闲inode号栈
s_rinode     — 铭记inode（搜索空闲inode起始位置）
s_fmod       — 超级块修改标志 (SUPDATE)
```

### 4.2 磁盘 inode (struct dinode) — 32字节
```
di_number    — 关联文件数（硬链接计数，0表示可回收）
di_mode      — 文件类型(DIFILE/DIDIR) + 访问权限(rwx)
di_uid       — 文件所有者UID
di_gid       — 文件所属组GID
di_size      — 文件大小（字节）
di_addr[10]  — 文件数据块索引数组（直接索引，最多10块=5KB）
```

### 4.3 内存 inode (struct inode)
```
i_forw/i_back — 前向/后向指针（Hash链表双向链接）
i_flag        — 修改标志 (IUPDATE/SUPDATE)
i_ino         — 对应磁盘inode编号
i_count       — 引用计数
di_number/di_mode/di_uid/di_gid/di_size/di_addr[10] — 同dinode
```

### 4.4 目录项 (struct direct) — 16字节
```
d_name[14]   — 文件名（区分大小写）
d_ino        — 文件对应的inode号
```

### 4.5 目录结构 (struct dir)
```
direct[128]  — 目录项数组（每个目录最多128个文件）
size         — 当前目录项个数
```

### 4.6 文件表项 (struct file)
```
f_flag       — 操作标志 (FREAD/FWRITE/FAPPEND)
f_count      — 引用计数
f_inode      — 指向内存inode的指针
f_off        — 文件读写偏移量
```

### 4.7 用户结构 (struct user)
```
u_default_mode — 默认文件权限
u_uid          — 用户ID
u_gid          — 组ID
u_ofile[20]    — 用户打开文件表（存放sys_ofile下标）
```

### 4.8 口令项 (struct pwd)
```
p_uid        — 用户ID
p_gid        — 组ID
password[12] — 口令
```

---

## 五、核心数据结构关系图

```
┌──────────────────┐
│   超级块 filsys    │
│  s_free[50]      │──→ 空闲盘块成组链接
│  s_inode[50]     │──→ 空闲inode栈
│  s_fmod          │
└──────────────────┘

┌──────────────────┐     ┌─────────────────────┐
│  hinode[128]     │────→│ Hash链表 (内存inode)  │
│  Hash表头        │     │ i_forw → i_forw → ... │
└──────────────────┘     └─────────────────────┘
                                │
                    ┌───────────┴───────────┐
                    │   struct inode         │
                    │   i_ino (inode号)       │
                    │   di_addr[10] (数据块)  │
                    │   di_size/di_mode/...  │
                    └───────────────────────┘

┌──────────────────────┐      ┌──────────────────────┐
│  sys_ofile[40]       │←────│  user[10].u_ofile[20] │
│  系统打开文件表        │      │  用户打开文件表         │
│  f_inode → inode     │      └──────────────────────┘
│  f_off (读写偏移)     │
└──────────────────────┘

┌──────────────────────┐
│  dir                  │
│  direct[128] 目录项    │
│  d_name[14] → d_ino   │
└──────────────────────┘

┌──────────────────────┐
│  pwd[32]             │
│  p_uid → password    │
└──────────────────────┘
```

---

## 六、关键算法

### 6.1 空闲盘块管理 — 成组链接法

将空闲块每50个分一组。超级块`s_free[50]`存放当前组空闲块号，`s_nfree`为块数。

**分配 balloc()**:
```
if s_nfree == 1:
    block = s_free[0]           // 取出最后一块
    将block中存储的下一组空闲块信息读入s_free
else:
    block = s_free[s_pfree]     // 弹出栈顶
    s_nfree--
return block
```

**回收 bfree(block)**:
```
if s_nfree == 50:              // 栈满
    将s_free写入block中         // 当前组存入新释放的块
    s_free[0] = block           // 新块成为新组
    s_nfree = 1
else:
    s_free[s_nfree] = block
    s_nfree++
```

### 6.2 空闲 inode 管理 — 超级块栈

类似空闲块管理，`s_inode[50]`存放空闲inode号。

### 6.3 内存 inode 缓存 — Hash 链表

- 128个Hash桶: `hinode[dinodeid % 128]`
- 每个桶维护双向链表，按 inode 号查找
- `iget()`: Hash查找 → 命中则i_count++返回；未命中则分配新内存inode，从磁盘dinode区读取
- `iput()`: i_count--；若为0且di_number==0则释放盘块和磁盘inode；回写后从链表摘除

### 6.4 文件物理结构 — 直接索引

每个inode有`di_addr[10]`共10个直接索引项，每项指向一个数据块(512B)，最大文件 = 10×512B = 5KB。

---

## 七、模块接口（函数调用关系）

```
          ┌─────────────────┐
          │  Shell / main()  │
          └───────┬─────────┘
                  │
    ┌─────────────┼─────────────┐
    │             │             │
    ▼             ▼             ▼
 login()      _dir()        creat()
 logout()     mkdir()       aopen()
 halt()       chdir()       close()
              namei()       delete()
              iname()       read()/write()
                            access()
    │             │             │
    └─────────────┼─────────────┘
                  │
          ┌───────┴───────┐
          │   iget/iput    │
          └───────┬───────┘
                  │
    ┌─────────────┼─────────────┐
    │             │             │
    ▼             ▼             ▼
 balloc()     bfree()      ialloc()     ifree()
    │             │             │
    └─────────────┼─────────────┘
                  │
          ┌───────┴───────┐
          │  虚拟磁盘 I/O   │
          │  fopen/fread   │
          │  fwrite/fseek  │
          └───────────────┘
```

---

## 八、4人分工

| 成员 | 角色 | 负责模块 | 关键文件 | 依赖接口 |
|------|------|---------|---------|---------|
| **A** | 存储管理 | Layer 1+2 | `format.c` `install.c` `balloc.c` `bfree.c` `ialloc.c` `ifree.c` | 无（底层） |
| **B** | Inode+目录 | Layer 3+5 | `igetput.c` `name.c` `dir.c` | balloc/bfree (from A) |
| **C** | 文件操作 | Layer 4 | `access.c` `creat.c` `open.c` `close.c` `delete.c` `rdwt.c` | iget/iput+namei (from B) |
| **D** | 用户+Shell | Layer 6+7 | `log.c` `halt.c` `main.c` `shell.c` | 全部 (from A/B/C) |

### 每个成员的接口约定

**A 提供给 B/C/D**:
- `unsigned int balloc()` → 返回空闲块号
- `void bfree(unsigned int block_num)`
- `struct inode *ialloc()` → 返回已分配inode
- `void ifree(unsigned int dinodeid)`
- `void format()` — 格式化虚拟磁盘
- `void install()` — 加载文件系统

**B 提供给 C/D**:
- `struct inode *iget(unsigned int dinodeid)` → 获取内存inode
- `void iput(struct inode *pinode)` → 释放内存inode
- `unsigned int namei(char *name)` → 按名查找，返回inode号
- `unsigned short iname(char *name)` → 查找空目录项位置
- `void mkdir(char *dirname)`
- `void chdir(char *dirname)`
- `void _dir()` — 列出当前目录

**C 提供给 D**:
- `unsigned int access(user_id, inode, mode)` → 权限检查
- `int creat(user_id, filename, mode)` → 返回fd
- `unsigned short aopen(user_id, filename, openmode)` → 返回fd
- `void close(user_id, fd)`
- `void delete(char *filename)`
- `unsigned int read(fd, buf, size)` → 返回实际读取字节数
- `unsigned int write(fd, buf, size)` → 返回实际写入字节数

**D 提供给全体**:
- `int login(uid, passwd)` → 返回user_id
- `int logout(uid)`
- `void halt()` — 安全退出系统
- 主程序入口和命令循环

---

## 九、开发计划

### 阶段1: 公共基础 (1天, 4人共同)
- [ ] 确定数据结构定义，编写 `filesys.h` 头文件
- [ ] 创建 CMakeLists.txt 项目文件
- [ ] 确认所有接口函数签名
- [ ] 搭建模块骨架文件

### 阶段2: 并行开发 (3-4天)
- [ ] **成员A**: format → balloc/bfree → ialloc/ifree → install
- [ ] **成员B**: iget/iput → namei/iname → mkdir/chdir/dir
- [ ] **成员C**: access → creat/open/close → read/write → delete
- [ ] **成员D**: login/logout/halt → 命令解析器 → main主循环

### 阶段3: 集成联调 (1-2天)
- [ ] 各模块集成编译
- [ ] 修复接口不匹配问题
- [ ] 修复原始代码已知bug（见附录）
- [ ] 端到端流程测试

### 阶段4: 验收 (1天)
- [ ] 编写测试用例
- [ ] 撰写课程设计报告
- [ ] 填写验收表
- [ ] 打包提交材料

---

## 十、附录：原始样例代码已知Bug

原始代码（`filesysorig-原始源程序/`）中存在以下已知问题，在新系统中需要修复：

1. **namei() 返回值歧义**: 返回0既表示"未找到"也可以表示找到索引0的文件
2. **login() strcmp 逻辑错误**: `strcmp(passwd, pwd[i].password)` 应该 `== 0` 才表示密码匹配
3. **aopen() 条件判断错误**: `if (dinodeid != NULL)` 应为 `if (dinodeid == NULL)`
4. **creat() access参数**: `access(user_id, inode, inode)` 第三个参数应为 mode 而非 inode
5. **iput() 链表操作**: 当 `pinode->i_back == pinode` 时（自引用）的链表修复逻辑有问题
6. **chdir() 打包目录**: 当前目录回写逻辑中目录项压缩有bug
7. **balloc() 读取下一组空闲块**: 未在读取前设置文件偏移位置
8. **bfree() 写入**: 栈满时将块写入磁盘的 fwrite 应在实际磁盘位置
9. **read()/write() 块计算**: 跨块读写时的块号计算边界条件
10. **halt() close参数错误**: `close(user[i].u_ofile[j])` 缺少 user_id 参数

---

## 十一、验收问题准备（报告需回答）

1. 文件卷的组织结构是什么？
2. 主要数据结构间的关系？
3. 格式化(Format)所作的工作有哪些？
4. 具体哪些因素影响了管理的文件的最大长度？
5. 不同于原程序，你解决了哪些问题或增加了哪些功能？

---

## 参考文献

1. 张尧学, 计算机操作系统教程（第3/4版），清华大学出版社
2. 汤小丹等, 计算机操作系统（第4版），西安电子科技大学出版社
3. 陈葆玉译, UNIX操作系统设计，北京大学出版社
