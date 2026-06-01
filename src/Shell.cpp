/**
 * Shell.cpp — Filename-based command REPL
 *
 * Every file operation uses filenames.  An internal path→fd hash table
 * translates names to descriptors transparently.
 *
 * Defaults:
 *   creat / open   mode  →  0777 / FREAD
 *   read           size  →  whole file
 *   write                 →  interactive editor mode
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>

#include "Core.h"

// ====== Static members ======

std::unordered_map<std::string, int> Shell::s_path2fd;

int Shell::lookup_fd(const std::string& name)
{
    auto it = s_path2fd.find(name);
    return (it != s_path2fd.end()) ? it->second : -1;
}

void Shell::bind_fd(const std::string& name, int fd)
{
    s_path2fd[name] = fd;
}

void Shell::unbind_fd(const std::string& name)
{
    s_path2fd.erase(name);
}

uint32_t Shell::file_size(int fd)
{
    auto& files = g_core.files();
    auto& users = g_core.users();
    if (fd < 0 || fd >= NOFILE) return 0;
    unsigned short sys_no = users.current_user().u_ofile[fd];
    if (sys_no >= SYSOPENFILE) return 0;
    inode* ino = files.ofile_table()[sys_no].f_inode;
    return ino ? ino->di_size : 0;
}

std::string Shell::editor_read()
{
    std::cout << "  (enter text, blank line to finish)\n";
    std::string line, buf;
    int ln = 1;
    while (true) {
        std::cout << "  " << ln++ << "| ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) break;
        if (!buf.empty()) buf += '\n';
        buf += line;
    }
    return buf;
}

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
    std::cout
        << "Commands:\n"
        << "  login  <uid> <passwd>       Log in\n"
        << "  logout                       Log out\n"
        << "  ls | dir                     List current directory\n"
        << "  mkdir <name>                 Create subdirectory\n"
        << "  cd    <name>                 Change directory\n"
        << "  creat <name> [mode]          Create file  (default mode 0777)\n"
        << "  open  <name> [mode]          Open file    (default mode 1=read)\n"
        << "  close <name>                 Close file\n"
        << "  read  <name> [size]          Read file   (default: all bytes)\n"
        << "  write <name> [text ...]      Write file  (no text → editor)\n"
        << "  delete <name>                Delete file\n"
        << "  format                       Format disk\n"
        << "  halt | exit | quit           Shutdown\n";
}

static bool parse_uint(const std::string &text, unsigned long &value)
{
    try {
        value = std::stoul(text);
        return true;
    } catch (const std::exception &) {
        std::cout << "  Invalid number: " << text << '\n';
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

        // ============================================================
        //  help
        // ============================================================
        if (cmd == "help") {
            show_help();
        }

        // ============================================================
        //  login  <uid>  <password>
        // ============================================================
        else if (cmd == "login") {
            if (args.size() < 3) {
                std::cout << "Usage: login <uid> <password>\n";
                continue;
            }
            unsigned long uid = 0;
            if (!parse_uint(args[1], uid)) continue;
            if (users.login(static_cast<uint16_t>(uid), args[2].c_str()))
                std::cout << "  Login successful (uid=" << uid << ").\n";
            else
                std::cout << "  Login failed.\n";
        }

        // ============================================================
        //  logout
        // ============================================================
        else if (cmd == "logout") {
            if (users.current_user_id() < 0) {
                std::cout << "  Not logged in.\n";
                continue;
            }
            unsigned short cur = users.current_user().u_uid;
            if (users.logout(cur)) {
                s_path2fd.clear();
                std::cout << "  Logged out (uid=" << cur << ").\n";
            } else {
                std::cout << "  Logout failed.\n";
            }
        }

        // ============================================================
        //  ls  |  dir
        // ============================================================
        else if (cmd == "dir" || cmd == "ls") {
            dirs.dir_list();
        }

        // ============================================================
        //  mkdir  <name>
        // ============================================================
        else if (cmd == "mkdir") {
            if (args.size() < 2) {
                std::cout << "Usage: mkdir <dirname>\n";
                continue;
            }
            dirs.mkdir(args[1].c_str(), users.current_user_id());
        }

        // ============================================================
        //  cd  <name>
        // ============================================================
        else if (cmd == "cd") {
            if (args.size() < 2) {
                std::cout << "Usage: cd <dirname>\n";
                continue;
            }
            dirs.chdir(args[1].c_str());
        }

        // ============================================================
        //  creat  <name>  [mode]
        // ============================================================
        else if (cmd == "creat") {
            if (args.size() < 2) {
                std::cout << "Usage: creat <filename> [mode]\n";
                continue;
            }
            unsigned long mode = DEFAULTMODE;
            if (args.size() >= 3 && !parse_uint(args[2], mode)) continue;

            int fd = files.creat(users.current_user(), args[1].c_str(),
                                 static_cast<uint16_t>(mode));
            if (fd >= 0) {
                bind_fd(args[1], fd);
                std::cout << "  Created '" << args[1] << "'  fd=" << fd
                          << "  mode=" << std::oct << mode << std::dec << "\n";
            } else {
                std::cout << "  creat failed.\n";
            }
        }

        // ============================================================
        //  open  <name>  [mode]
        // ============================================================
        else if (cmd == "open") {
            if (args.size() < 2) {
                std::cout << "Usage: open <filename> [mode]\n";
                continue;
            }
            unsigned long mode = FREAD;
            if (args.size() >= 3 && !parse_uint(args[2], mode)) continue;

            uint16_t fd = files.open(users.current_user(), args[1].c_str(),
                                     static_cast<uint16_t>(mode));
            if (fd != static_cast<uint16_t>(-1)) {
                bind_fd(args[1], fd);
                std::cout << "  Opened '" << args[1] << "'  fd=" << fd
                          << "  mode=" << std::oct << mode << std::dec << "\n";
            } else {
                std::cout << "  open failed: file not found or permission denied.\n";
            }
        }

        // ============================================================
        //  close  <name>
        // ============================================================
        else if (cmd == "close") {
            if (args.size() < 2) {
                std::cout << "Usage: close <filename>\n";
                continue;
            }
            int fd = lookup_fd(args[1]);
            if (fd < 0) {
                std::cout << "  '" << args[1] << "' is not open.\n";
                continue;
            }
            files.close(users.current_user(), static_cast<uint16_t>(fd));
            unbind_fd(args[1]);
            std::cout << "  Closed '" << args[1] << "'.\n";
        }

        // ============================================================
        //  read  <name>  [size]
        // ============================================================
        else if (cmd == "read") {
            if (args.size() < 2) {
                std::cout << "Usage: read <filename> [size]\n";
                continue;
            }
            int fd = lookup_fd(args[1]);
            if (fd < 0) {
                // Auto-open with read mode
                uint16_t new_fd = files.open(users.current_user(), args[1].c_str(), FREAD);
                if (new_fd == static_cast<uint16_t>(-1)) {
                    std::cout << "  Cannot open '" << args[1] << "' for reading.\n";
                    continue;
                }
                fd = new_fd;
                bind_fd(args[1], fd);
                std::cout << "  (auto-opened '" << args[1] << "' fd=" << fd << ")\n";
            }

            uint32_t size = file_size(fd);          // default: whole file
            if (args.size() >= 3) {
                unsigned long s = 0;
                if (!parse_uint(args[2], s)) continue;
                size = static_cast<uint32_t>(s);
            }

            if (size == 0) {
                std::cout << "  (empty file)\n";
                continue;
            }

            char* buf = new char[size + 1];
            memset(buf, 0, size + 1);
            uint32_t n = files.read(users.current_user(),
                                    static_cast<uint32_t>(fd), buf, size);
            std::cout << "  [" << n << " bytes]  ";
            for (uint32_t i = 0; i < n; i++) std::cout << buf[i];
            std::cout << "\n";
            delete[] buf;
        }

        // ============================================================
        //  write  <name>  [text ...]
        // ============================================================
        else if (cmd == "write") {
            if (args.size() < 2) {
                std::cout << "Usage: write <filename> [text ...]\n";
                continue;
            }
            int fd = lookup_fd(args[1]);
            if (fd < 0) {
                // Auto-open with write mode; create if it doesn't exist
                uint16_t new_fd = files.open(users.current_user(), args[1].c_str(), FWRITE);
                if (new_fd == static_cast<uint16_t>(-1)) {
                    // Try creat as fallback
                    int cfd = files.creat(users.current_user(), args[1].c_str(), DEFAULTMODE);
                    if (cfd < 0) {
                        std::cout << "  Cannot open or create '" << args[1] << "'.\n";
                        continue;
                    }
                    new_fd = static_cast<uint16_t>(cfd);
                }
                fd = new_fd;
                bind_fd(args[1], fd);
                std::cout << "  (auto-opened '" << args[1] << "' fd=" << fd << ")\n";
            }

            std::string text;
            if (args.size() >= 3) {
                // Text provided on command line — re-join remaining args
                for (size_t i = 2; i < args.size(); i++) {
                    if (i > 2) text += ' ';
                    text += args[i];
                }
            } else {
                // No text → enter interactive editor mode
                text = editor_read();
            }

            uint32_t n = files.write(users.current_user(),
                                     static_cast<uint32_t>(fd),
                                     text.c_str(),
                                     static_cast<uint32_t>(text.size()));
            std::cout << "  Written " << n << " bytes to '" << args[1] << "'.\n";
        }

        // ============================================================
        //  delete  <name>
        // ============================================================
        else if (cmd == "delete") {
            if (args.size() < 2) {
                std::cout << "Usage: delete <filename>\n";
                continue;
            }
            int fd = lookup_fd(args[1]);
            if (fd >= 0) {
                files.close(users.current_user(), static_cast<uint16_t>(fd));
                unbind_fd(args[1]);
            }
            files.delete_file(args[1].c_str());
            std::cout << "  Deleted '" << args[1] << "'.\n";
        }

        // ============================================================
        //  format
        // ============================================================
        else if (cmd == "format") {
            std::cout << "  Formatting virtual disk...\n";
            s_path2fd.clear();
            disk.format(blocks.superblock());
            icache.iput(dirs.cur_path_inode());
            dirs.set_cur_path_inode(nullptr);
            disk.install(blocks.superblock());
            std::cout << "  Format complete.  Please login again.\n";
        }

        // ============================================================
        //  halt  |  exit  |  quit
        // ============================================================
        else if (cmd == "halt" || cmd == "exit" || cmd == "quit") {
            g_core.exit();
            break;
        }

        // ============================================================
        //  unknown
        // ============================================================
        else {
            std::cout << "  Unknown command: " << cmd
                      << "\n  Type 'help' for available commands.\n";
        }
    }
}
