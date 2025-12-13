# Serene

minimal text editor. made it because i can.

## building

you need:
- g++ or clang++ with c++17
- ncurses

compile:
```bash
g++ -std=c++17 serene.cpp -lncurses -o serene
```

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

- `^E` - switch between browser and editor
- `^N` - new file (prompts for name)
- `^O` - previous tab
- `^P` - next tab  
- `^X` - close tab
- `^S` - save
- `^Q` - save and quit

browser shows:
- `---EDIT---` when you're editing
- `---OPEN---` when you're browsing files
- `---NEW---` when creating new file

## config

`~/.config/serene.ini`

```ini
[theme]
BackgroundC=#000a0f
ForegroundC=#ffffff
BrowserWidth=20

[keys]
NextTab=C-P
PrevTab=C-O
CloseTab=C-X
NewFile=C-N
ToggleBrowser=C-E
Save=C-S
Quit=C-Q
```

## notes

- file browser only shows non-hidden files (no dotfiles)
- saves on quit automatically
- tabs remember cursor position
- compiled binary is like 50kb

---

by izzy, with team oculine. some parts made with claude sonnet 4.5 by antropic. https://claude.ai\.
