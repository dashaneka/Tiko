# Tiko

A tiny, distraction-free TikTok viewer for GNOME designed in the style of **Monet** — built in C17 with GTK4, libadwaita, and WebKitGTK 6.0.

## Features

- **Pure Video Mode (Monet style):** Pure black vertical canvas with an auto-hiding translucent header bar and zero custom player chrome. Hides TikTok's sidebars, comments, banners, and action clutter while watching.
- **Persistent Account Login:** Sign in once (including Google / OAuth popups) and stay signed in via a dedicated persistent SQLite cookie store (`~/.local/share/tiko/webkit`).
- **Creator Search & Profiles:** Press `Ctrl+F` or `/` to search creators, jump straight to `@username`, or switch between **Creators** and **Videos** search. Clicking the active `@creator` pill opens their profile, and recently visited creators are saved in the star popover.
- **Full Toggle:** Press `Z` anytime to temporarily switch between Pure Video Mode and the full TikTok interface.

## Shortcuts

| Key | Action |
|---|---|
| `Space` | Play / Pause |
| `↑` / `↓` | Previous / Next video |
| `←` / `→` | Seek -5s / +5s |
| `M` | Mute / Unmute |
| `F` / `F11` | Toggle Fullscreen |
| `Ctrl+F` or `/` | Search creators / `@username` |
| `Ctrl+L` | Sign in to TikTok |
| `Z` | Toggle Pure Video Mode |
| `Alt+←` | Back |
| `Ctrl+R` / `F5` | Reload |

## Build & Install

```bash
chmod +x install.sh
./install.sh
```

Run tests:

```bash
make test
```
