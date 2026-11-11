// SPDX-License-Identifier: GPL-2.0-or-later
#include "keymap.h"

#include <linux/input-event-codes.h>
#include <Qt>

namespace {

struct Entry {
    quint32 evdev;
    GhosttyKey key;
    char lower;
    char upper;
};

// US layout: the unshifted and shifted character of each printable key.
constexpr Entry kTable[] = {
    {KEY_GRAVE, GHOSTTY_KEY_BACKQUOTE, '`', '~'},
    {KEY_1, GHOSTTY_KEY_DIGIT_1, '1', '!'},
    {KEY_2, GHOSTTY_KEY_DIGIT_2, '2', '@'},
    {KEY_3, GHOSTTY_KEY_DIGIT_3, '3', '#'},
    {KEY_4, GHOSTTY_KEY_DIGIT_4, '4', '$'},
    {KEY_5, GHOSTTY_KEY_DIGIT_5, '5', '%'},
    {KEY_6, GHOSTTY_KEY_DIGIT_6, '6', '^'},
    {KEY_7, GHOSTTY_KEY_DIGIT_7, '7', '&'},
    {KEY_8, GHOSTTY_KEY_DIGIT_8, '8', '*'},
    {KEY_9, GHOSTTY_KEY_DIGIT_9, '9', '('},
    {KEY_0, GHOSTTY_KEY_DIGIT_0, '0', ')'},
    {KEY_MINUS, GHOSTTY_KEY_MINUS, '-', '_'},
    {KEY_EQUAL, GHOSTTY_KEY_EQUAL, '=', '+'},
    {KEY_Q, GHOSTTY_KEY_Q, 'q', 'Q'},
    {KEY_W, GHOSTTY_KEY_W, 'w', 'W'},
    {KEY_E, GHOSTTY_KEY_E, 'e', 'E'},
    {KEY_R, GHOSTTY_KEY_R, 'r', 'R'},
    {KEY_T, GHOSTTY_KEY_T, 't', 'T'},
    {KEY_Y, GHOSTTY_KEY_Y, 'y', 'Y'},
    {KEY_U, GHOSTTY_KEY_U, 'u', 'U'},
    {KEY_I, GHOSTTY_KEY_I, 'i', 'I'},
    {KEY_O, GHOSTTY_KEY_O, 'o', 'O'},
    {KEY_P, GHOSTTY_KEY_P, 'p', 'P'},
    {KEY_LEFTBRACE, GHOSTTY_KEY_BRACKET_LEFT, '[', '{'},
    {KEY_RIGHTBRACE, GHOSTTY_KEY_BRACKET_RIGHT, ']', '}'},
    {KEY_BACKSLASH, GHOSTTY_KEY_BACKSLASH, '\\', '|'},
    {KEY_A, GHOSTTY_KEY_A, 'a', 'A'},
    {KEY_S, GHOSTTY_KEY_S, 's', 'S'},
    {KEY_D, GHOSTTY_KEY_D, 'd', 'D'},
    {KEY_F, GHOSTTY_KEY_F, 'f', 'F'},
    {KEY_G, GHOSTTY_KEY_G, 'g', 'G'},
    {KEY_H, GHOSTTY_KEY_H, 'h', 'H'},
    {KEY_J, GHOSTTY_KEY_J, 'j', 'J'},
    {KEY_K, GHOSTTY_KEY_K, 'k', 'K'},
    {KEY_L, GHOSTTY_KEY_L, 'l', 'L'},
    {KEY_SEMICOLON, GHOSTTY_KEY_SEMICOLON, ';', ':'},
    {KEY_APOSTROPHE, GHOSTTY_KEY_QUOTE, '\'', '"'},
    {KEY_Z, GHOSTTY_KEY_Z, 'z', 'Z'},
    {KEY_X, GHOSTTY_KEY_X, 'x', 'X'},
    {KEY_C, GHOSTTY_KEY_C, 'c', 'C'},
    {KEY_V, GHOSTTY_KEY_V, 'v', 'V'},
    {KEY_B, GHOSTTY_KEY_B, 'b', 'B'},
    {KEY_N, GHOSTTY_KEY_N, 'n', 'N'},
    {KEY_M, GHOSTTY_KEY_M, 'm', 'M'},
    {KEY_COMMA, GHOSTTY_KEY_COMMA, ',', '<'},
    {KEY_DOT, GHOSTTY_KEY_PERIOD, '.', '>'},
    {KEY_SLASH, GHOSTTY_KEY_SLASH, '/', '?'},
    {KEY_SPACE, GHOSTTY_KEY_SPACE, ' ', ' '},
    {KEY_102ND, GHOSTTY_KEY_INTL_BACKSLASH, '\\', '|'},
};

struct Special {
    quint32 evdev;
    int qt;
    GhosttyKey key;
};

constexpr Special kSpecial[] = {
    {KEY_ESC, Qt::Key_Escape, GHOSTTY_KEY_ESCAPE},
    {KEY_BACKSPACE, Qt::Key_Backspace, GHOSTTY_KEY_BACKSPACE},
    {KEY_TAB, Qt::Key_Tab, GHOSTTY_KEY_TAB},
    {KEY_ENTER, Qt::Key_Return, GHOSTTY_KEY_ENTER},
    {KEY_LEFTCTRL, Qt::Key_Control, GHOSTTY_KEY_CONTROL_LEFT},
    {KEY_RIGHTCTRL, 0, GHOSTTY_KEY_CONTROL_RIGHT},
    {KEY_LEFTSHIFT, Qt::Key_Shift, GHOSTTY_KEY_SHIFT_LEFT},
    {KEY_RIGHTSHIFT, 0, GHOSTTY_KEY_SHIFT_RIGHT},
    {KEY_LEFTALT, Qt::Key_Alt, GHOSTTY_KEY_ALT_LEFT},
    {KEY_RIGHTALT, Qt::Key_AltGr, GHOSTTY_KEY_ALT_RIGHT},
    {KEY_LEFTMETA, Qt::Key_Meta, GHOSTTY_KEY_META_LEFT},
    {KEY_RIGHTMETA, 0, GHOSTTY_KEY_META_RIGHT},
    {KEY_CAPSLOCK, Qt::Key_CapsLock, GHOSTTY_KEY_CAPS_LOCK},
    {KEY_COMPOSE, Qt::Key_Menu, GHOSTTY_KEY_CONTEXT_MENU},
    {KEY_F1, Qt::Key_F1, GHOSTTY_KEY_F1},
    {KEY_F2, Qt::Key_F2, GHOSTTY_KEY_F2},
    {KEY_F3, Qt::Key_F3, GHOSTTY_KEY_F3},
    {KEY_F4, Qt::Key_F4, GHOSTTY_KEY_F4},
    {KEY_F5, Qt::Key_F5, GHOSTTY_KEY_F5},
    {KEY_F6, Qt::Key_F6, GHOSTTY_KEY_F6},
    {KEY_F7, Qt::Key_F7, GHOSTTY_KEY_F7},
    {KEY_F8, Qt::Key_F8, GHOSTTY_KEY_F8},
    {KEY_F9, Qt::Key_F9, GHOSTTY_KEY_F9},
    {KEY_F10, Qt::Key_F10, GHOSTTY_KEY_F10},
    {KEY_F11, Qt::Key_F11, GHOSTTY_KEY_F11},
    {KEY_F12, Qt::Key_F12, GHOSTTY_KEY_F12},
    {KEY_SYSRQ, Qt::Key_Print, GHOSTTY_KEY_PRINT_SCREEN},
    {KEY_SCROLLLOCK, Qt::Key_ScrollLock, GHOSTTY_KEY_SCROLL_LOCK},
    {KEY_PAUSE, Qt::Key_Pause, GHOSTTY_KEY_PAUSE},
    {KEY_INSERT, Qt::Key_Insert, GHOSTTY_KEY_INSERT},
    {KEY_HOME, Qt::Key_Home, GHOSTTY_KEY_HOME},
    {KEY_PAGEUP, Qt::Key_PageUp, GHOSTTY_KEY_PAGE_UP},
    {KEY_DELETE, Qt::Key_Delete, GHOSTTY_KEY_DELETE},
    {KEY_END, Qt::Key_End, GHOSTTY_KEY_END},
    {KEY_PAGEDOWN, Qt::Key_PageDown, GHOSTTY_KEY_PAGE_DOWN},
    {KEY_UP, Qt::Key_Up, GHOSTTY_KEY_ARROW_UP},
    {KEY_DOWN, Qt::Key_Down, GHOSTTY_KEY_ARROW_DOWN},
    {KEY_LEFT, Qt::Key_Left, GHOSTTY_KEY_ARROW_LEFT},
    {KEY_RIGHT, Qt::Key_Right, GHOSTTY_KEY_ARROW_RIGHT},
    {KEY_NUMLOCK, Qt::Key_NumLock, GHOSTTY_KEY_NUM_LOCK},
    {KEY_KPENTER, Qt::Key_Enter, GHOSTTY_KEY_NUMPAD_ENTER},
    {KEY_KP0, 0, GHOSTTY_KEY_NUMPAD_0},
    {KEY_KP1, 0, GHOSTTY_KEY_NUMPAD_1},
    {KEY_KP2, 0, GHOSTTY_KEY_NUMPAD_2},
    {KEY_KP3, 0, GHOSTTY_KEY_NUMPAD_3},
    {KEY_KP4, 0, GHOSTTY_KEY_NUMPAD_4},
    {KEY_KP5, 0, GHOSTTY_KEY_NUMPAD_5},
    {KEY_KP6, 0, GHOSTTY_KEY_NUMPAD_6},
    {KEY_KP7, 0, GHOSTTY_KEY_NUMPAD_7},
    {KEY_KP8, 0, GHOSTTY_KEY_NUMPAD_8},
    {KEY_KP9, 0, GHOSTTY_KEY_NUMPAD_9},
    {KEY_KPDOT, 0, GHOSTTY_KEY_NUMPAD_DECIMAL},
    {KEY_KPPLUS, 0, GHOSTTY_KEY_NUMPAD_ADD},
    {KEY_KPMINUS, 0, GHOSTTY_KEY_NUMPAD_SUBTRACT},
    {KEY_KPASTERISK, 0, GHOSTTY_KEY_NUMPAD_MULTIPLY},
    {KEY_KPSLASH, 0, GHOSTTY_KEY_NUMPAD_DIVIDE},
    {KEY_KPEQUAL, 0, GHOSTTY_KEY_NUMPAD_EQUAL},
    {0, Qt::Key_Backtab, GHOSTTY_KEY_TAB},
};

} // namespace

KeyInfo keyFromEvdev(quint32 evdev)
{
    for (const Entry &e : kTable) {
        if (e.evdev == evdev)
            return {e.key, uint(uchar(e.lower)), false};
    }
    for (const Special &s : kSpecial) {
        if (s.evdev && s.evdev == evdev)
            return {s.key, 0, false};
    }
    return {};
}

KeyInfo keyFromQt(int qtKey)
{
    for (const Special &s : kSpecial) {
        if (s.qt && s.qt == qtKey)
            return {s.key, 0, false};
    }
    if (qtKey >= 0x20 && qtKey < 0x7f) {
        char32_t c = char32_t(qtKey);
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        return keyFromChar(c);
    }
    return {};
}

KeyInfo keyFromChar(char32_t c)
{
    for (const Entry &e : kTable) {
        if (char32_t(uchar(e.lower)) == c)
            return {e.key, uint(uchar(e.lower)), false};
        if (char32_t(uchar(e.upper)) == c)
            return {e.key, uint(uchar(e.lower)), true};
    }
    if (c == '\n' || c == '\r')
        return {GHOSTTY_KEY_ENTER, 0, false};
    if (c == '\t')
        return {GHOSTTY_KEY_TAB, 0, false};
    return {};
}
