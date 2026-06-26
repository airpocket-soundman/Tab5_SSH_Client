#include "Tab5KeyboardInput.hpp"

#include <M5Unified.h>

#if USE_M5_TAB5_KEYBOARD
#include <M5UnitUnified.h>
#include <M5UnitUnifiedKEYBOARD.h>
#include <Wire.h>
#endif

#if ENABLE_USB_HOST_KEYBOARD
#include <tusb.h>
extern "C" esp_err_t init_usb_hal(bool external_phy);
#endif

namespace {
KeyboardMapper mapper;
Tab5KeyboardInput* activeInput = nullptr;

#if USE_M5_TAB5_KEYBOARD
m5::unit::UnitUnified units;
m5::unit::UnitTab5Keyboard keyboard;
constexpr int8_t TAB5_KEYBOARD_SDA = 0;
constexpr int8_t TAB5_KEYBOARD_SCL = 1;
#endif

#if ENABLE_USB_HOST_KEYBOARD
bool usbHostStarted = false;
uint32_t usbReports = 0;
uint8_t usbKeyboardCount = 0;

struct UsbKeyboardState {
    bool active{false};
    uint8_t devAddr{0};
    uint8_t instance{0};
    uint8_t previous[6]{};
};

UsbKeyboardState usbStates[6];

UsbKeyboardState* usbState(uint8_t devAddr, uint8_t instance, bool create)
{
    UsbKeyboardState* freeSlot = nullptr;
    for (auto& state : usbStates) {
        if (state.active && state.devAddr == devAddr && state.instance == instance) {
            return &state;
        }
        if (!state.active && !freeSlot) {
            freeSlot = &state;
        }
    }
    if (!create || !freeSlot) {
        return nullptr;
    }
    *freeSlot = {};
    freeSlot->active = true;
    freeSlot->devAddr = devAddr;
    freeSlot->instance = instance;
    return freeSlot;
}

void clearUsbState(uint8_t devAddr, uint8_t instance)
{
    for (auto& state : usbStates) {
        if (state.active && state.devAddr == devAddr && state.instance == instance) {
            state = {};
            return;
        }
    }
}

bool keyWasPressed(const UsbKeyboardState& state, uint8_t keycode)
{
    for (uint8_t previous : state.previous) {
        if (previous == keycode) {
            return true;
        }
    }
    return false;
}

bool beginUsbHost()
{
    if (usbHostStarted) {
        return true;
    }
    init_usb_hal(false);
    tusb_rhport_init_t init = {};
    init.role = TUSB_ROLE_HOST;
#if CONFIG_IDF_TARGET_ESP32P4
    init.speed = TUSB_SPEED_HIGH;
    usbHostStarted = tusb_init(BOARD_TUH_RHPORT, &init);
#else
    init.speed = TUSB_SPEED_FULL;
    usbHostStarted = tusb_init(0, &init);
#endif
    return usbHostStarted;
}
#endif

KeyAction mapNamedKey(const char* chars)
{
    String token(chars);
    token.toLowerCase();
    token.replace("_", " ");
    token.replace("-", " ");
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
    if (token == "minus" || token == "hyphen") {
        return mapper.mapChar('-');
    }
    if (token == "underscore") {
        return mapper.mapChar('_');
    }
    if (token == "equal" || token == "equals") {
        return mapper.mapChar('=');
    }
    if (token == "plus") {
        return mapper.mapChar('+');
    }
    if (token == "left bracket" || token == "open bracket" || token == "lbracket") {
        return mapper.mapChar('[');
    }
    if (token == "right bracket" || token == "close bracket" || token == "rbracket") {
        return mapper.mapChar(']');
    }
    if (token == "left brace" || token == "open brace" || token == "lbrace") {
        return mapper.mapChar('{');
    }
    if (token == "right brace" || token == "close brace" || token == "rbrace") {
        return mapper.mapChar('}');
    }
    if (token == "backslash") {
        return mapper.mapChar('\\');
    }
    if (token == "pipe" || token == "vertical bar") {
        return mapper.mapChar('|');
    }
    if (token == "semicolon") {
        return mapper.mapChar(';');
    }
    if (token == "colon") {
        return mapper.mapChar(':');
    }
    if (token == "quote" || token == "apostrophe" || token == "single quote") {
        return mapper.mapChar('\'');
    }
    if (token == "double quote" || token == "quotation mark") {
        return mapper.mapChar('"');
    }
    if (token == "grave" || token == "backquote" || token == "backtick") {
        return mapper.mapChar('`');
    }
    if (token == "tilde") {
        return mapper.mapChar('~');
    }
    if (token == "comma") {
        return mapper.mapChar(',');
    }
    if (token == "period" || token == "dot") {
        return mapper.mapChar('.');
    }
    if (token == "slash" || token == "forward slash") {
        return mapper.mapChar('/');
    }
    if (token == "question" || token == "question mark") {
        return mapper.mapChar('?');
    }
    if (token == "exclamation" || token == "exclamation mark") {
        return mapper.mapChar('!');
    }
    if (token == "at" || token == "at sign") {
        return mapper.mapChar('@');
    }
    if (token == "hash" || token == "number sign" || token == "pound") {
        return mapper.mapChar('#');
    }
    if (token == "dollar" || token == "dollar sign") {
        return mapper.mapChar('$');
    }
    if (token == "percent" || token == "percent sign") {
        return mapper.mapChar('%');
    }
    if (token == "caret" || token == "circumflex") {
        return mapper.mapChar('^');
    }
    if (token == "ampersand") {
        return mapper.mapChar('&');
    }
    if (token == "asterisk" || token == "star") {
        return mapper.mapChar('*');
    }
    if (token == "left paren" || token == "open paren" || token == "left parenthesis") {
        return mapper.mapChar('(');
    }
    if (token == "right paren" || token == "close paren" || token == "right parenthesis") {
        return mapper.mapChar(')');
    }
    if (token == "less" || token == "less than") {
        return mapper.mapChar('<');
    }
    if (token == "greater" || token == "greater than") {
        return mapper.mapChar('>');
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

#if ENABLE_USB_HOST_KEYBOARD
extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx, const uint8_t*, uint16_t)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD) {
        usbState(dev_addr, idx, true);
        ++usbKeyboardCount;
        if (activeInput) {
            activeInput->noteUsbKeyboardMounted();
        }
        tuh_hid_receive_report(dev_addr, idx);
    }
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD) {
        clearUsbState(dev_addr, idx);
        if (usbKeyboardCount) {
            --usbKeyboardCount;
        }
        if (activeInput) {
            activeInput->noteUsbKeyboardUnmounted();
        }
    }
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx, const uint8_t* report, uint16_t len)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD && len >= sizeof(hid_keyboard_report_t)) {
        const auto* keyboardReport = reinterpret_cast<const hid_keyboard_report_t*>(report);
        if (activeInput) {
            activeInput->enqueueUsbReport(dev_addr, idx, keyboardReport->modifier, keyboardReport->keycode,
                                          sizeof(keyboardReport->keycode));
        }
        ++usbReports;
    }
    tuh_hid_receive_report(dev_addr, idx);
}
#endif
}

void Tab5KeyboardInput::configure(const KeyboardConfig& config)
{
    mapper.configure(config);
    _bleEnabled = config.bleKeyboardEnabled;
    _bleName = config.bleKeyboardName;
    _bleAddress = config.bleKeyboardAddress;
    if (_bleEnabled) {
        _bleRuntimeStatus = "BLE keyboard enabled; HID backend unavailable on this build";
    } else {
        _bleRuntimeStatus = "BLE keyboard disabled";
    }
    activeInput = this;
}

bool Tab5KeyboardInput::begin()
{
    activeInput = this;
    bool tab5Ready = false;
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
    } else {
        uint8_t fw = keyboard.firmwareVersion();
        keyboard.writeMode(m5::unit::tab5_keyboard::Mode::Character);
        _status = String("Tab5 keyboard ready fw=0x") + String(fw, HEX);
        tab5Ready = true;
    }
#else
    _status = "Serial input fallback active";
#endif
#if ENABLE_USB_HOST_KEYBOARD
    if (beginUsbHost()) {
        _status += "; USB host ready";
    } else {
        _status += "; USB host unavailable";
    }
#endif
    if (_bleEnabled) {
        _status += "; BLE keyboard pending";
    }
    return tab5Ready;
}

void Tab5KeyboardInput::update()
{
#if ENABLE_USB_HOST_KEYBOARD
    if (usbHostStarted) {
        tuh_task_ext(0, false);
    }
#endif
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

void Tab5KeyboardInput::noteUsbKeyboardMounted()
{
    _status = String("USB keyboard ready count=") + usbKeyboardCount;
}

void Tab5KeyboardInput::noteUsbKeyboardUnmounted()
{
    _status = String("USB keyboard removed count=") + usbKeyboardCount;
}

void Tab5KeyboardInput::enqueueUsbReport(uint8_t devAddr, uint8_t instance, uint8_t modifier, const uint8_t* keycodes,
                                         size_t keyCount)
{
#if ENABLE_USB_HOST_KEYBOARD
    UsbKeyboardState* state = usbState(devAddr, instance, true);
    if (!state) {
        return;
    }
    for (size_t i = 0; i < keyCount; ++i) {
        uint8_t keycode = keycodes[i];
        if (!keycode || keyWasPressed(*state, keycode)) {
            continue;
        }
        push(mapper.mapHid(modifier, keycode));
    }
    memset(state->previous, 0, sizeof(state->previous));
    memcpy(state->previous, keycodes, min(keyCount, sizeof(state->previous)));
    _status = String("USB keyboard reports=") + usbReports;
#else
    (void)devAddr;
    (void)instance;
    (void)modifier;
    (void)keycodes;
    (void)keyCount;
#endif
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

String Tab5KeyboardInput::bleStatus() const
{
    String text = _bleRuntimeStatus;
    if (_bleName.length()) {
        text += String(" name=") + _bleName;
    }
    if (_bleAddress.length()) {
        text += String(" addr=") + _bleAddress;
    }
    return text;
}

bool Tab5KeyboardInput::bleScan(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    result = "BLE HID backend is enabled, but scanner is not bound yet";
    return false;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::blePair(size_t index, String& result)
{
    (void)index;
#if ENABLE_BLE_HID_KEYBOARD
    result = "BLE HID backend is enabled, but pairing is not bound yet";
    return false;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleForget(String& result)
{
    _bleName = "";
    _bleAddress = "";
    _bleRuntimeStatus = _bleEnabled ? "BLE keyboard enabled; no paired keyboard" : "BLE keyboard disabled";
    result = "BLE keyboard pairing cleared";
    return true;
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
