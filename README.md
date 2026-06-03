# Sleeker

A lightweight desktop overlay for Windows that lets you organize shortcuts and files into sleek, dockable panels that sit between your wallpaper and desktop icons.

Built with **Qt 6** and **C++23**.

![Windows](https://img.shields.io/badge/platform-Windows-blue)

## Demo

### Clean desktop — panels hidden or docked
> Panels tuck away to the screen edge, leaving your desktop uncluttered.

![Clean desktop](screenshots/desktop-clean.png)

### Panel slides in on hover
> Hover the edge to reveal your shortcuts. Here the **Games** panel is docked to the top.

![Panel open](screenshots/panel-open.png)

## Features

- **Desktop-embedded panels** — panels live on your desktop, behind icons but above the wallpaper
- **Dock to any edge** — snap panels to the left, right, top, or bottom of any monitor with smooth slide-in/out animations
- **Multi-monitor support** — panels dock to the correct screen and don't interfere across monitors
- **Global hotkey** — `Win+`` ` toggles all panels visible/hidden instantly
- **Per-panel customization** — font, font size, icon size (24-72px), and background opacity per panel
- **Search / filter** — built-in filter bar for panels with many items (auto-shows at 8+ items, or toggle via right-click)
- **Drag & drop** — drag files into panels from Explorer, or drag items out to apps like Discord
- **Reorder items** — drag items within a panel to rearrange them
- **Watch folders** — panels can mirror a directory, auto-populating with its contents
- **Native context menus** — right-click items to get the full Windows shell context menu (Open with, Run as admin, Send to, etc.)
- **Edge detection** — docked panels open even when another window (e.g. Chrome) covers the screen edge
- **Single instance** — launching a second copy asks whether to close the first
- **Start with Windows** — optional auto-start via the panel settings dialog
- **Auto-save** — panel layout, positions, and settings persist automatically

## Building

### Requirements

- **Qt 6.x** (static or shared)
- **MSVC** (Visual Studio 2022+ recommended)
- **CMake 3.16+**

### Build steps

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Or open the folder in Visual Studio — it picks up `CMakeSettings.json` automatically.

## Usage

1. Run `Sleeker.exe` — panels appear on your desktop
2. **Right-click** the desktop background to create panels or quit
3. **Right-click** a panel for options: rename, dock, collapse, filter, remove
4. **Double-click** the title bar to rename a panel
5. **Drag files** onto a panel to add shortcuts
6. **`Win+`` `** to hide/show all panels
7. **System tray** icon provides quick access to toggle and quit

## License

MIT
