#pragma once

#include "AppConfig.hpp"

class SettingsStore {
public:
    bool begin();
    bool load(AppConfig& config);
    bool save(const AppConfig& config);
    const String& lastError() const { return _lastError; }

private:
    String _lastError;
};

