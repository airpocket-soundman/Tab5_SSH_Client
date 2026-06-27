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
    String bleStatus() const;
    String bleName() const { return _bleName; }
    String bleAddress() const { return _bleAddress; }
    uint8_t bleAddressType() const { return _bleAddressType; }
    String bleKind() const { return _bleKind; }
    size_t bleScanCount() const;
    String bleScanEntry(size_t index) const;
    String bleDevicesStatus() const;
    bool bleScan(String& result);
    bool bleScanRaw(String& result);
    bool blePair(size_t index, String& result);
    bool bleScanAndPairFirst(String& result);
    bool bleGapTest(size_t index, String& result);
    bool bleGapScanAndTest(String& result);
    bool bleGapScanAndSubscribeHid(String& result);
    bool bleArduinoClientTest(String& result);
    String bleGapStatus() const;
    bool bleGapClose(String& result);
    bool bleGapSecure(String& result);
    bool bleGapListServices(String& result);
    bool bleGapSubscribeHid(String& result);
    bool bleForget(String& result);
    bool bleDisconnect(int index, String& result);
    void bleSetConnectTypes(uint8_t ownType, uint8_t peerType);
    void bleSetSecurity(uint8_t authMode, bool forceSecurity);
    void bleSetGapParams(uint16_t scanInterval, uint16_t scanWindow, uint16_t intervalMin, uint16_t intervalMax,
                         uint16_t latency, uint16_t supervisionTimeout);
    void noteUsbKeyboardMounted();
    void noteUsbKeyboardUnmounted();
    void noteBleKeyboardDisconnected();
    void noteBlePairStage(const String& status);
    void enqueueUsbReport(uint8_t devAddr, uint8_t instance, uint8_t modifier, const uint8_t* keycodes, size_t keyCount);
    void enqueueBleReport(uint8_t* previousKeys, uint8_t modifier, const uint8_t* keycodes, size_t keyCount);

private:
    static constexpr size_t QueueSize = 32;
    void push(const KeyAction& action);

    KeyAction _queue[QueueSize];
    size_t _head{0};
    size_t _tail{0};
    uint32_t _events{0};
    String _status{"not initialized"};
    bool _bleEnabled{false};
    String _bleName;
    String _bleAddress;
    uint8_t _bleAddressType{1};
    String _bleKind{"keyboard"};
    String _bleRuntimeStatus{"BLE keyboard not configured"};
};
