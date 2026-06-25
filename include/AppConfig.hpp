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

struct KeyboardConfig {
    String layout{"us"};
    String terminalFont{"mono12"};
    uint8_t terminalLineStep{15};
    bool swapCtrlCaps{false};
};

struct AppConfig {
    std::vector<WifiProfile> wifi;
    std::vector<SshProfile> ssh;
    KeyboardConfig keyboard;
    size_t activeWifi{0};
    size_t activeSsh{0};
};
