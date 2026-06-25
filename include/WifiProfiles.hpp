#pragma once

#include "AppConfig.hpp"

class WifiProfiles {
public:
    bool connectAny(const AppConfig& config, uint32_t timeoutMs = 5000);
    const String& activeName() const { return _activeName; }
    const String& lastError() const { return _lastError; }

private:
    String _activeName;
    String _lastError;
};
