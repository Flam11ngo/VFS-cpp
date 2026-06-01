#include <catch2/catch_test_macros.hpp>

#include "test_helpers.h"

static void setup_passwords(VfsFixture& fx) {
    fx.format();
    fx.install();
    /* Override pwd table entries directly */
    fx.users.pwd_table()[0].p_uid = 0;
    fx.users.pwd_table()[0].p_gid = 0;
    std::strcpy(fx.users.pwd_table()[0].password, "root");

    fx.users.pwd_table()[1].p_uid = 1;
    fx.users.pwd_table()[1].p_gid = 1;
    std::strcpy(fx.users.pwd_table()[1].password, "pass1");
}

TEST_CASE("login() with correct credentials succeeds", "[login]") {
    VfsFixture fx;
    setup_passwords(fx);

    int32_t result = fx.users.login(0, "root");
    REQUIRE(result == 1);
    REQUIRE(fx.users.current_user_id() == 0);
    REQUIRE(fx.users.user_at(0).u_uid == 0);
    REQUIRE(fx.users.user_at(0).u_gid == 0);
    REQUIRE(fx.users.user_at(0).u_default_mode == DEFAULTMODE);
}

TEST_CASE("login() with wrong password fails", "[login]") {
    VfsFixture fx;
    setup_passwords(fx);

    int32_t result = fx.users.login(0, "wrong");
    REQUIRE(result == 0);
    REQUIRE(fx.users.current_user_id() == -1);
}

TEST_CASE("login() with unknown uid fails", "[login]") {
    VfsFixture fx;
    setup_passwords(fx);

    int32_t result = fx.users.login(99, "root");
    REQUIRE(result == 0);
}

TEST_CASE("login() fills the first available user slot", "[login]") {
    VfsFixture fx;
    setup_passwords(fx);

    /* Pre-fill slot 0 */
    fx.users.user_at(0).u_uid = 5;
    fx.users.user_at(1).u_uid = 0; /* empty */

    int32_t result = fx.users.login(1, "pass1");
    REQUIRE(result == 1);
    REQUIRE(fx.users.current_user_id() == 1); /* should use slot 1 */
    REQUIRE(fx.users.user_at(1).u_uid == 1);
    REQUIRE(fx.users.user_at(1).u_gid == 1);
}

TEST_CASE("logout() clears user session", "[logout]") {
    VfsFixture fx;
    setup_passwords(fx);
    fx.users.login(0, "root");

    int32_t result = fx.users.logout(0);
    REQUIRE(result == 1);
    REQUIRE(fx.users.user_at(0).u_uid == 0);
    REQUIRE(fx.users.user_at(0).u_gid == 0);
    REQUIRE(fx.users.user_at(0).u_default_mode == 0);
    REQUIRE(fx.users.current_user_id() == -1);
}

TEST_CASE("logout() with unknown uid returns 0", "[logout]") {
    VfsFixture fx;
    setup_passwords(fx);

    int32_t result = fx.users.logout(99);
    REQUIRE(result == 0);
}
