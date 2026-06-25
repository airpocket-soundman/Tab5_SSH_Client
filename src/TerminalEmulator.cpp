#include "TerminalEmulator.hpp"

#include <algorithm>
#include <cstdlib>

namespace {
constexpr uint32_t DirectColorFlag = 0x01000000UL;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint32_t directColor(uint8_t r, uint8_t g, uint8_t b)
{
    return DirectColorFlag | rgb565(r, g, b);
}

TerminalEmulator::Cell BlankCell(uint32_t fg = 7, uint32_t bg = 0)
{
    TerminalEmulator::Cell cell;
    cell.ch = " ";
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = false;
    cell.inverse = false;
    cell.dirty = true;
    return cell;
}

bool isUtf8Continuation(uint8_t c)
{
    return (c & 0xC0) == 0x80;
}

uint8_t utf8Length(uint8_t c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}
}

void TerminalEmulator::resize(size_t columns, size_t rows)
{
    columns = std::max<size_t>(10, columns);
    rows = std::max<size_t>(5, rows);
    if (columns == _cols && rows == _rows && !_main.empty() && !_alt.empty()) {
        return;
    }
    _cols = columns;
    _rows = rows;
    _main.assign(_cols * _rows, BlankCell());
    _alt.assign(_cols * _rows, BlankCell());
    _cursorCol = 0;
    _cursorRow = 0;
    _savedCol = 0;
    _savedRow = 0;
    _scrollTop = 0;
    _scrollBottom = _rows ? _rows - 1 : 0;
    _wrapPending = false;
    markAllDirty();
}

void TerminalEmulator::reset()
{
    clear();
    _alternate = false;
    _cursorVisible = true;
    _state = State::Ground;
    _csi = "";
    _osc = "";
    _utf8 = "";
    _utf8Expected = 0;
    resetAttrs();
}

void TerminalEmulator::clear()
{
    clearCells();
    _cursorCol = 0;
    _cursorRow = 0;
    _scrollTop = 0;
    _scrollBottom = _rows ? _rows - 1 : 0;
    _wrapPending = false;
}

void TerminalEmulator::write(const char* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        processByte(static_cast<uint8_t>(data[i]));
    }
}

void TerminalEmulator::putStatusLine(const String& text)
{
    if (!_rows) return;
    scrollUp(0, _rows - 1, 1);
    setCursor(_rows - 1, 0);
    resetAttrs();
    write("[tab5] ", 7);
    write(text);
}

const TerminalEmulator::Cell& TerminalEmulator::cell(size_t col, size_t row) const
{
    static Cell blank;
    const auto& buf = _alternate ? _alt : _main;
    if (col >= _cols || row >= _rows || buf.empty()) {
        return blank;
    }
    return buf[row * _cols + col];
}

void TerminalEmulator::markAllDirty()
{
    for (auto& cell : _main) cell.dirty = true;
    for (auto& cell : _alt) cell.dirty = true;
}

void TerminalEmulator::clearDirty()
{
    auto& buf = _alternate ? _alt : _main;
    for (auto& cell : buf) cell.dirty = false;
}

void TerminalEmulator::markCursorDirty()
{
    if (_cursorCol < _cols && _cursorRow < _rows) {
        mutableCell(_cursorCol, _cursorRow).dirty = true;
    }
}

void TerminalEmulator::resetAttrs()
{
    _fg = 7;
    _bg = 0;
    _bold = false;
    _inverse = false;
}

void TerminalEmulator::useAlternate(bool enabled)
{
    if (_alternate == enabled) {
        return;
    }
    if (enabled) {
        _savedCol = _cursorCol;
        _savedRow = _cursorRow;
    }
    _alternate = enabled;
    if (enabled) {
        _cursorCol = 0;
        _cursorRow = 0;
    } else {
        _cursorCol = std::min(_savedCol, _cols ? _cols - 1 : 0);
        _cursorRow = std::min(_savedRow, _rows ? _rows - 1 : 0);
    }
    _wrapPending = false;
    if (enabled) {
        for (auto& cell : _alt) cell = BlankCell();
    }
    markAllDirty();
}

void TerminalEmulator::newline()
{
    _wrapPending = false;
    size_t bottom = _scrollBottom ? _scrollBottom : (_rows ? _rows - 1 : 0);
    if (_cursorRow == bottom) {
        scrollUp(_scrollTop, bottom, 1);
    } else if (_cursorRow + 1 < _rows) {
        ++_cursorRow;
    } else if (_rows) {
        scrollUp(0, _rows - 1, 1);
    }
}

void TerminalEmulator::carriageReturn()
{
    _cursorCol = 0;
    _wrapPending = false;
}

void TerminalEmulator::backspace()
{
    if (_cursorCol > 0) {
        --_cursorCol;
        _wrapPending = false;
    }
}

void TerminalEmulator::tab()
{
    size_t next = ((_cursorCol / 8) + 1) * 8;
    if (next >= _cols) {
        next = _cols - 1;
    }
    _cursorCol = next;
    _wrapPending = false;
}

void TerminalEmulator::putGlyph(const String& glyph, bool wide)
{
    if (_wrapPending) {
        carriageReturn();
        newline();
    }
    Cell& cell = mutableCell(_cursorCol, _cursorRow);
    cell.ch = glyph.length() ? glyph : " ";
    cell.fg = _fg;
    cell.bg = _bg;
    cell.bold = _bold;
    cell.inverse = _inverse;
    markDirtyWithNeighbors(_cursorCol, _cursorRow);

    if (wide && _cursorCol + 1 < _cols) {
        Cell& next = mutableCell(_cursorCol + 1, _cursorRow);
        next = BlankCell(_fg, _bg);
        next.inverse = _inverse;
        markDirtyWithNeighbors(_cursorCol + 1, _cursorRow);
    }

    size_t advance = wide ? 2 : 1;
    if (_cursorCol + advance >= _cols) {
        _cursorCol = _cols - 1;
        _wrapPending = true;
    } else {
        _cursorCol += advance;
    }
}

void TerminalEmulator::scrollUp(size_t top, size_t bottom, size_t count)
{
    if (top >= _rows || bottom >= _rows || top > bottom || count == 0) return;
    auto& buf = _alternate ? _alt : _main;
    count = std::min(count, bottom - top + 1);
    for (size_t row = top; row + count <= bottom; ++row) {
        for (size_t col = 0; col < _cols; ++col) {
            buf[row * _cols + col] = buf[(row + count) * _cols + col];
            markDirtyWithNeighbors(col, row);
        }
    }
    for (size_t row = bottom - count + 1; row <= bottom; ++row) {
        clearRow(row, 0, _cols - 1);
    }
}

void TerminalEmulator::clearRow(size_t row, size_t fromCol, size_t toCol)
{
    if (row >= _rows || fromCol >= _cols) return;
    toCol = std::min(toCol, _cols - 1);
    for (size_t col = fromCol; col <= toCol; ++col) {
        mutableCell(col, row) = BlankCell(_fg, _bg);
        markDirtyWithNeighbors(col, row);
    }
}

void TerminalEmulator::clearCells()
{
    auto& buf = _alternate ? _alt : _main;
    for (auto& cell : buf) {
        cell = BlankCell();
    }
}

void TerminalEmulator::scrollDown(size_t top, size_t bottom, size_t count)
{
    if (top >= _rows || bottom >= _rows || top > bottom || count == 0) return;
    auto& buf = _alternate ? _alt : _main;
    count = std::min(count, bottom - top + 1);
    for (size_t row = bottom + 1; row-- > top + count;) {
        for (size_t col = 0; col < _cols; ++col) {
            buf[row * _cols + col] = buf[(row - count) * _cols + col];
            markDirtyWithNeighbors(col, row);
        }
    }
    for (size_t row = top; row < top + count; ++row) {
        clearRow(row, 0, _cols - 1);
    }
}

void TerminalEmulator::insertCells(size_t count)
{
    if (_cursorRow >= _rows || _cursorCol >= _cols || count == 0) return;
    count = std::min(count, _cols - _cursorCol);
    for (size_t col = _cols; col-- > _cursorCol + count;) {
        mutableCell(col, _cursorRow) = mutableCell(col - count, _cursorRow);
        markDirtyWithNeighbors(col, _cursorRow);
    }
    for (size_t col = _cursorCol; col < _cursorCol + count; ++col) {
        mutableCell(col, _cursorRow) = BlankCell(_fg, _bg);
        markDirtyWithNeighbors(col, _cursorRow);
    }
}

void TerminalEmulator::deleteCells(size_t count)
{
    if (_cursorRow >= _rows || _cursorCol >= _cols || count == 0) return;
    count = std::min(count, _cols - _cursorCol);
    for (size_t col = _cursorCol; col + count < _cols; ++col) {
        mutableCell(col, _cursorRow) = mutableCell(col + count, _cursorRow);
        markDirtyWithNeighbors(col, _cursorRow);
    }
    for (size_t col = _cols - count; col < _cols; ++col) {
        mutableCell(col, _cursorRow) = BlankCell(_fg, _bg);
        markDirtyWithNeighbors(col, _cursorRow);
    }
}

void TerminalEmulator::insertLines(size_t count)
{
    size_t bottom = _scrollBottom ? _scrollBottom : (_rows ? _rows - 1 : 0);
    scrollDown(_cursorRow, bottom, count);
}

void TerminalEmulator::deleteLines(size_t count)
{
    size_t bottom = _scrollBottom ? _scrollBottom : (_rows ? _rows - 1 : 0);
    scrollUp(_cursorRow, bottom, count);
}

void TerminalEmulator::processByte(uint8_t c)
{
    if (_state == State::Utf8) {
        if (isUtf8Continuation(c)) {
            _utf8 += static_cast<char>(c);
            if (_utf8.length() >= _utf8Expected) {
                putGlyph(_utf8, true);
                _utf8 = "";
                _utf8Expected = 0;
                _state = State::Ground;
            }
        } else {
            _utf8 = "";
            _utf8Expected = 0;
            _state = State::Ground;
            processByte(c);
        }
        return;
    }
    if (_state == State::Escape) {
        processEscape(c);
        return;
    }
    if (_state == State::Charset) {
        _state = State::Ground;
        return;
    }
    if (_state == State::Csi) {
        processCsi(c);
        return;
    }
    if (_state == State::Osc) {
        if (c == '\a') {
            _state = State::Ground;
        } else if (c == 0x1B) {
            _state = State::Escape;
        }
        return;
    }

    if (c == 0x1B) {
        _state = State::Escape;
    } else if (c == '\r') {
        carriageReturn();
    } else if (c == '\n') {
        newline();
    } else if (c == '\b' || c == 0x7F) {
        backspace();
    } else if (c == '\t') {
        tab();
    } else if (c >= 0x20 && c < 0x80) {
        putGlyph(String(static_cast<char>(c)), false);
    } else if (c >= 0x80) {
        _utf8 = static_cast<char>(c);
        _utf8Expected = utf8Length(c);
        if (_utf8Expected <= 1) {
            putGlyph(_utf8, true);
            _utf8 = "";
        } else {
            _state = State::Utf8;
        }
    }
}

void TerminalEmulator::processEscape(uint8_t c)
{
    if (c == '[') {
        _csi = "";
        _params.clear();
        _state = State::Csi;
    } else if (c == ']') {
        _osc = "";
        _state = State::Osc;
    } else if (c == 'c') {
        reset();
    } else if (c == '7') {
        _savedCol = _cursorCol;
        _savedRow = _cursorRow;
        _state = State::Ground;
    } else if (c == '8') {
        setCursor(_savedRow, _savedCol);
        _state = State::Ground;
    } else if (c == '(' || c == ')' || c == '*' || c == '+') {
        _state = State::Charset;
    } else {
        _state = State::Ground;
    }
}

void TerminalEmulator::processCsi(uint8_t c)
{
    if (c >= 0x40 && c <= 0x7E) {
        parseParams();
        executeCsi(static_cast<char>(c));
        _state = State::Ground;
    } else {
        _csi += static_cast<char>(c);
    }
}

void TerminalEmulator::executeCsi(char command)
{
    switch (command) {
        case 'A':
            setCursor(_cursorRow > static_cast<size_t>(param(0, 1)) ? _cursorRow - param(0, 1) : 0, _cursorCol);
            break;
        case 'B':
            setCursor(std::min(_rows - 1, _cursorRow + static_cast<size_t>(param(0, 1))), _cursorCol);
            break;
        case 'C':
            setCursor(_cursorRow, std::min(_cols - 1, _cursorCol + static_cast<size_t>(param(0, 1))));
            break;
        case 'D':
            setCursor(_cursorRow, _cursorCol > static_cast<size_t>(param(0, 1)) ? _cursorCol - param(0, 1) : 0);
            break;
        case 'G':
            setCursor(_cursorRow, static_cast<size_t>(std::max(1, param(0, 1)) - 1));
            break;
        case '`':
            setCursor(_cursorRow, static_cast<size_t>(std::max(1, param(0, 1)) - 1));
            break;
        case 'H':
        case 'f':
            setCursor(static_cast<size_t>(std::max(1, param(0, 1)) - 1),
                      static_cast<size_t>(std::max(1, param(1, 1)) - 1));
            break;
        case 'd':
            setCursor(static_cast<size_t>(std::max(1, param(0, 1)) - 1), _cursorCol);
            break;
        case '@':
            insertCells(static_cast<size_t>(param(0, 1)));
            break;
        case 'P':
            deleteCells(static_cast<size_t>(param(0, 1)));
            break;
        case 'X':
            clearRow(_cursorRow, _cursorCol,
                     std::min(_cols - 1, _cursorCol + static_cast<size_t>(param(0, 1)) - 1));
            break;
        case 'L':
            insertLines(static_cast<size_t>(param(0, 1)));
            break;
        case 'M':
            deleteLines(static_cast<size_t>(param(0, 1)));
            break;
        case 'S':
            scrollUp(_scrollTop, _scrollBottom ? _scrollBottom : (_rows ? _rows - 1 : 0),
                     static_cast<size_t>(param(0, 1)));
            break;
        case 'T':
            scrollDown(_scrollTop, _scrollBottom ? _scrollBottom : (_rows ? _rows - 1 : 0),
                       static_cast<size_t>(param(0, 1)));
            break;
        case 'J': {
            int mode = param(0, 0);
            if (mode == 2 || mode == 3) {
                clearCells();
                setCursor(0, 0);
            } else if (mode == 0) {
                clearRow(_cursorRow, _cursorCol, _cols - 1);
                for (size_t row = _cursorRow + 1; row < _rows; ++row) clearRow(row, 0, _cols - 1);
            } else if (mode == 1) {
                for (size_t row = 0; row < _cursorRow; ++row) clearRow(row, 0, _cols - 1);
                clearRow(_cursorRow, 0, _cursorCol);
            }
            break;
        }
        case 'K': {
            int mode = param(0, 0);
            if (mode == 0) clearRow(_cursorRow, _cursorCol, _cols - 1);
            else if (mode == 1) clearRow(_cursorRow, 0, _cursorCol);
            else if (mode == 2) clearRow(_cursorRow, 0, _cols - 1);
            break;
        }
        case 'm':
            if (_params.empty()) _params.push_back(0);
            for (size_t i = 0; i < _params.size(); ++i) {
                int p = _params[i];
                if (p == 0) resetAttrs();
                else if (p == 1) _bold = true;
                else if (p == 7) _inverse = true;
                else if (p == 22) _bold = false;
                else if (p == 27) _inverse = false;
                else if (p >= 30 && p <= 37) _fg = static_cast<uint32_t>(p - 30);
                else if (p == 39) _fg = 7;
                else if (p >= 40 && p <= 47) _bg = static_cast<uint32_t>(p - 40);
                else if (p == 49) _bg = 0;
                else if (p >= 90 && p <= 97) _fg = static_cast<uint32_t>(p - 90 + 8);
                else if (p >= 100 && p <= 107) _bg = static_cast<uint32_t>(p - 100 + 8);
                else if ((p == 38 || p == 48) && i + 2 < _params.size() && _params[i + 1] == 5) {
                    uint32_t color = static_cast<uint32_t>(std::max(0, std::min(255, _params[i + 2])));
                    if (p == 38) _fg = color;
                    else _bg = color;
                    i += 2;
                } else if ((p == 38 || p == 48) && i + 4 < _params.size() && _params[i + 1] == 2) {
                    uint8_t r = static_cast<uint8_t>(std::max(0, std::min(255, _params[i + 2])));
                    uint8_t g = static_cast<uint8_t>(std::max(0, std::min(255, _params[i + 3])));
                    uint8_t b = static_cast<uint8_t>(std::max(0, std::min(255, _params[i + 4])));
                    uint32_t color = directColor(r, g, b);
                    if (p == 38) _fg = color;
                    else _bg = color;
                    i += 4;
                }
            }
            break;
        case 'r':
            _scrollTop = static_cast<size_t>(std::max(1, param(0, 1)) - 1);
            _scrollBottom = static_cast<size_t>(std::max(1, param(1, static_cast<int>(_rows))) - 1);
            if (_scrollTop >= _rows || _scrollBottom >= _rows || _scrollTop > _scrollBottom) {
                _scrollTop = 0;
                _scrollBottom = _rows ? _rows - 1 : 0;
            }
            setCursor(0, 0);
            break;
        case 'h':
            if (_csi.startsWith("?")) {
                for (int p : _params) {
                    if (p == 25) _cursorVisible = true;
                    if (p == 1049 || p == 47 || p == 1047) useAlternate(true);
                }
            }
            break;
        case 'l':
            if (_csi.startsWith("?")) {
                for (int p : _params) {
                    if (p == 25) _cursorVisible = false;
                    if (p == 1049 || p == 47 || p == 1047) useAlternate(false);
                }
            }
            break;
        case 's':
            _savedCol = _cursorCol;
            _savedRow = _cursorRow;
            break;
        case 'u':
            setCursor(_savedRow, _savedCol);
            break;
        default:
            break;
    }
    _wrapPending = false;
}

int TerminalEmulator::param(size_t index, int fallback) const
{
    if (index >= _params.size() || _params[index] <= 0) {
        return fallback;
    }
    return _params[index];
}

void TerminalEmulator::parseParams()
{
    _params.clear();
    String s = _csi;
    s.replace("?", "");
    s.replace(">", "");
    size_t start = 0;
    while (start <= s.length()) {
        int semi = s.indexOf(';', start);
        size_t end = semi < 0 ? s.length() : static_cast<size_t>(semi);
        String part = s.substring(start, end);
        _params.push_back(part.length() ? atoi(part.c_str()) : 0);
        if (semi < 0) break;
        start = static_cast<size_t>(semi) + 1;
    }
}

void TerminalEmulator::setCursor(size_t row, size_t col)
{
    markCursorDirty();
    _cursorRow = std::min(row, _rows ? _rows - 1 : 0);
    _cursorCol = std::min(col, _cols ? _cols - 1 : 0);
    _wrapPending = false;
    markCursorDirty();
}

void TerminalEmulator::markDirty(size_t col, size_t row)
{
    mutableCell(col, row).dirty = true;
}

void TerminalEmulator::markDirtyWithNeighbors(size_t col, size_t row)
{
    if (row >= _rows || col >= _cols) {
        return;
    }
    mutableCell(col, row).dirty = true;
    if (col > 0) {
        mutableCell(col - 1, row).dirty = true;
    }
    if (col + 1 < _cols) {
        mutableCell(col + 1, row).dirty = true;
    }
}

TerminalEmulator::Cell& TerminalEmulator::mutableCell(size_t col, size_t row)
{
    auto& buf = _alternate ? _alt : _main;
    return buf[row * _cols + col];
}
