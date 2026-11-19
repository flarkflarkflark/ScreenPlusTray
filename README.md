# ScreenPlusTray (`screenplus-tray`)

A lightweight, standalone Linux system tray icon (`org.kde.StatusNotifierItem` + `com.canonical.dbusmenu`) for [Screen+](https://github.com/CoolNinja2009/ScreenPlus) on **KDE Plasma 6** and **Hyprland** (Waybar `tray` module).

This is an unofficial companion tool. Not affiliated with or endorsed by the Screen+ project.

`screenplus-tray` communicates directly with an already-installed Screen+ service over its Unix domain socket (`$XDG_RUNTIME_DIR/screenplus.sock`) without modifying Screen+ itself.

## Features

- **Zero heavy dependencies**: pure C17 using only `libc` and `libdbus-1` (no GTK, Qt, SDL, libappindicator, or libpng).
- **Tray icon, rich tooltip & OSD feedback**:
  - Active, paused, and stopped (greyed-out) icon variants.
  - Tooltip displays the current wallpaper name, playback status (`Playing` / `Paused` / `Not running`), current volume percentage, and a quick reference of mouse interactions.
  - **Left click**: toggles the Screen+ window (`show` / `hide`).
  - **Middle click**: pauses or resumes wallpapers (`toggle`) and shows an on-screen display (OSD) indicator.
  - **Mouse wheel / touchpad scroll**: adjusts wallpaper volume in $\pm 5\%$ steps per 120 scroll units (clamped to `0..100%`), updates the tooltip and volume OSD immediately on every step, and debounces the `volume N` command to the service (150 ms).
  - **On-Screen Display (OSD)**: uses KDE Plasma's native `org.kde.osdService` (`mediaPlayerVolumeChanged` and `showText`) when `org.kde.plasmashell` is running, and falls back to `org.freedesktop.Notifications` (with synchronous replacement and progress gauge hint for `mako`, `dunst`, or `swaync` on Hyprland).
- **Full context menu**:
  - Open/hide the Screen+ window.
  - Pause/resume wallpapers, skip to next wallpaper, or switch directly among up to 20 library wallpapers (plus the active wallpaper if outside the top 20).
  - Toggle favourite status on the current wallpaper.
  - Toggle wallpaper sound and choose preset volume levels (`0%`, `25%`, `50%`, `75%`, `100%`).
  - Toggle slideshow, interval (`5`, `15`, `30`, `60` minutes), shuffle, and source (`All`, `Favourites`, or any user collection).
  - Toggle day/night schedule and switch display mode (`Independent`, `Duplicate`, `Span`).
  - Toggle start at login, restart wallpapers, or stop/start the Screen+ service while the tray icon stays active.
- **Resilient & quiet**:
  - Polls only `status` every 2 seconds (which Screen+ does not log at `INFO` level) and emits D-Bus signals (`NewIcon`, `NewToolTip`, `LayoutUpdated`) **only** when state actually changes.
  - Automatically re-registers with `org.kde.StatusNotifierWatcher` whenever the watcher (re)appears on the session bus (e.g., when Waybar starts or restarts on Hyprland).
  - Single-instance lock via `org.screenplus.Tray`.

## Requirements

- A working installation of **Screen+ for Linux** (`screenplus` on `PATH`)
- CMake $\ge 3.16$, Ninja, a C17 compiler (`gcc` or `clang`)
- `pkg-config` and `libdbus-1` development headers (`libdbus-1-dev` on Debian/Ubuntu, `dbus-devel` on Fedora, `dbus` on Arch)

## Build & Install

By default, `CMAKE_INSTALL_PREFIX` defaults to `~/.local`:

```bash
cmake -S . -B build -G Ninja
cmake --build build
cmake --install build
```

To install to a custom prefix (such as `/usr/local`):

```bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

## Usage

```bash
# Start the tray icon
screenplus-tray

# Enable or disable starting screenplus-tray automatically at login
# (writes or removes ~/.config/autostart/screenplus-tray.desktop)
screenplus-tray --autostart on
screenplus-tray --autostart off

# Print the current parsed Screen+ state and exit
screenplus-tray --dump
```

On **Hyprland**, you can also launch `screenplus-tray` from `hyprland.conf`:

```ini
exec-once = screenplus-tray
```

## Known Limitations

- **Window toggle state after closing with the window's `X` button**: The Screen+ service does not expose whether its management window is currently open in `status` or `settings`. `screenplus-tray` tracks window state locally (`show` $\rightarrow$ open, `hide` $\rightarrow$ closed, reset whenever the service stops or restarts). If you close the Screen+ window using its own window close (`X`) button instead of clicking the tray icon, the next left-click on the tray icon sends a harmless `hide` command to synchronise state, and the click after that opens the window again.

## License

MIT License — see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
