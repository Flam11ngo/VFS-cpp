#pragma once

#include <cstdio>
#include <cstring>

#include "Core.h"

/**
 * Test fixture that creates and wires all VFS subsystems.
 * Usage:
 *   VfsFixture fx;
 *   fx.format();           // create fresh disk
 *   fx.balloc();           // allocate a block
 */
struct VfsFixture {
    filsys           superblock{};
    VirtualDisk      disk;
    BlockManager     blocks{disk, superblock};
    InodeCache       icache{disk, blocks};
    DirectoryManager dirs{disk, icache, blocks};
    FileOperator     files{disk, blocks, icache, dirs, superblock};
    UserManager      users{disk, icache, dirs, files, superblock};

    VfsFixture() {
        fs_cleanup();
    }

    ~VfsFixture() {
        fs_cleanup();
    }

    void format() {
        disk.format(superblock);
    }

    void install() {
        disk.install(superblock);
        // Re-init inode hash chains
        for (int i = 0; i < NHINO; i++) {
            icache.hash_table()[i].i_forw = nullptr;
        }
        // Load password file into user table
        users.load_password_file();
    }

    void load_root_dir() {
        inode* root = icache.iget(1);
        if (root) {
            dirs.set_cur_path_inode(root);
            dirs.load_dir(root);
        }
    }

    static void fs_cleanup() {
        std::remove("filesystem");
    }
};
