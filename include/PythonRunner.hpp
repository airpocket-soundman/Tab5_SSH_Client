#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>

class PythonRunner {
public:
    using Output = void (*)(const String& line);

    void reset();
    bool runLine(const String& line, Output output);
    bool runFile(fs::FS& fs, const String& path, Output output);
    bool runFile(fs::FS& fs, const String& path, const String& args, Output output);
    String lastError() const { return _lastError; }
    void appendOutputChar(char c, Output output);

private:
    bool ensureVm();
    bool runSource(const String& source, Output output);
    bool shouldPrintExpression(const String& source) const;
    String buildArgvPrelude(const String& path, const String& args) const;
    String buildGfxPrelude() const;
    std::vector<String> splitArgs(const String& args) const;
    String pythonStringLiteral(const String& value) const;
    bool handleGfxOutputLine(const String& line);
    void flushOutput(Output output);
    void setError(const String& text);

    void* _heap{nullptr};
    size_t _heapSize{0};
    bool _started{false};
    bool _interrupted{false};
    String _lastError;
    String _pendingOutput;
};
