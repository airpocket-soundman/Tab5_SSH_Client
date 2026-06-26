#pragma once

#include "AppConfig.hpp"
#include <Arduino.h>
#include <FS.h>

class SshClient {
public:
    bool connect(const SshProfile& profile, String& error, int columns = 100, int rows = 32);
    void disconnect();
    bool connected() const;
    int read(char* buffer, size_t len);
    bool write(const uint8_t* data, size_t len);
    bool resizePty(int columns, int rows);
    bool scpDownload(const SshProfile& profile, const String& remotePath, fs::FS& fs, const String& localPath, String& error);
    bool scpUpload(const SshProfile& profile, fs::FS& fs, const String& localPath, const String& remotePath, String& error);

private:
#if ENABLE_SSH
    void* _session{nullptr};
    void* _channel{nullptr};
#endif
};
