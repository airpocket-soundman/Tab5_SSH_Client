#pragma once

#include <Arduino.h>
#include <vector>

struct WifiProfile {
    String name;
    String ssid;
    String password;
};

struct SshProfile {
    String name;
    String host;
    uint16_t port{22};
    String user;
    String password;
    String terminal{"xterm-256color"};
};

struct BleHidProfile {
    String name;
    String address;
    String kind{"keyboard"};
    uint8_t addressType{1};
    bool enabled{true};
};

struct KeyboardConfig {
    String layout{"us"};
    String terminalFont{"mono12"};
    uint8_t terminalLineStep{15};
    bool swapCtrlCaps{false};
    bool bleKeyboardEnabled{false};
    std::vector<BleHidProfile> bleDevices;
    size_t activeBle{0};
    String bleKeyboardName;
    String bleKeyboardAddress;
};

struct SystemConfig {
    String deviceName{"tab5"};
    String region{"Asia/Tokyo"};
    int16_t utcOffsetMinutes{540};
    String ntpServer{"pool.ntp.org"};
};

struct AppConfig {
    std::vector<WifiProfile> wifi;
    std::vector<SshProfile> ssh;
    KeyboardConfig keyboard;
    SystemConfig system;
    size_t activeWifi{0};
    size_t activeSsh{0};
};
