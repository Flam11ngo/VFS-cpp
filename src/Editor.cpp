/**
 * Editor.cpp — Terminal-based text editor
 *
 * Uses platform-specific raw input to capture arrow keys.
 * Windows: _getch() from <conio.h>
 * Unix:    termios raw mode
 */
#include "Editor.h"

#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

// ====== Platform raw input ======

int Editor::raw_getch()
{
#ifdef _WIN32
    return _getch();
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

// ====== Screen redraw ======

void Editor::redraw(const std::vector<std::string>& lines,
                    int cur_line, int cur_col)
{
    std::cout << "\033[2J\033[H";   // clear screen, cursor home (works on Win 10+ too)
    std::cout << "  === EDITOR  Esc=finish  arrows=navigate  Ctrl+D=quit ===\n";

    for (size_t i = 0; i < lines.size(); i++) {
        if ((int)i == cur_line)
            std::cout << "  \033[7m" << (i + 1) << "\033[0m| " << lines[i] << "\n";
        else
            std::cout << "  " << (i + 1) << "| " << lines[i] << "\n";
    }
    // Status line
    std::cout << "\n  line " << (cur_line + 1) << "/" << lines.size()
              << "  col " << (cur_col + 1) << "  \n";

    // Move cursor to current line at correct column
    // Row (1-based) = header(1) + cur_line + 1
    // Col (1-based) = "  N| " prefix + cur_col
    int prefix_w = (cur_line + 1 >= 10) ? 5 : 4;
    std::cout << "\033[" << (cur_line + 2) << ";" << (prefix_w + cur_col + 1) << "H";
    std::cout.flush();
}

// ====== Main loop ======

std::string Editor::run(const std::string& existing)
{
    std::vector<std::string> lines;
    if (!existing.empty()) {
        std::istringstream iss(existing);
        std::string l;
        while (std::getline(iss, l))
            lines.push_back(l);
    }
    if (lines.empty())
        lines.push_back("");

    // Start cursor at end (append-friendly)
    int cur_line = (int)lines.size() - 1;
    int cur_col  = (int)lines[cur_line].size();
    bool running = true;

    redraw(lines, cur_line, cur_col);

    while (running) {
        int ch = raw_getch();

        // Windows arrow-key prefix
        if (ch == 0 || ch == 224) {
            ch = raw_getch();
            switch (ch) {
            case 72: if (cur_line > 0) { cur_line--; cur_col = (int)lines[cur_line].size(); } break;
            case 80: if (cur_line < (int)lines.size() - 1) { cur_line++; cur_col = (int)lines[cur_line].size(); } break;
            case 75: if (cur_col > 0) cur_col--; break;
            case 77: if (cur_col < (int)lines[cur_line].size()) cur_col++; break;
            }
        }
        // Unix arrow-key sequence  Esc [ X
        else if (ch == 27) {
            ch = raw_getch();
            if (ch == '[') {
                ch = raw_getch();
                switch (ch) {
                case 'A': if (cur_line > 0) { cur_line--; cur_col = (int)lines[cur_line].size(); } break;
                case 'B': if (cur_line < (int)lines.size() - 1) { cur_line++; cur_col = (int)lines[cur_line].size(); } break;
                case 'C': if (cur_col < (int)lines[cur_line].size()) cur_col++; break;
                case 'D': if (cur_col > 0) cur_col--; break;
                }
            } else {
                running = false;   // plain Esc = finish
            }
        }
        // Ctrl+D — finish
        else if (ch == 4) {
            running = false;
        }
        // Enter — split line
        else if (ch == '\r' || ch == '\n') {
            std::string rest = lines[cur_line].substr(cur_col);
            lines[cur_line] = lines[cur_line].substr(0, cur_col);
            lines.insert(lines.begin() + cur_line + 1, rest);
            cur_line++;
            cur_col = 0;
        }
        // Backspace
        else if (ch == 8 || ch == 127) {
            if (cur_col > 0) {
                lines[cur_line].erase(cur_col - 1, 1);
                cur_col--;
            } else if (cur_line > 0) {
                cur_col = (int)lines[cur_line - 1].size();
                lines[cur_line - 1] += lines[cur_line];
                lines.erase(lines.begin() + cur_line);
                cur_line--;
            }
        }
        // Printable character
        else if (ch >= 32 && ch < 127) {
            lines[cur_line].insert(cur_col, 1, (char)ch);
            cur_col++;
        }

        redraw(lines, cur_line, cur_col);
    }

    // Join lines
    std::string result;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) result += '\n';
        result += lines[i];
    }
    return result;
}
