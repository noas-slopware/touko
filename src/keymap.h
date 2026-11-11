// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QtGlobal>
#include <ghostty/vt.h>

struct KeyInfo {
    GhosttyKey key = GHOSTTY_KEY_UNIDENTIFIED;
    uint unshifted = 0; // codepoint the key gives without modifiers (US layout)
    bool shifted = false; // the character needs Shift on a US layout
};

// From a Linux evdev key code (XKB keycode - 8), which is what Wayland
// hands Qt as nativeScanCode for both hardware and on-screen keyboards.
KeyInfo keyFromEvdev(quint32 evdev);
// From Qt::Key, when there is no scan code.
KeyInfo keyFromQt(int qtKey);
// From a character an input method committed, so a latched Ctrl or Alt
// from the extra-keys row can be combined with it.
KeyInfo keyFromChar(char32_t c);
