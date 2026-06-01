#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "test_helpers.h"

/* --- vfs_strerror() --- */

TEST_CASE("vfs_strerror() returns correct strings", "[error]") {
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_OK), "Success") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_NOSPC), "No space left on device") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_AUTH), "Authentication failure") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_IO), "Input/output error") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_INVAL), "Invalid argument") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_NFILE), "Too many open files") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_NOENT), "No such file or directory") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_NOPERM), "Permission denied") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_EXIST), "File already exists") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_NOTDIR), "Not a directory") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_ISDIR), "Is a directory") == 0);
    REQUIRE(std::strcmp(vfs_strerror(VfsError::E_VFS_BUSY), "Device or resource busy") == 0);
    REQUIRE(std::strcmp(vfs_strerror(static_cast<VfsError>(999)), "Unknown error") == 0);
}

/* --- balloc() --- */

TEST_CASE("balloc() returns DISKFULL when disk is full", "[error]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    /* Simulate exhausted free block stack */
    fx.superblock.s_nfree = 0;
    fx.superblock.s_pfree = 0;

    uint32_t result = fx.blocks.balloc();
    REQUIRE(result == DISKFULL);
}

/* --- ialloc() --- */

TEST_CASE("ialloc() returns nullptr when no free inodes", "[error]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    /* Simulate exhausted inode stack and no free inodes on disk */
    fx.superblock.s_ninode = 0;
    fx.superblock.s_pinode = 0;
    fx.superblock.s_rinode = 512; /* beyond all valid inodes */

    inode *result = fx.blocks.ialloc(fx.icache);
    REQUIRE(result == nullptr);
}

/* --- ifree() --- */

TEST_CASE("ifree() handles full stack gracefully", "[error]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    fx.superblock.s_ninode = NICINOD;
    fx.superblock.s_pinode = NICINOD - 1;
    fx.superblock.s_rinode = 100;

    fx.blocks.ifree(200);
    /* Should still update s_rinode even if stack is full */
    REQUIRE(fx.superblock.s_rinode == 100); /* 200 > 100, so unchanged */
}

/* --- login() --- */

static void setup_pwd(VfsFixture& fx) {
    fx.format();
    fx.install();
    fx.users.pwd_table()[0].p_uid = 0;
    fx.users.pwd_table()[0].p_gid = 0;
    std::strcpy(fx.users.pwd_table()[0].password, "root");
    fx.users.pwd_table()[1].p_uid = 1;
    fx.users.pwd_table()[1].p_gid = 1;
    std::strcpy(fx.users.pwd_table()[1].password, "pass1");
}

TEST_CASE("login() fails on bad password", "[error]") {
    VfsFixture fx;
    setup_pwd(fx);

    int32_t result = fx.users.login(0, "wrong");
    REQUIRE(result == 0);
}

TEST_CASE("login() fails on unknown uid", "[error]") {
    VfsFixture fx;
    setup_pwd(fx);

    int32_t result = fx.users.login(99, "pass1");
    REQUIRE(result == 0);
}

TEST_CASE("login() fails when user table is full", "[error]") {
    VfsFixture fx;
    setup_pwd(fx);

    /* Fill all user slots */
    for (int i = 0; i < USERNUM; i++) {
        fx.users.user_at(i).u_uid = 100 + i;
    }

    int32_t result = fx.users.login(0, "root");
    REQUIRE(result == 0);
}

/* --- logout() --- */

TEST_CASE("logout() fails on unknown uid", "[error]") {
    VfsFixture fx;
    setup_pwd(fx);

    int32_t result = fx.users.logout(99);
    REQUIRE(result == 0);
}
