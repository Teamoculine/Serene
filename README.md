# Serene

minimal text editor. made it because i can.

**version 1**

## building

you need:
- g++ or clang++ with c++17
- ncurses

compile:
```bash
g++ -std=c++17 serene.cpp -lncurses -o serene
```

optionally, disable flow control if you want C-S/C-Q to work in other apps:
```bash
stty -ixon
```
(serene doesn't need this since it uses ESC commands)

install wherever:
```bash
cp serene ~/bin/
# or
sudo cp serene /usr/local/bin/
```

## usage

```bash
./serene                    # open in current directory
./serene file.txt           # open file
./serene file1 file2 file3  # open multiple as tabs
```

## keybinds

### global
- `ESC` - toggle command mode
- `^E` - switch between browser and editor

### command mode (ESC then !)
- `!s` - save file
- `!q` - save and quit
- `!n` - new file (prompts for name)
- `!x` - close current tab
- `!p` - next tab
- `!o` - previous tab

### file browser
- arrow keys - navigate
- enter - open file or expand/collapse directory
- `h` or `H` - toggle hidden files visibility

### editor
- arrow keys - move cursor
- normal typing works
- enter - new line
- backspace - delete

## features

- **tree view** - hierarchical directory browser with expandable folders
- **tabs** - open multiple files, switch between them
- **auto-save on quit** - never lose work
- **hidden files toggle** - press H in browser to show/hide dotfiles
- **modified indicator** - `*` shows unsaved changes
- **configurable** - edit `~/.config/serene.ini`

browser modes:
- `---EDIT---` - you're editing a file
- `---OPEN---` - browsing files (focus with ^E)
- `[H]` - hidden files visible

## config

`~/.config/serene.ini`

```ini
[theme]
BackgroundC=#000a0f
ForegroundC=#ffffff
BrowserWidth=20

[keys]
ToggleBrowser=C-E
```

## notes

- directories shown with `/` suffix
- depth shown with `|` prefix
- folders sorted before files, both alphabetical
- file browser skips dotfiles by default (toggle with H)
- ESC delay set to 25ms for instant response
- compiled binary is ~50kb

## why

vim's command mode without vim's bullshit.

---

by oculine. design by izzy. some elements made with claude sonnet 4.5 by anthropic. https://claude.ai
