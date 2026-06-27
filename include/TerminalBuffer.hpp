#pragma once

#include <Arduino.h>
#include <vector>

class TerminalBuffer {
public:
    explicit TerminalBuffer(size_t maxLines = 2000);

    void setViewport(size_t columns, size_t rows);
    void clear();
    void append(const char* data, size_t len);
    void append(const String& text);
    void inputEcho(char c);
    void backspace();
    void scroll(int delta);
    void scrollToBottom();
    String lineAt(size_t viewportRow) const;
    size_t inputViewportRow() const;
    size_t viewportRows() const { return _rows; }
    bool atBottom() const;

private:
    void pushLine(String line);
    void appendChar(char c);
    void trim();

    size_t _maxLines;
    size_t _columns{100};
    size_t _rows{32};
    size_t _scrollOffset{0};
    uint8_t _escapeState{0};
    std::vector<String> _lines;
    String _current;
};
