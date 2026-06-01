/**
 * User.cpp — Layer 6: User authentication and session management
 * login / logout / halt / load_password_file
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "Core.h"

// ====== Constructor ======

UserManager::UserManager(VirtualDisk& disk, InodeCache& icache,
                         DirectoryManager& dirs, FileOperator& files,
                         filsys& superblock)
    : m_disk(disk), m_icache(icache), m_dirs(dirs),
      m_files(files), m_superblock(superblock)
{
    for (int i = 0; i < USERNUM; i++) {
        m_users[i].u_uid = 0;
        m_users[i].u_gid = 0;
        m_users[i].u_default_mode = DEFAULTMODE;
        for (int j = 0; j < NOFILE; j++) {
            m_users[i].u_ofile[j] = SYSOPENFILE + 1;
        }
    }
    m_user_id = -1;
}

// ====== load_password_file() ======

void UserManager::load_password_file()
{
    dinode pwd_inode;
    fseek(m_disk.handle(), DINODESTART + 3 * DINODESIZ, SEEK_SET);
    fread(&pwd_inode, sizeof(dinode), 1, m_disk.handle());

    char block[BLOCKSIZ];
    memset(block, 0, BLOCKSIZ);
    fseek(m_disk.handle(), DATASTART + pwd_inode.di_addr[0] * BLOCKSIZ, SEEK_SET);
    fread(block, BLOCKSIZ, 1, m_disk.handle());
    memcpy(m_pwd, block, sizeof(pwd) * PWDNUM);
}

// ====== login() ======

int UserManager::login(unsigned short uid, const char *passwd)
{
    if (passwd[0] == '\0') return 0;

    for (int i = 0; i < PWDNUM; i++) {
        if (m_pwd[i].p_uid == uid && strcmp(passwd, m_pwd[i].password) == 0) {
            for (int j = 0; j < USERNUM; j++) {
                if (m_users[j].u_uid == 0) {
                    m_users[j].u_uid = uid;
                    m_users[j].u_gid = m_pwd[i].p_gid;
                    m_users[j].u_default_mode = DEFAULTMODE;
                    for (int k = 0; k < NOFILE; k++) {
                        m_users[j].u_ofile[k] = SYSOPENFILE + 1;
                    }
                    m_user_id = j;
                    return 1;
                }
            }
            return 0;
        }
    }
    return 0;
}

// ====== logout() ======

int UserManager::logout(unsigned short uid)
{
    for (int i = 0; i < USERNUM; i++) {
        if (uid == m_users[i].u_uid) {
            for (int j = 0; j < NOFILE; j++) {
                if (m_users[i].u_ofile[j] != SYSOPENFILE + 1) {
                    m_files.close(m_users[i], static_cast<unsigned short>(j));
                }
                m_users[i].u_ofile[j] = SYSOPENFILE + 1;
            }

            m_users[i].u_uid = 0;
            m_users[i].u_gid = 0;
            m_users[i].u_default_mode = 0;
            if (m_user_id == i) m_user_id = -1;
            return 1;
        }
    }
    return 0;
}

// ====== halt() ======

void UserManager::halt()
{
    m_dirs.chdir("..");
    m_icache.iput(m_dirs.cur_path_inode());
    m_dirs.set_cur_path_inode(nullptr);

    for (int i = 0; i < USERNUM; i++) {
        logout(m_users[i].u_uid);
    }

    if (m_disk.is_open()) {
        if (fseek(m_disk.handle(), BLOCKSIZ, SEEK_SET) == 0) {
            if (fwrite(&m_superblock, sizeof(m_superblock), 1, m_disk.handle()) != 1) {
                printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_IO));
            } else {
                fflush(m_disk.handle());
            }
        } else {
            printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_IO));
        }
        fclose(m_disk.handle());
    }

    printf("Good Bye...\n");
    exit(0);
}
