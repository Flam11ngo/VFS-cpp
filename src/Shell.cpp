/**
 * Shell.cpp — Command REPL
 *
 * Uses g_core (declared in Core.h) to access all VFS subsystems.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>

#include "Core.h"

// ====== Helpers ======

static std::vector<std::string> parse_cmd(const std::string &line)
{
    std::vector<std::string> args;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) args.push_back(token);
    return args;
}

static void show_help()
{
    std::cout << "Available commands:\n"
              << "  login <uid> <password>   - User login\n"
              << "  logout                   - User logout\n"
              << "  dir / ls                 - List directory\n"
              << "  mkdir <dirname>          - Create directory\n"
              << "  cd <dirname>             - Change directory\n"
              << "  creat <filename> <mode>  - Create file\n"
              << "  open <filename> <mode>   - Open file\n"
              << "  read <fd> <size>         - Read file\n"
              << "  write <fd> <size>        - Write file\n"
              << "  close <fd>               - Close file\n"
              << "  delete <filename>        - Delete file\n"
              << "  format                   - Format disk\n"
              << "  halt / exit / quit       - Exit system\n";
}

static bool parse_uint(const std::string &text, unsigned long &value)
{
    try {
        value = std::stoul(text);
        return true;
    } catch (const std::exception &) {
        std::cout << "Invalid number: " << text << '\n';
        return false;
    }
}

// ====== run() ======

void Shell::run()
{
    auto& dirs  = g_core.dirs();
    auto& files = g_core.files();
    auto& users = g_core.users();
    auto& disk  = g_core.disk();
    auto& blocks = g_core.blocks();
    auto& icache = g_core.icache();

    std::string line;
    while (true) {
        std::cout << "$ ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        auto args = parse_cmd(line);
        if (args.empty()) continue;

        const std::string &cmd = args[0];

        if (cmd == "help") {
            show_help();
        } else if (cmd == "login" && args.size() >= 3) {
            unsigned long uid = 0;
            if (!parse_uint(args[1], uid)) continue;
            if (users.login(static_cast<uint16_t>(uid), args[2].c_str()))
                std::cout << "Login successful.\n";
            else
                std::cout << "Login failed.\n";
        } else if (cmd == "logout") {
            if (users.current_user_id() >= 0)
                users.logout(users.current_user().u_uid);
        } else if (cmd == "dir" || cmd == "ls") {
            dirs.dir_list();
        } else if (cmd == "mkdir" && args.size() >= 2) {
            dirs.mkdir(args[1].c_str(), users.current_user_id());
        } else if (cmd == "cd" && args.size() >= 2) {
            dirs.chdir(args[1].c_str());
        } else if (cmd == "creat" && args.size() >= 3) {
            unsigned long mode = 0;
            if (!parse_uint(args[2], mode)) continue;
            int fd = files.creat(users.current_user(), args[1].c_str(),
                                 static_cast<uint16_t>(mode));
            if (fd >= 0) std::cout << "File created, fd=" << fd << "\n";
        } else if (cmd == "open" && args.size() >= 3) {
            unsigned long mode = 0;
            if (!parse_uint(args[2], mode)) continue;
            uint16_t fd = files.open(users.current_user(), args[1].c_str(),
                                     static_cast<uint16_t>(mode));
            if (fd != (uint16_t)-1) std::cout << "File opened, fd=" << fd << "\n";
        } else if (cmd == "read" && args.size() >= 3) {
            char buf[BUFSIZ];
            memset(buf, 0, sizeof(buf));
            unsigned long fd = 0, size = 0;
            if (!parse_uint(args[1], fd) || !parse_uint(args[2], size)) continue;
            uint32_t n = files.read(users.current_user(),
                                    static_cast<uint32_t>(fd), buf,
                                    static_cast<uint32_t>(size));
            std::cout << "Read " << n << " bytes: ";
            for (uint32_t i = 0; i < n; i++) std::cout << buf[i];
            std::cout << "\n";
        } else if (cmd == "write" && args.size() >= 3) {
            unsigned long fd = 0, size = 0;
            if (!parse_uint(args[1], fd) || !parse_uint(args[2], size)) continue;
            uint32_t n = files.write(users.current_user(),
                                     static_cast<uint32_t>(fd), "test data",
                                     static_cast<uint32_t>(size));
            std::cout << "Written " << n << " bytes.\n";
        } else if (cmd == "close" && args.size() >= 2) {
            unsigned long fd = 0;
            if (!parse_uint(args[1], fd)) continue;
            files.close(users.current_user(), static_cast<uint16_t>(fd));
        } else if (cmd == "delete" && args.size() >= 2) {
            files.delete_file(args[1].c_str());
        } else if (cmd == "format") {
            disk.format(blocks.superblock());
            icache.iput(dirs.cur_path_inode());
            dirs.set_cur_path_inode(nullptr);
            disk.install(blocks.superblock());
        } else if (cmd == "halt" || cmd == "exit" || cmd == "quit") {
            g_core.exit();
            break;
        } else {
            std::cout << "Unknown command: " << cmd
                      << "\nType 'help' for available commands.\n";
        }
    }
}
