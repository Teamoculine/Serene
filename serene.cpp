#include <ncurses.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

// Convert hex color string (e.g. "1a2b3c") to ncurses 0-1000 range
static short hexToNcurses(const std::string& hex, char channel) {
    std::string h = hex;
    if (h.size() == 3) {
        // expand shorthand
        h = {h[0],h[0],h[1],h[1],h[2],h[2]};
    }
    if (h.size() != 6) return 0;
    int offset = 0;
    if (channel == 'r') offset = 0;
    else if (channel == 'g') offset = 2;
    else offset = 4;
    int val = std::stoi(h.substr(offset, 2), nullptr, 16);
    return (short)(val * 1000 / 255);
}

struct Config {
    std::string bgColor = "000a0f";
    std::string fgColor = "ffffff";
    int browserWidth = 20;
    std::map<std::string, std::string> keys;
};

struct Tab {
    std::string filename;
    std::vector<std::string> lines;
    int cursorX = 0;
    int cursorY = 0;
    bool modified = false;
};

struct FileEntry {
    std::string name;
    std::string fullPath;
    bool isDir;
    int depth;
    bool expanded;
};

enum class EditorMode {
    EDIT,
    COMMAND,
    INPUT
};

class SereneEditor {
private:
    Config config;
    std::vector<FileEntry> allEntries;
    std::vector<FileEntry> visibleEntries;
    std::vector<Tab> tabs;
    int activeTab = 0;
    int selectedEntryIdx = 0;
    bool focusBrowser = false;
    bool showHidden = false;
    EditorMode mode = EditorMode::EDIT;
    bool waitingForCommand = false;
    std::string inputBuffer;
    int screenHeight, screenWidth;
    int browserWidth;
    int scrollY = 0;
    int fileScrollY = 0;

    WINDOW* browserWin;
    WINDOW* editorWin;
    WINDOW* tabWin;
    WINDOW* statusWin;

    void loadConfig() {
        std::string configPath = std::string(getenv("HOME")) + "/.config/serene.ini";
        std::ifstream file(configPath);

        config.keys["ToggleBrowser"] = "C-E";

        if (!file.is_open()) return;

        std::string line, section;
        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);

            if (line.empty() || line[0] == ';') continue;

            if (line[0] == '[') {
                section = line.substr(1, line.find(']') - 1);
            } else if (line.find('=') != std::string::npos) {
                size_t pos = line.find('=');
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);

                key.erase(key.find_last_not_of(" \t") + 1);
                val.erase(0, val.find_first_not_of(" \t"));

                if (section == "theme") {
                    if (key == "BackgroundC") config.bgColor = val;
                    else if (key == "ForegroundC") config.fgColor = val;
                    else if (key == "BrowserWidth") config.browserWidth = std::stoi(val);
                } else if (section == "keys") {
                    config.keys[key] = val;
                }
            }
        }
    }

    // Apply config colors to ncurses. Call after initscr() and start_color().
    void applyColors() {
        if (!has_colors() || !can_change_color()) return;

        short bg_r = hexToNcurses(config.bgColor, 'r');
        short bg_g = hexToNcurses(config.bgColor, 'g');
        short bg_b = hexToNcurses(config.bgColor, 'b');

        short fg_r = hexToNcurses(config.fgColor, 'r');
        short fg_g = hexToNcurses(config.fgColor, 'g');
        short fg_b = hexToNcurses(config.fgColor, 'b');

        // COLOR_BLACK (0) = bg, COLOR_WHITE (7) = fg
        init_color(COLOR_BLACK, bg_r, bg_g, bg_b);
        init_color(COLOR_WHITE, fg_r, fg_g, fg_b);

        // Pair 1: normal text (fg on bg)
        init_pair(1, COLOR_WHITE, COLOR_BLACK);

        // Apply globally
        bkgd(COLOR_PAIR(1));
        wbkgd(stdscr, COLOR_PAIR(1));
    }

    void applyWindowColors() {
        // Called after windows are created
        wbkgd(tabWin,     COLOR_PAIR(1));
        wbkgd(browserWin, COLOR_PAIR(1));
        wbkgd(editorWin,  COLOR_PAIR(1));
        wbkgd(statusWin,  COLOR_PAIR(1));
    }

    // Loads only the top-level entries of a directory into allEntries.
    // Expansion of subdirectories is handled by rebuildVisibleEntries.
    void loadDirectoryEntries(const std::string& path, int depth) {
        std::vector<FileEntry> entries;

        try {
            for (const auto& entry : fs::directory_iterator(path)) {
                std::string fname = entry.path().filename().string();
                if (fname[0] == '.' && !showHidden) continue;

                FileEntry fe;
                fe.name = fname;
                fe.fullPath = entry.path().string();
                fe.isDir = fs::is_directory(entry);
                fe.depth = depth;
                fe.expanded = false;

                entries.push_back(fe);
            }
        } catch (...) {
            return;
        }

        std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.isDir != b.isDir) return a.isDir > b.isDir;
            return a.name < b.name;
        });

        allEntries.insert(allEntries.end(), entries.begin(), entries.end());
    }

    void rebuildVisibleEntries() {
        visibleEntries.clear();

        for (auto& entry : allEntries) {
            visibleEntries.push_back(entry);

            if (entry.isDir && entry.expanded) {
                std::vector<FileEntry> children;

                try {
                    for (const auto& child : fs::directory_iterator(entry.fullPath)) {
                        std::string fname = child.path().filename().string();
                        if (fname[0] == '.' && !showHidden) continue;

                        FileEntry fe;
                        fe.name = fname;
                        fe.fullPath = child.path().string();
                        fe.isDir = fs::is_directory(child);
                        fe.depth = entry.depth + 1;
                        fe.expanded = false;

                        children.push_back(fe);
                    }
                } catch (...) {
                    continue;
                }

                std::sort(children.begin(), children.end(), [](const FileEntry& a, const FileEntry& b) {
                    if (a.isDir != b.isDir) return a.isDir > b.isDir;
                    return a.name < b.name;
                });

                visibleEntries.insert(visibleEntries.end(), children.begin(), children.end());
            }
        }
    }

    void loadFileTree() {
        allEntries.clear();
        loadDirectoryEntries(".", 0);
        rebuildVisibleEntries();
    }

    void saveCurrentFile() {
        if (tabs.empty()) return;

        Tab& tab = tabs[activeTab];
        std::ofstream file(tab.filename);

        if (file.is_open()) {
            for (const auto& line : tab.lines) {
                file << line << "\n";
            }
            tab.modified = false;
        }
    }

    void drawTabs() {
        werase(tabWin);

        std::string tabStr = "Serene v1 | ";
        for (size_t i = 0; i < tabs.size(); i++) {
            if ((int)i == activeTab) {
                tabStr += "[" + tabs[i].filename + "] ";
            } else {
                tabStr += tabs[i].filename + " ";
            }
        }

        mvwprintw(tabWin, 0, 0, "%s", tabStr.c_str());
        wrefresh(tabWin);
    }

    void drawBrowser() {
        werase(browserWin);

        if (focusBrowser) {
            wattron(browserWin, A_BOLD);
        }

        std::string header = focusBrowser ? "---OPEN---" : "---EDIT---";
        if (showHidden) header += " [H]";
        mvwprintw(browserWin, 0, 0, "%s", header.c_str());

        int maxDisplay = screenHeight - 4;
        int startIdx = fileScrollY;

        for (int i = 0; i < maxDisplay && (startIdx + i) < (int)visibleEntries.size(); i++) {
            int idx = startIdx + i;
            const FileEntry& entry = visibleEntries[idx];

            if (idx == selectedEntryIdx && focusBrowser) {
                wattron(browserWin, A_REVERSE);
                for (int x = 0; x < browserWidth - 1; x++) {
                    mvwaddch(browserWin, i + 1, x, ' ');
                }
            }

            std::string display;
            for (int d = 0; d < entry.depth; d++) {
                display += "| ";
            }

            display += entry.name;
            if (entry.isDir) {
                display += "/";
            }

            if ((int)display.length() > browserWidth - 3) {
                display = display.substr(0, browserWidth - 6) + "...";
            }

            mvwprintw(browserWin, i + 1, 1, "%s", display.c_str());

            if (idx == selectedEntryIdx && focusBrowser) {
                wattroff(browserWin, A_REVERSE);
            }
        }

        for (int i = 0; i < screenHeight - 2; i++) {
            mvwaddch(browserWin, i, browserWidth - 1, ACS_VLINE);
        }

        if (focusBrowser) {
            wattroff(browserWin, A_BOLD);
        }

        wrefresh(browserWin);
    }

    void drawEditor() {
        werase(editorWin);

        if (tabs.empty()) {
            mvwprintw(editorWin, 0, 0, "Serene v1 - No file open");
            mvwprintw(editorWin, 1, 0, "ESC !n - new file | C-E - browse files | ESC !q - quit");
            wrefresh(editorWin);
            return;
        }

        Tab& tab = tabs[activeTab];
        int maxDisplay = screenHeight - 3;

        for (int i = 0; i < maxDisplay && (scrollY + i) < (int)tab.lines.size(); i++) {
            mvwprintw(editorWin, i, 1, "%s", tab.lines[scrollY + i].c_str());
        }

        wrefresh(editorWin);
    }

    void updateCursor() {
        if (tabs.empty() || focusBrowser || mode == EditorMode::COMMAND) {
            curs_set(0);
            return;
        }

        if (mode == EditorMode::INPUT) {
            curs_set(1);
            return;
        }

        curs_set(1);
        Tab& tab = tabs[activeTab];
        int maxDisplay = screenHeight - 3;

        if (tab.cursorY >= scrollY && tab.cursorY < scrollY + maxDisplay) {
            wmove(editorWin, tab.cursorY - scrollY, tab.cursorX + 1);
            wrefresh(editorWin);
        }
    }

    void drawStatus() {
        werase(statusWin);

        if (mode == EditorMode::INPUT) {
            mvwprintw(statusWin, 0, 0, "> New file: %s", inputBuffer.c_str());
            curs_set(1);
            wmove(statusWin, 0, 13 + (int)inputBuffer.length());
        } else if (mode == EditorMode::COMMAND) {
            if (waitingForCommand) {
                mvwprintw(statusWin, 0, 0, "> !");
            } else {
                mvwprintw(statusWin, 0, 0, "> ");
            }
            curs_set(0);
        } else {
            std::string status = "ESC:cmd | C-E:browse";
            if (!tabs.empty()) {
                status = tabs[activeTab].filename;
                status += " [" + std::to_string(tabs[activeTab].cursorY + 1) + ":" +
                          std::to_string(tabs[activeTab].cursorX + 1) + "]";
                if (tabs[activeTab].modified) status += " *";
                status += " | ESC:cmd | C-E:browse";
            }

            mvwprintw(statusWin, 0, 0, "%s", status.c_str());
        }

        wrefresh(statusWin);
    }

    void render() {
        drawTabs();
        drawBrowser();
        drawEditor();
        drawStatus();
        updateCursor();
    }

    int getCtrlKey(char c) {
        return c & 0x1f;
    }

    void handleBrowserInput(int ch) {
        if (ch == 'h' || ch == 'H') {
            showHidden = !showHidden;
            loadFileTree();
            return;
        }

        switch (ch) {
            case KEY_UP:
                if (selectedEntryIdx > 0) {
                    selectedEntryIdx--;
                    if (selectedEntryIdx < fileScrollY) {
                        fileScrollY = selectedEntryIdx;
                    }
                }
                break;
            case KEY_DOWN:
                if (selectedEntryIdx < (int)visibleEntries.size() - 1) {
                    selectedEntryIdx++;
                    int maxDisplay = screenHeight - 4;
                    if (selectedEntryIdx >= fileScrollY + maxDisplay) {
                        fileScrollY = selectedEntryIdx - maxDisplay + 1;
                    }
                }
                break;
            case '\n':
            case KEY_ENTER:
                if (selectedEntryIdx < (int)visibleEntries.size()) {
                    FileEntry& entry = visibleEntries[selectedEntryIdx];

                    if (entry.isDir) {
                        for (auto& e : allEntries) {
                            if (e.fullPath == entry.fullPath) {
                                e.expanded = !e.expanded;
                                break;
                            }
                        }
                        rebuildVisibleEntries();
                    } else {
                        openFile(entry.fullPath);
                        focusBrowser = false;
                    }
                }
                break;
        }
    }

    void handleInputMode(int ch) {
        if (ch == '\n' || ch == KEY_ENTER) {
            if (!inputBuffer.empty()) {
                std::ofstream newFile(inputBuffer);
                newFile.close();

                loadFileTree();
                openFile(inputBuffer);

                mode = EditorMode::EDIT;
                inputBuffer.clear();
            }
        } else if (ch == 27) {
            mode = EditorMode::EDIT;
            inputBuffer.clear();
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            if (!inputBuffer.empty()) {
                inputBuffer.pop_back();
            }
        } else if (ch >= 32 && ch < 127) {
            inputBuffer += (char)ch;
        }
    }

    void executeCommand(char cmd) {
        switch (cmd) {
            case 's':
                saveCurrentFile();
                break;
            case 'q':
                saveCurrentFile();
                endwin();
                exit(0);
                break;
            case 'n':
                mode = EditorMode::INPUT;
                waitingForCommand = false;
                inputBuffer.clear();
                return;
            case 'x':
                if (!tabs.empty()) {
                    tabs.erase(tabs.begin() + activeTab);
                    // Clamp activeTab to valid range; if no tabs remain, stay at 0
                    if (!tabs.empty()) {
                        if (activeTab >= (int)tabs.size()) {
                            activeTab = (int)tabs.size() - 1;
                        }
                    } else {
                        activeTab = 0;
                    }
                    scrollY = 0;
                }
                break;
            case 'p':
                if (!tabs.empty()) {
                    activeTab = (activeTab + 1) % (int)tabs.size();
                    scrollY = 0;
                }
                break;
            case 'o':
                if (!tabs.empty()) {
                    activeTab = (activeTab - 1 + (int)tabs.size()) % (int)tabs.size();
                    scrollY = 0;
                }
                break;
        }
        waitingForCommand = false;
    }

    void handleCommandMode(int ch) {
        if (ch == '!') {
            waitingForCommand = true;
        } else if (waitingForCommand) {
            executeCommand(ch);
        }
    }

    void handleEditorInput(int ch) {
        if (tabs.empty()) return;

        Tab& tab = tabs[activeTab];

        switch (ch) {
            case KEY_UP:
                if (tab.cursorY > 0) {
                    tab.cursorY--;
                    tab.cursorX = std::min(tab.cursorX, (int)tab.lines[tab.cursorY].length());
                    if (tab.cursorY < scrollY) scrollY = tab.cursorY;
                }
                break;
            case KEY_DOWN:
                if (tab.cursorY < (int)tab.lines.size() - 1) {
                    tab.cursorY++;
                    tab.cursorX = std::min(tab.cursorX, (int)tab.lines[tab.cursorY].length());
                    int maxDisplay = screenHeight - 3;
                    if (tab.cursorY >= scrollY + maxDisplay) scrollY = tab.cursorY - maxDisplay + 1;
                }
                break;
            case KEY_LEFT:
                if (tab.cursorX > 0) tab.cursorX--;
                break;
            case KEY_RIGHT:
                if (tab.cursorX < (int)tab.lines[tab.cursorY].length()) tab.cursorX++;
                break;
            case KEY_BACKSPACE:
            case 127:
                if (tab.cursorX > 0) {
                    tab.lines[tab.cursorY].erase(tab.cursorX - 1, 1);
                    tab.cursorX--;
                    tab.modified = true;
                } else if (tab.cursorY > 0) {
                    tab.cursorX = (int)tab.lines[tab.cursorY - 1].length();
                    tab.lines[tab.cursorY - 1] += tab.lines[tab.cursorY];
                    tab.lines.erase(tab.lines.begin() + tab.cursorY);
                    tab.cursorY--;
                    tab.modified = true;
                }
                break;
            case '\n':
            case KEY_ENTER:
                {
                    std::string rest = tab.lines[tab.cursorY].substr(tab.cursorX);
                    tab.lines[tab.cursorY] = tab.lines[tab.cursorY].substr(0, tab.cursorX);
                    tab.lines.insert(tab.lines.begin() + tab.cursorY + 1, rest);
                    tab.cursorY++;
                    tab.cursorX = 0;
                    tab.modified = true;
                }
                break;
            default:
                if (ch >= 32 && ch < 127) {
                    tab.lines[tab.cursorY].insert(tab.cursorX, 1, ch);
                    tab.cursorX++;
                    tab.modified = true;
                }
                break;
        }
    }

public:
    SereneEditor() {
        loadConfig();
        loadFileTree();

        initscr();
        set_escdelay(25);
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(1);

        // Color init — must happen after initscr()
        if (has_colors()) {
            start_color();
            applyColors();
        }

        getmaxyx(stdscr, screenHeight, screenWidth);
        browserWidth = config.browserWidth;

        tabWin    = newwin(1, screenWidth, 0, 0);
        browserWin = newwin(screenHeight - 2, browserWidth, 1, 0);
        editorWin  = newwin(screenHeight - 2, screenWidth - browserWidth, 1, browserWidth);
        statusWin  = newwin(1, screenWidth, screenHeight - 1, 0);

        keypad(browserWin, TRUE);
        keypad(editorWin, TRUE);
        keypad(statusWin, TRUE);

        if (has_colors()) {
            applyWindowColors();
        }
    }

    ~SereneEditor() {
        delwin(browserWin);
        delwin(editorWin);
        delwin(tabWin);
        delwin(statusWin);
        endwin();
    }

    void openFile(const std::string& filename) {
        for (size_t i = 0; i < tabs.size(); i++) {
            if (tabs[i].filename == filename) {
                activeTab = (int)i;
                return;
            }
        }

        Tab tab;
        tab.filename = filename;

        std::ifstream file(filename);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                tab.lines.push_back(line);
            }
        }

        if (tab.lines.empty()) {
            tab.lines.push_back("");
        }

        tabs.push_back(tab);
        activeTab = (int)tabs.size() - 1;
    }

    void run() {
        while (true) {
            render();

            int ch;

            if (mode == EditorMode::INPUT) {
                ch = wgetch(statusWin);
                handleInputMode(ch);
                continue;
            }

            if (focusBrowser) {
                ch = wgetch(browserWin);
            } else {
                ch = wgetch(editorWin);
            }

            // Global keys
            if (ch == 27) { // ESC
                if (mode == EditorMode::COMMAND) {
                    mode = EditorMode::EDIT;
                    waitingForCommand = false;
                } else {
                    mode = EditorMode::COMMAND;
                    waitingForCommand = false;
                }
                continue;
            }

            if (ch == getCtrlKey('e')) {
                focusBrowser = !focusBrowser;
                continue;
            }

            // Mode-specific handling
            if (mode == EditorMode::COMMAND) {
                handleCommandMode(ch);
            } else if (focusBrowser) {
                handleBrowserInput(ch);
            } else {
                handleEditorInput(ch);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    SereneEditor editor;

    for (int i = 1; i < argc; i++) {
        editor.openFile(argv[i]);
    }

    editor.run();
    return 0;
}

// Serene v1
// Compile: g++ -std=c++17 serene.cpp -lncurses -o serene
// Usage: ./serene [files...]
