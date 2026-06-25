#include "Tab5KeyboardInput.hpp"

#include <M5Unified.h>

#if USE_M5_TAB5_KEYBOARD
#include <M5UnitUnified.h>
#include <M5UnitUnifiedKEYBOARD.h>
#include <Wire.h>
#endif

namespace {
KeyboardMapper mapper;

#if USE_M5_TAB5_KEYBOARD
m5::unit::UnitUnified units;
m5::unit::UnitTab5Keyboard keyboard;
constexpr int8_t TAB5_KEYBOARD_SDA = 0;
constexpr int8_t TAB5_KEYBOARD_SCL = 1;
#endif

KeyAction mapNamedKey(const char* chars)
{
    String token(chars);
    token.toLowerCase();
    if (token == "backspace" || token == "bs") {
        return mapper.mapChar(static_cast<char>(0x7F));
    }
    if (token == "enter" || token == "return") {
        return mapper.mapChar('\r');
    }
    if (token == "tab") {
        return mapper.mapChar('\t');
    }
    if (token == "esc" || token == "escape") {
        return mapper.mapChar(static_cast<char>(0x1B));
    }
    if (token == "space") {
        return mapper.mapChar(' ');
    }
    if (token == "up" || token == "up arrow" || token == "arrow up") {
        return {KeyActionType::Text, "\x1B[A", 0};
    }
    if (token == "down" || token == "down arrow" || token == "arrow down") {
        return {KeyActionType::Text, "\x1B[B", 0};
    }
    if (token == "right" || token == "right arrow" || token == "arrow right") {
        return {KeyActionType::Text, "\x1B[C", 0};
    }
    if (token == "left" || token == "left arrow" || token == "arrow left") {
        return {KeyActionType::Text, "\x1B[D", 0};
    }
    if (token == "home") {
        return {KeyActionType::Text, "\x1B[H", 0};
    }
    if (token == "end") {
        return {KeyActionType::Text, "\x1B[F", 0};
    }
    if (token == "delete" || token == "del") {
        return {KeyActionType::Text, "\x1B[3~", 0};
    }
    if (token == "insert" || token == "ins") {
        return {KeyActionType::Text, "\x1B[2~", 0};
    }
    if (token == "page up" || token == "pgup") {
        return {KeyActionType::Text, "\x1B[5~", 0};
    }
    if (token == "page down" || token == "pgdn") {
        return {KeyActionType::Text, "\x1B[6~", 0};
    }
    return {};
}

KeyAction mapCtrlCharacter(char c)
{
    if (c >= 'a' && c <= 'z') {
        return {KeyActionType::Text, String(static_cast<char>(c - 'a' + 1)), 0};
    }
    if (c >= 'A' && c <= 'Z') {
        return {KeyActionType::Text, String(static_cast<char>(c - 'A' + 1)), 0};
    }
    switch (c) {
        case '[':
            return {KeyActionType::Text, String(static_cast<char>(0x1B)), 0};
        case '\\':
            return {KeyActionType::Text, String(static_cast<char>(0x1C)), 0};
        case ']':
            return {KeyActionType::Text, String(static_cast<char>(0x1D)), 0};
        case '^':
            return {KeyActionType::Text, String(static_cast<char>(0x1E)), 0};
        case '_':
            return {KeyActionType::Text, String(static_cast<char>(0x1F)), 0};
        case '?':
            return {KeyActionType::Text, String(static_cast<char>(0x7F)), 0};
        default:
            return {};
    }
}
}

void Tab5KeyboardInput::configure(const KeyboardConfig& config)
{
    mapper.configure(config);
}

bool Tab5KeyboardInput::begin()
{
#if USE_M5_TAB5_KEYBOARD
    auto cfg = keyboard.config();
    cfg.mode = m5::unit::tab5_keyboard::Mode::Character;
    cfg.software_repeat = true;
    cfg.irq_pin = -1;
    cfg.interval_ms = 10;
    keyboard.config(cfg);

    Wire.end();
    Wire.begin(TAB5_KEYBOARD_SDA, TAB5_KEYBOARD_SCL, keyboard.component_config().clock);
    if (!units.add(keyboard, Wire) || !units.begin()) {
        _status = "Tab5 keyboard not found; Serial input fallback active";
        return false;
    }
    uint8_t fw = keyboard.firmwareVersion();
    keyboard.writeMode(m5::unit::tab5_keyboard::Mode::Character);
    _status = String("Tab5 keyboard ready fw=0x") + String(fw, HEX);
    return true;
#else
    _status = "Serial input fallback active";
    return false;
#endif
}

void Tab5KeyboardInput::update()
{
#if USE_M5_TAB5_KEYBOARD
    keyboard.update(true);
    while (!keyboard.empty()) {
        auto event = keyboard.oldest();
        ++_events;
        if (event.type == m5::unit::tab5_keyboard::EventType::Character) {
            KeyAction named = mapNamedKey(event.chr.chars);
            if (named.type != KeyActionType::None) {
                push(named);
            } else {
                for (uint8_t i = 0; i < event.chr.length; ++i) {
                    KeyAction action = event.isCtrl() ? mapCtrlCharacter(event.chr.chars[i]) : KeyAction{};
                    push(action.type != KeyActionType::None ? action : mapper.mapChar(event.chr.chars[i]));
                }
            }
        } else if (event.type == m5::unit::tab5_keyboard::EventType::Hid) {
            push(mapper.mapHid(event.modifier, event.hid.keycode));
        }
        _status = String("Tab5 keyboard events=") + _events;
        keyboard.discard();
    }
#endif

    while (Serial.available()) {
        push(mapper.mapChar(static_cast<char>(Serial.read())));
    }
}

bool Tab5KeyboardInput::available() const
{
    return _head != _tail;
}

KeyAction Tab5KeyboardInput::read()
{
    if (!available()) {
        return {};
    }
    KeyAction action = _queue[_tail];
    _tail = (_tail + 1) % QueueSize;
    return action;
}

void Tab5KeyboardInput::push(const KeyAction& action)
{
    if (action.type == KeyActionType::None) {
        return;
    }
    const size_t next = (_head + 1) % QueueSize;
    if (next == _tail) {
        _tail = (_tail + 1) % QueueSize;
    }
    _queue[_head] = action;
    _head = next;
}
