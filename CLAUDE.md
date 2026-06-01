# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Configure (generates build/compile_commands.json for clangd)
cmake --preset ninja-debug

# Build
cmake --build build

# Run
./build/vfs.exe

# Tests
./build/vfs_test.exe
```

## Architecture

7-layer simulated UNIX file system running on a single host file (`filesystem`) as the virtual disk.

Headers in `include/`, sources in `src/` — one `.cpp` per `.h`.

| Layer | Class | Header | Source |
|-------|-------|--------|--------|
| 7: Shell | *(free functions)* | — | `src/main.cpp` |
| 6: User mgmt | `UserManager` | `include/User.h` | `src/User.cpp` |
| 5: Directory ops | `DirectoryManager` | `include/DirectoryOpreation.h` | `src/DirectoryOpreation.cpp` |
| 4: File ops | `FileOperator` | `include/FileOperation.h` | `src/FileOperation.cpp` |
| 3: Inode cache | `InodeCache` | `include/InodeCache.h` | `src/InodeCache.cpp` |
| 2: Block mgmt | `BlockManager` | `include/BlockManage.h` | `src/BlockManage.cpp` |
| 1: Virtual disk | `VirtualDisk` | `include/VirtualDisk.h` | `src/VirtualDisk.cpp` |
| — | types/constants/errors | `include/Core.h` | `src/Core.cpp` |

### Dependency wiring (main.cpp)
```
filsys superblock (owned by main)
VirtualDisk disk
  → BlockManager blocks(disk, superblock)
    → InodeCache icache(disk, blocks)
      → DirectoryManager dirs(disk, icache, blocks)
        → FileOperator files(disk, blocks, icache, dirs, superblock)
          → UserManager users(disk, icache, dirs, files, superblock)
```

### Disk layout (512B blocks)

| Region | Offset | Size | Content |
|--------|--------|------|---------|
| Boot block | 0 | 1 block | Unused |
| Superblock | 512B | 1 block | `filsys` struct |
| Inode area | 1024B | 32 blocks (16KB) | 512 `dinode` at 32B each |
| Data area | 17408B | 512 blocks (256KB) | File/directory data, password file |

### Key data structures (defined in `Core.h`)

- **dinode** (32B): `di_number` (links), `di_mode` (type+perms), `di_uid/gid`, `di_size`, `di_addr[10]` (direct blocks, max 5KB file)
- **inode** (memory): dinode fields + `i_forw/i_back` (hash chain), `i_flag`, `i_ino`, `i_count`
- **direct** (16B): `d_name[14]` + `d_ino` — 128 entries per directory block
- **file**: `f_flag`, `f_count`, `f_inode`, `f_off` — 40 slots in `FileOperator::m_ofile[]`
- **user_t**: `u_uid/gid`, `u_ofile[20]` — 10 slots in `UserManager::m_users[]`
- **filsys**: superblock — `s_free[50]` (free block stack), `s_inode[50]` (free inode stack)
