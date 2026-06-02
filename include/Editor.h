#pragma once

#include <string>
#include <vector>

/**
 * Editor — Built-in terminal editor for write command.
 *
 * Supports arrow-key navigation, text insertion, backspace,
 * and line management.  Call run() with optional existing content.
 */
class Editor {
public:
    /** Launch the editor.  If `existing` is non-empty it is loaded as
     *  the initial buffer (cursor at end).  Returns the final text. */
    static std::string run(const std::string& existing = "");

private:
    static int  raw_getch();
    static void redraw(const std::vector<std::string>& lines,
                       int cur_line, int cur_col);
};
