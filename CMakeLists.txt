cmake_minimum_required(VERSION 3.16)
project(VFS VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 头文件路径
include_directories(${CMAKE_SOURCE_DIR}/include)

# ====== 源文件 — 每个 include/*.h 对应一个 src/*.cpp ====== #

set(VFS_SOURCES
    src/Core.cpp
    src/VirtualDisk.cpp
    src/BlockManage.cpp
    src/InodeCache.cpp
    src/DirectoryOpreation.cpp
    src/FileOperation.cpp
    src/User.cpp
    src/Editor.cpp
    src/Shell.cpp
    src/main.cpp
)

# 头文件 (for IDE integration)
set(VFS_HEADERS
    include/Core.h
    include/VirtualDisk.h
    include/BlockManage.h
    include/InodeCache.h
    include/DirectoryOpreation.h
    include/FileOperation.h
    include/User.h
    include/Shell.h
)

add_executable(vfs ${VFS_SOURCES} ${VFS_HEADERS})

# 平台相关设置
if(WIN32)
    set_target_properties(vfs PROPERTIES
        WIN32_EXECUTABLE OFF
    )
endif()

# ====== Unit Tests ======
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(Catch2)

# Library sources (everything except main.cpp)
set(VFS_LIB_SOURCES
    src/Core.cpp
    src/VirtualDisk.cpp
    src/BlockManage.cpp
    src/InodeCache.cpp
    src/DirectoryOpreation.cpp
    src/FileOperation.cpp
    src/User.cpp
    src/Editor.cpp
    src/Shell.cpp
)

set(TEST_SOURCES
    test/test_globals.cpp
    test/test_format.cpp
    test/test_balloc_bfree.cpp
    test/test_ifree.cpp
    test/test_login.cpp
    test/test_inode_dir.cpp
    test/test_error.cpp
)

add_executable(vfs_test ${VFS_LIB_SOURCES} ${TEST_SOURCES})
target_link_libraries(vfs_test PRIVATE Catch2::Catch2WithMain)

# CTest + Catch2 discover disabled — run tests manually:
#   cd build && ./vfs_test.exe
# include(CTest)
# enable_testing()
# include(Catch)
# catch_discover_tests(vfs_test)
