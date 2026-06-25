#pragma once

#include <Arduino.h>
#include <vector>

class TerminalEmulator {
public:
    struct Cell {
        String ch{" "};
        uint8_t fg{7};
        uint8_t bg{0};
        bool bold{false};
        bool inverse{false};
        bool dirty{true};
    };

    void resize(size_t columns, size_t rows);
    void reset();
    void clear();
    void write(const char* data, size_t len);
    void write(const String& text) { write(text.c_str(), text.length()); }
    void putStatusLine(const String& text);

    size_t columns() const { return _cols; }
    size_t rows() const { return _rows; }
    size_t cursorColumn() const { return _cursorCol; }
    size_t cursorRow() const { return _cursorRow; }
    bool cursorVisible() const { return _cursorVisible; }
    bool alternateScreen() const { return _alternate; }
    const Cell& cell(size_t col, size_t row) const;
    void markAllDirty();
    void markCursorDirty();
    void clearDirty();

private:
    enum class State : uint8_t {
        Ground,
        Escape,
        Charset,
        Csi,
        Osc,
        Utf8
    };

    void resetAttrs();
    void useAlternate(bool enabled);
    void newline();
    void carriageReturn();
    void backspace();
    void tab();
    void putGlyph(const String& glyph, bool wide);
    void scrollUp(size_t top, size_t bottom, size_t count);
    void clearRow(size_t row, size_t fromCol, size_t toCol);
    void clearCells();
    void insertCells(size_t count);
    void deleteCells(size_t count);
    void insertLines(size_t count);
    void deleteLines(size_t count);
    void scrollDown(size_t top, size_t bottom, size_t count);
    void processByte(uint8_t c);
    void processEscape(uint8_t c);
    void processCsi(uint8_t c);
    void executeCsi(char command);
    int param(size_t index, int fallback) const;
    void parseParams();
    void setCursor(size_t row, size_t col);
    void markDirty(size_t col, size_t row);
    void markDirtyWithNeighbors(size_t col, size_t row);
    Cell& mutableCell(size_t col, size_t row);

    size_t _cols{80};
    size_t _rows{24};
    size_t _cursorCol{0};
    size_t _cursorRow{0};
    size_t _savedCol{0};
    size_t _savedRow{0};
    uint8_t _fg{7};
    uint8_t _bg{0};
    bool _bold{false};
    bool _inverse{false};
    bool _wrapPending{false};
    bool _alternate{false};
    bool _cursorVisible{true};
    size_t _scrollTop{0};
    size_t _scrollBottom{0};
    State _state{State::Ground};
    String _csi;
    String _osc;
    String _utf8;
    uint8_t _utf8Expected{0};
    std::vector<int> _params;
    std::vector<Cell> _main;
    std::vector<Cell> _alt;
};
