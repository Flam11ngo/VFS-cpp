/**
 * Core.cpp — VFS system facade + error strings
 *
 * Core owns and wires all 7 layers.  g_core is the single global instance.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <iostream>

#include "Core.h"
#include "VirtualDisk.h"
#include "BlockManage.h"
#include "InodeCache.h"
#include "DirectoryOpreation.h"
#include "FileOperation.h"
#include "User.h"

// ====== Error strings ======

const char *vfs_strerror(VfsError err)
{
    switch (err) {
    case VfsError::E_VFS_OK:      return "Success";
    case VfsError::E_VFS_NOENT:   return "No such file or directory";
    case VfsError::E_VFS_NOSPC:   return "No space left on device";
    case VfsError::E_VFS_NOPERM:  return "Permission denied";
    case VfsError::E_VFS_EXIST:   return "File already exists";
    case VfsError::E_VFS_NOTDIR:  return "Not a directory";
    case VfsError::E_VFS_ISDIR:   return "Is a directory";
    case VfsError::E_VFS_NFILE:   return "Too many open files";
    case VfsError::E_VFS_IO:      return "Input/output error";
    case VfsError::E_VFS_AUTH:    return "Authentication failure";
    case VfsError::E_VFS_BUSY:    return "Device or resource busy";
    case VfsError::E_VFS_INVAL:   return "Invalid argument";
    default:                      return "Unknown error";
    }
}

// ====== Global instance ======

Core g_core;

// ====== Core constructor / destructor ======

Core::Core()  = default;
Core::~Core()
{
    delete m_users;
    delete m_files;
    delete m_dirs;
    delete m_icache;
    delete m_blocks;
    delete m_disk;
}

// ====== init() ======

void Core::init()
{
    std::cout << "======================================\n"
              << "  Virtual File System (VFS) v1.0\n"
              << "  Simulated UNIX-like File System\n"
              << "======================================\n\n";

    m_disk = new VirtualDisk;

    std::cout << "Do you want to format the virtual disk? (y/n): ";
    char ch;
    std::cin >> ch;
    std::cin.ignore();

    if (ch == 'y' || ch == 'Y') {
        std::cout << "WARNING: Format will erase all data!\n"
                  << "Are you sure? (y/n): ";
        std::cin >> ch;
        std::cin.ignore();
        if (ch == 'y' || ch == 'Y') {
            m_disk->format(m_superblock);
            std::cout << "Format completed.\n";
        }
    }

    m_disk->install(m_superblock);
    std::cout << "File system loaded.\n\n";

    // Wire layers 2→6
    m_blocks = new BlockManager(*m_disk, m_superblock);
    m_icache = new InodeCache(*m_disk, *m_blocks);
    m_dirs   = new DirectoryManager(*m_disk, *m_icache, *m_blocks);
    m_files  = new FileOperator(*m_disk, *m_blocks, *m_icache, *m_dirs, m_superblock);
    m_users  = new UserManager(*m_disk, *m_icache, *m_dirs, *m_files, m_superblock);
    m_users->load_password_file();

    // Load root directory
    inode* root = m_icache->iget(1);
    if (root) {
        m_dirs->set_cur_path_inode(root);
        char block[BLOCKSIZ];
        memset(block, 0, BLOCKSIZ);
        fseek(m_disk->handle(), DATASTART + root->di_addr[0] * BLOCKSIZ, SEEK_SET);
        fread(block, BLOCKSIZ, 1, m_disk->handle());
        int n = root->di_size / sizeof(direct);
        if (n > (int)(BLOCKSIZ / sizeof(direct))) n = BLOCKSIZ / sizeof(direct);
        m_dirs->current_dir().entries.resize(n);
        memcpy(m_dirs->current_dir().entries.data(), block, n * sizeof(direct));
    }

    // Login
    std::cout << "Please login:\n"
              << "  Username (uid): ";
    unsigned short uid;
    std::cin >> uid;
    std::cin.ignore();
    std::cout << "  Password: ";
    std::string passwd;
    std::getline(std::cin, passwd);

    if (m_users->login(uid, passwd.c_str())) {
        std::cout << "Login successful!\n\n";
    } else {
        std::cout << "Login failed.\n";
        m_users->halt();
    }
}

// ====== exit() ======

void Core::exit()
{
    m_users->halt();
}
