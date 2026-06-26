#pragma once

#include "KeyboardMapper.hpp"
#include <Arduino.h>

class Tab5KeyboardInput {
public:
    void configure(const KeyboardConfig& config);
    bool begin();
    void update();
    bool available() const;
    KeyAction read();
    String status() const { return _status; }
    void noteUsbKeyboardMounted();
    void noteUsbKeyboardUnmounted();
    void enqueueUsbReport(uint8_t devAddr, uint8_t instance, uint8_t modifier, const uint8_t* keycodes, size_t keyCount);

private:
    static constexpr size_t QueueSize = 32;
    void push(const KeyAction& action);

    KeyAction _queue[QueueSize];
    size_t _head{0};
    size_t _tail{0};
    uint32_t _events{0};
    String _status{"not initialized"};
};
