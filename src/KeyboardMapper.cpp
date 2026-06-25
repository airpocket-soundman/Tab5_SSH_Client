#include "KeyboardMapper.hpp"

#include <cctype>
#include <cstring>

namespace {
constexpr uint8_t HID_ENTER = 0x28;
constexpr uint8_t HID_ESCAPE = 0x29;
constexpr uint8_t HID_BACKSPACE = 0x2A;
constexpr uint8_t HID_TAB = 0x2B;
constexpr uint8_t HID_SPACE = 0x2C;
constexpr uint8_t HID_UP = 0x52;
constexpr uint8_t HID_DOWN = 0x51;
constexpr uint8_t HID_LEFT = 0x50;
constexpr uint8_t HID_RIGHT = 0x4F;
}

void KeyboardMapper::configure(const KeyboardConfig& config)
{
    _config = config;
}

KeyAction KeyboardMapper::mapChar(char c) const
{
    if (c == 0x1B) {
        return {KeyActionType::Menu, "", 0};
    }
    if (c == 0x11) {
        return {KeyActionType::Scroll, "", 5};
    }
    if (c == 0x12) {
        return {KeyActionType::Scroll, "", -5};
    }
    return {KeyActionType::Text, String(translatePrintable(c)), 0};
}

KeyAction KeyboardMapper::mapHid(uint8_t modifier, uint8_t keycode) const
{
    const bool shift = (modifier & 0x22) != 0;
    const bool ctrl = (modifier & 0x11) != 0;

    if (ctrl && keycode == HID_UP) {
        return {KeyActionType::Scroll, "", 5};
    }
    if (ctrl && keycode == HID_DOWN) {
        return {KeyActionType::Scroll, "", -5};
    }
    if (keycode == HID_ENTER) {
        return {KeyActionType::Text, "\r", 0};
    }
    if (keycode == HID_ESCAPE) {
        return {KeyActionType::Menu, "", 0};
    }
    if (keycode == HID_BACKSPACE) {
        return {KeyActionType::Text, String(static_cast<char>(0x7F)), 0};
    }
    if (keycode == HID_TAB) {
        return {KeyActionType::Text, "\t", 0};
    }
    if (keycode == HID_SPACE) {
        return {KeyActionType::Text, " ", 0};
    }
    if (keycode == HID_UP) {
        return {KeyActionType::Text, "\x1B[A", 0};
    }
    if (keycode == HID_DOWN) {
        return {KeyActionType::Text, "\x1B[B", 0};
    }
    if (keycode == HID_RIGHT) {
        return {KeyActionType::Text, "\x1B[C", 0};
    }
    if (keycode == HID_LEFT) {
        return {KeyActionType::Text, "\x1B[D", 0};
    }

    if (keycode >= 0x04 && keycode <= 0x1D) {
        char c = static_cast<char>('a' + keycode - 0x04);
        if (shift) {
            c = static_cast<char>(toupper(c));
        }
        if (ctrl) {
            c = static_cast<char>((tolower(c) - 'a') + 1);
        }
        return {KeyActionType::Text, String(c), 0};
    }

    static const char normal[] = "1234567890-=[]\\#;'`,./";
    static const char shifted[] = "!@#$%^&*()_+{}|~:\"~<>?";
    if (keycode >= 0x1E && keycode <= 0x38) {
        const size_t index = keycode - 0x1E;
        if (index < strlen(normal)) {
            return {KeyActionType::Text, String(shift ? shifted[index] : normal[index]), 0};
        }
    }

    return {};
}

char KeyboardMapper::translatePrintable(char c) const
{
    if (_config.layout == "jp") {
        switch (c) {
            case '@':
                return '"';
            case '"':
                return '@';
            default:
                break;
        }
    }
    return c;
}
