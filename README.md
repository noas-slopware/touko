# Touko

A terminal for Linux phones. The UI is Kirigami and QML, the terminal emulation is
Ghostty's, through libghostty-vt. Built for the Galaxy Z Flip3 on Arch Linux Ports with
Plasma Mobile, and it runs on any Linux desktop too.

## What it does

- Ghostty's VT core: parsing, reflow on resize, scrollback, kitty keyboard protocol,
  modifyOtherKeys, mouse reporting, bracketed paste, OSC 52 clipboard writes, OSC 8
  hyperlinks, OSC 9/777 notifications, colour scheme reports (mode 2031).
- Rendering in the Qt Quick scene graph, one texture per row, and only rows libghostty
  marks dirty get repainted. Box drawing, block elements and powerline separators are drawn
  as shapes, so they join up whatever the font is. Wide characters (Hangul, CJK, emoji)
  take exactly two cells.
- On-screen keyboard support: Hangul composition shows as preedit at the cursor and is sent
  once it is committed. No autocorrect, auto capitals or prediction.
- An extra-keys row: Esc, Ctrl, Alt, Tab, arrows, Home/End, PgUp/PgDn, Del, F1 to F12 and a
  few symbols. Ctrl and Alt latch for one key; a double tap locks them.
- Touch: tap to type, drag to scroll (with fling), pinch to zoom, long press to select a
  word, then drag the handles. Full-screen programs get arrow keys or mouse wheel events
  from a drag, depending on what they asked for.
- Tabs, Ghostty's shell integration (fish, zsh, bash: prompt marks, titles, working
  directory), and any of Ghostty's 800-odd themes, with separate light and dark picks that
  follow the system.
- Nothing runs while nothing happens: no polling. The cursor blinks only when a program
  asks for it, and stops after 15 seconds without activity.

## Building

Needs Qt 6.8+, Kirigami 6, CMake, and Arch's `ghostty` package (it ships
`libghostty-vt.so` and its headers).

```sh
cmake -S . -B build -G Ninja
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build
./build/touko
```

`packaging/PKGBUILD` builds it for x86_64 and aarch64. libghostty-vt's C API is not stable
yet, so rebuild Touko whenever ghostty is updated.

## Running

```sh
touko                    # your login shell
touko -w ~/src           # starting somewhere else
touko -e htop            # a command instead of the shell
```

Shortcuts with a hardware keyboard: Ctrl+Shift+C/V copy and paste, Ctrl+Shift+T and
Ctrl+Shift+W open and close tabs, Ctrl+PgUp/PgDn switch tabs, Shift+PgUp/PgDn scroll,
Ctrl+plus/minus zoom.

## Testing without a phone

The tests drive the terminal and the item offscreen, checking the bytes that reach the pty
for keys, input-method commits, preedit and latched modifiers. For a look at the UI at
phone size:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_CONTROLS_MOBILE=1 QT_SCALE_FACTOR=3 \
  ./build/touko --size 360x880 --screenshot shot.png --test-page settings
```

`--test-page` takes `settings`, `light-themes`, `dark-themes`, `tabs` or `select`, and
`--test-preedit 허` shows a preedit.

## Licence

GPL-2.0-or-later. libghostty-vt is MIT.
