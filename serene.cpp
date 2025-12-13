#include <ncurses.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;
// test
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

class SereneEditor {
private:
    Config config;
    std::vector<std::string> fileList;
    std::vector<Tab> tabs;
    int activeTab = 0;
    int selectedFileIdx = 0;
    bool focusBrowser = false;
    bool inputMode = false;
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
        
        // Default config
        config.keys["NextTab"] = "C-P";
        config.keys["PrevTab"] = "C-O";
        config.keys["CloseTab"] = "C-X";
        config.keys["NewFile"] = "C-N";
        config.keys["ToggleBrowser"] = "C-E";
        config.keys["Save"] = "C-S";
        config.keys["Quit"] = "C-Q";
        
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
    
    void loadFileList() {
        fileList.clear();
        for (const auto& entry : fs::directory_iterator(".")) {
            std::string fname = entry.path().filename().string();
            if (fname[0] != '.') {
                fileList.push_back(fname);
            }
        }
        std::sort(fileList.begin(), fileList.end());
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
        
        std::string tabStr = "Serene v0 | ";
        for (size_t i = 0; i < tabs.size(); i++) {
            if (i == activeTab) {
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
        
        // Context-aware header
        if (inputMode) {
            mvwprintw(browserWin, 0, 0, "---NEW---");
            mvwprintw(browserWin, 1, 1, "> %s", inputBuffer.c_str());
            
            // Show cursor in input
            wmove(browserWin, 1, 3 + inputBuffer.length());
        } else if (focusBrowser) {
            mvwprintw(browserWin, 0, 0, "---OPEN---");
        } else {
            mvwprintw(browserWin, 0, 0, "---EDIT---");
        }
        
        // Only show file list when not in input mode
        if (!inputMode) {
            int maxDisplay = screenHeight - 4;
            int startIdx = fileScrollY;
            
            for (int i = 0; i < maxDisplay && (startIdx + i) < fileList.size(); i++) {
                int idx = startIdx + i;
                
                // Highlight entire line if selected and browser is focused
                if (idx == selectedFileIdx && focusBrowser) {
                    wattron(browserWin, A_REVERSE);
                    // Fill the entire line width with spaces to create full highlight
                    for (int x = 0; x < browserWidth - 1; x++) {
                        mvwaddch(browserWin, i + 1, x, ' ');
                    }
                }
                
                std::string display = fileList[idx];
                if (display.length() > browserWidth - 3) {
                    display = display.substr(0, browserWidth - 6) + "...";
                }
                
                mvwprintw(browserWin, i + 1, 1, "%s", display.c_str());
                
                if (idx == selectedFileIdx && focusBrowser) {
                    wattroff(browserWin, A_REVERSE);
                }
            }
        }
        
        // Draw separator on the right edge
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
            mvwprintw(editorWin, 0, 0, "No file open. Press C-N for new file or select from browser.");
            wrefresh(editorWin);
            return;
        }
        
        Tab& tab = tabs[activeTab];
        int maxDisplay = screenHeight - 3;
        
        for (int i = 0; i < maxDisplay && (scrollY + i) < tab.lines.size(); i++) {
            mvwprintw(editorWin, i, 1, "%s", tab.lines[scrollY + i].c_str());
        }
        
        wrefresh(editorWin);
    }
    
    void updateCursor() {
        if (tabs.empty() || focusBrowser) {
            curs_set(0); // Hide cursor
            return;
        }
        
        curs_set(1); // Show cursor
        Tab& tab = tabs[activeTab];
        int maxDisplay = screenHeight - 3;
        
        // Position cursor in editor window
        if (tab.cursorY >= scrollY && tab.cursorY < scrollY + maxDisplay) {
            wmove(editorWin, tab.cursorY - scrollY, tab.cursorX + 1);
            wrefresh(editorWin);
        }
    }
    
    void drawStatus() {
        werase(statusWin);
        
        std::string status = "C-E:switch | C-O/P:tabs | C-X:close | C-N:new | C-S:save | C-Q:save+quit";
        if (!tabs.empty()) {
            status += " | " + tabs[activeTab].filename;
            status += " [" + std::to_string(tabs[activeTab].cursorY + 1) + ":" + 
                      std::to_string(tabs[activeTab].cursorX + 1) + "]";
        }
        
        mvwprintw(statusWin, 0, 0, "%s", status.c_str());
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
        if (inputMode) {
            // Handle input mode
            if (ch == '\n' || ch == KEY_ENTER) {
                if (!inputBuffer.empty()) {
                    // Create file with touch
                    std::ofstream newFile(inputBuffer);
                    newFile.close();
                    
                    // Reload file list to include new file
                    loadFileList();
                    
                    // Open the new file
                    openFile(inputBuffer);
                    
                    // Exit input mode
                    inputMode = false;
                    inputBuffer.clear();
                    focusBrowser = false;
                }
            } else if (ch == 27) { // ESC
                inputMode = false;
                inputBuffer.clear();
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!inputBuffer.empty()) {
                    inputBuffer.pop_back();
                }
            } else if (ch >= 32 && ch < 127) {
                inputBuffer += (char)ch;
            }
        } else {
            // Normal browser navigation
            switch (ch) {
                case KEY_UP:
                    if (selectedFileIdx > 0) {
                        selectedFileIdx--;
                        if (selectedFileIdx < fileScrollY) {
                            fileScrollY = selectedFileIdx;
                        }
                    }
                    break;
                case KEY_DOWN:
                    if (selectedFileIdx < fileList.size() - 1) {
                        selectedFileIdx++;
                        int maxDisplay = screenHeight - 4;
                        if (selectedFileIdx >= fileScrollY + maxDisplay) {
                            fileScrollY = selectedFileIdx - maxDisplay + 1;
                        }
                    }
                    break;
                case '\n':
                case KEY_ENTER:
                    if (selectedFileIdx < fileList.size()) {
                        openFile(fileList[selectedFileIdx]);
                        focusBrowser = false;
                    }
                    break;
            }
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
                if (tab.cursorY < tab.lines.size() - 1) {
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
                if (tab.cursorX < tab.lines[tab.cursorY].length()) tab.cursorX++;
                break;
            case KEY_BACKSPACE:
            case 127:
                if (tab.cursorX > 0) {
                    tab.lines[tab.cursorY].erase(tab.cursorX - 1, 1);
                    tab.cursorX--;
                    tab.modified = true;
                } else if (tab.cursorY > 0) {
                    tab.cursorX = tab.lines[tab.cursorY - 1].length();
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
        loadFileList();
        
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(1);
        
        getmaxyx(stdscr, screenHeight, screenWidth);
        browserWidth = config.browserWidth;
        
        // Create windows
        tabWin = newwin(1, screenWidth, 0, 0);
        browserWin = newwin(screenHeight - 2, browserWidth, 1, 0);
        editorWin = newwin(screenHeight - 2, screenWidth - browserWidth, 1, browserWidth);
        statusWin = newwin(1, screenWidth, screenHeight - 1, 0);
        
        keypad(browserWin, TRUE);
        keypad(editorWin, TRUE);
    }
    
    ~SereneEditor() {
        delwin(browserWin);
        delwin(editorWin);
        delwin(tabWin);
        delwin(statusWin);
        endwin();
    }
    
    void openFile(const std::string& filename) {
        // Check if already open
        for (size_t i = 0; i < tabs.size(); i++) {
            if (tabs[i].filename == filename) {
                activeTab = i;
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
        activeTab = tabs.size() - 1;
    }
    
    void run() {
        bool running = true;
        
        while (running) {
            render();
            
            int ch = focusBrowser ? wgetch(browserWin) : wgetch(editorWin);
            
            // Handle control keys
            if (ch == getCtrlKey('e')) {
                focusBrowser = !focusBrowser;
            } else if (ch == getCtrlKey('p')) {
                if (!tabs.empty()) {
                    activeTab = (activeTab + 1) % tabs.size();
                    scrollY = 0;
                }
            } else if (ch == getCtrlKey('o')) {
                if (!tabs.empty()) {
                    activeTab = (activeTab - 1 + tabs.size()) % tabs.size();
                    scrollY = 0;
                }
            } else if (ch == getCtrlKey('x')) {
                if (!tabs.empty()) {
                    tabs.erase(tabs.begin() + activeTab);
                    if (activeTab >= tabs.size() && !tabs.empty()) {
                        activeTab = tabs.size() - 1;
                    }
                    scrollY = 0;
                }
            } else if (ch == getCtrlKey('n')) {
                // Enter new file creation mode
                inputMode = true;
                focusBrowser = true;
                inputBuffer.clear();
            } else if (ch == getCtrlKey('s')) {
                saveCurrentFile();
            } else if (ch == getCtrlKey('q')) {
                saveCurrentFile();
                running = false;
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
    
    // Open files passed as arguments
    for (int i = 1; i < argc; i++) {
        editor.openFile(argv[i]);
    }
    
    editor.run();
    return 0;
}

// Compile with: g++ -std=c++17 serene.cpp -lncurses -o serene
// Run with: ./serene [file1] [file2] ...
