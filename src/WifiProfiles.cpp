#include "WifiProfiles.hpp"

#include <WiFi.h>

bool WifiProfiles::connectAny(const AppConfig& config, uint32_t timeoutMs)
{
    _activeName = "";
    _lastError = "";
    WiFi.mode(WIFI_STA);

    for (const auto& profile : config.wifi) {
        WiFi.disconnect(true);
        delay(150);
        WiFi.begin(profile.ssid.c_str(), profile.password.c_str());

        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
            delay(250);
        }

        if (WiFi.status() == WL_CONNECTED) {
            _activeName = profile.name;
            return true;
        }
    }

    _lastError = "No Wi-Fi profile connected";
    return false;
}

