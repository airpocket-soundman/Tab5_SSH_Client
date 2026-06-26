#pragma once

#include <Arduino.h>
#include <FS.h>

class PythonRunner {
public:
    using Output = void (*)(const String& line);

    void reset();
    bool runLine(const String& line, Output output);
    bool runFile(fs::FS& fs, const String& path, Output output);
    String lastError() const { return _lastError; }
    void appendOutputChar(char c, Output output);

private:
    bool ensureVm();
    bool runSource(const String& source, Output output);
    bool shouldPrintExpression(const String& source) const;
    void flushOutput(Output output);
    void setError(const String& text);

    void* _heap{nullptr};
    size_t _heapSize{0};
    bool _started{false};
    String _lastError;
    String _pendingOutput;
};
