#pragma once

#include "AppConfig.hpp"
#include <Arduino.h>

enum class KeyActionType : uint8_t {
    None,
    Text,
    Control,
    Scroll,
    ConnectNext,
    ConnectPrevious,
    Menu
};

struct KeyAction {
    KeyActionType type{KeyActionType::None};
    String text;
    int value{0};
};

class KeyboardMapper {
public:
    void configure(const KeyboardConfig& config);
    KeyAction mapChar(char c) const;
    KeyAction mapHid(uint8_t modifier, uint8_t keycode) const;

private:
    KeyboardConfig _config;
    char translatePrintable(char c) const;
    char translateHidPrintable(uint8_t keycode, bool shift) const;
};

