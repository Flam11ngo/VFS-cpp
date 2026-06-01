#include <catch2/catch_test_macros.hpp>

#include "test_helpers.h"

TEST_CASE("ifree() adds inode back to free stack", "[ifree]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    fx.superblock.s_ninode = 10;
    fx.superblock.s_pinode = 9;
    fx.superblock.s_rinode = 4;

    fx.blocks.ifree(100);

    /* Stack should have one more entry */
    REQUIRE(fx.superblock.s_ninode == 11);
    REQUIRE(fx.superblock.s_pinode == 10);
    REQUIRE(fx.superblock.s_inode[10] == 100);
    REQUIRE(fx.superblock.s_fmod == SUPDATE);
}

TEST_CASE("ifree() updates s_rinode for lower-numbered inode", "[ifree]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    fx.superblock.s_ninode = 5;
    fx.superblock.s_pinode = 4;
    fx.superblock.s_rinode = 50; /* remembered inode */

    fx.blocks.ifree(30); /* lower than current s_rinode */

    REQUIRE(fx.superblock.s_rinode == 30);
    REQUIRE(fx.superblock.s_ninode == 6);
    REQUIRE(fx.superblock.s_pinode == 5);
}

TEST_CASE("ifree() does not update s_rinode for higher-numbered inode", "[ifree]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    fx.superblock.s_ninode = 5;
    fx.superblock.s_pinode = 4;
    fx.superblock.s_rinode = 50;

    fx.blocks.ifree(80); /* higher than current s_rinode */

    REQUIRE(fx.superblock.s_rinode == 50); /* unchanged */
}
