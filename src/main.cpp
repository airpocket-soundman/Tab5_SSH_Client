#include "SettingsStore.hpp"
#include "SshClient.hpp"
#include "Tab5KeyboardInput.hpp"
#include "TerminalBuffer.hpp"
#include "TerminalEmulator.hpp"
#include "WifiProfiles.hpp"

#include <M5Unified.h>
#include <WiFi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <esp_system.h>

namespace {
AppConfig config;
SettingsStore settings;
WifiProfiles wifiProfiles;
SshClient ssh;
Tab5KeyboardInput keyboard;
TerminalBuffer terminal(2500);
TerminalEmulator vt;
M5Canvas screenSprite(&M5.Display);
bool screenSpriteReady = false;

enum class Screen : uint8_t {
    Terminal,
    WifiList,
    WifiEdit,
    WifiScan,
    SshList,
    SshEdit,
    FontList,
};

enum class WifiConnectState : uint8_t {
    Idle,
    Connecting,
    Connected,
    Failed,
};

struct Rect {
    int x;
    int y;
    int w;
    int h;

    bool contains(int px, int py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct ScannedNetwork {
    String ssid;
    int32_t rssi;
    wifi_auth_mode_t auth;
};

Screen screen = Screen::Terminal;
size_t activeWifi = 0;
size_t activeSsh = 0;
size_t editIndex = 0;
uint8_t editField = 0;
bool editIsNew = false;
uint32_t lastDraw = 0;
bool dirty = true;
bool headerDirty = false;
String statusLine = "booting";
std::vector<ScannedNetwork> scannedNetworks;
size_t settingScrollOffset = 0;
bool keyboardMenuMode = false;
size_t focusedHeaderButton = 0;
size_t focusedContentItem = 0;
bool wifiScanActive = false;
WifiConnectState wifiState = WifiConnectState::Idle;
size_t wifiProfileIndex = 0;
uint32_t wifiAttemptStart = 0;
uint32_t wifiRetryAt = 0;
uint32_t wifiAttemptTimeoutMs = 20000;
int16_t wifiLastRetrySecond = -1;
uint8_t wifiLastDisconnectReason = 0;
String wifiLastDisconnectName;
String wifiLastFailureText;
String wifiStatusText = "Wi-Fi idle";
String wifiWorkerSsid;
String wifiWorkerPassword;
volatile bool wifiWorkerBusy = false;
volatile bool wifiWorkerDone = false;
bool wifiDirectBeginPending = false;
bool wifiPinsConfigured = false;
String serialCommand;
String commandLine;
size_t commandCursor = 0;
std::vector<String> commandHistory;
size_t commandHistoryIndex = 0;
bool remoteLineMode = false;
uint32_t lastCursorBlink = 0;
bool cursorVisible = true;
RTC_DATA_ATTR uint32_t crashStageMagic = 0;
RTC_DATA_ATTR char crashStage[64] = "";

constexpr bool ForceFixedWifiForTest = false;
constexpr const char* FixedWifiSsid = "kumakero2.4";
constexpr const char* FixedWifiPassword = "4roses6126";
constexpr const char* LocalPrompt = "[tab5]  ";

constexpr int HeaderH = 44;
constexpr int HeaderTouchH = HeaderH * 3;
constexpr int ContentTopGap = 30;

constexpr Rect BtnTerminal{4, 4, 72, 36};
constexpr Rect BtnWifi{82, 4, 72, 36};
constexpr Rect BtnSsh{160, 4, 72, 36};
constexpr Rect BtnFont{238, 4, 72, 36};
constexpr Rect BtnConnect{316, 4, 80, 36};
constexpr Rect BtnAdd{402, 4, 72, 36};
constexpr Rect BtnMinus{480, 4, 72, 36};
constexpr Rect BtnPlus{558, 4, 72, 36};
constexpr Rect BtnSave{636, 4, 72, 36};
constexpr Rect BtnDelete{714, 4, 72, 36};
constexpr Rect BodyBtn1{8, HeaderH + ContentTopGap, 112, 48};
constexpr Rect BodyBtn2{132, HeaderH + ContentTopGap, 112, 48};
constexpr Rect BodyBtn3{256, HeaderH + ContentTopGap, 150, 48};
constexpr Rect FontMinusBtn{8, HeaderH + ContentTopGap, 112, 48};
constexpr Rect FontPlusBtn{132, HeaderH + ContentTopGap, 112, 48};
constexpr Rect FontSaveBtn{256, HeaderH + ContentTopGap, 150, 48};

bool connectSshProfile(const SshProfile& profile);
bool parseSshCommand(const String& line, SshProfile& profile, String& error);

bool headerButtonContains(const Rect& r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= 0 && py < HeaderTouchH;
}

const Rect* headerButtonAt(size_t index)
{
    size_t i = 0;
    if (index == i++) return &BtnTerminal;
    if (index == i++) return &BtnWifi;
    if (index == i++) return &BtnSsh;
    if (index == i++) return &BtnFont;

    if (screen == Screen::Terminal) {
        if (index == i++) return &BtnConnect;
    } else if (screen == Screen::WifiEdit) {
        if (index == i++) return &BtnConnect;
        if (index == i++) return &BtnSave;
        if (index == i++) return &BtnDelete;
    } else if (screen == Screen::SshEdit) {
        if (index == i++) return &BtnSave;
        if (index == i++) return &BtnDelete;
    }
    return nullptr;
}

size_t headerButtonCount()
{
    size_t count = 0;
    while (headerButtonAt(count) != nullptr) {
        ++count;
    }
    return count;
}

void clampFocusedHeaderButton()
{
    const size_t count = headerButtonCount();
    if (!count) {
        focusedHeaderButton = 0;
    } else if (focusedHeaderButton >= count) {
        focusedHeaderButton = count - 1;
    }
}

void focusCurrentScreenButton()
{
    if (screen == Screen::WifiList || screen == Screen::WifiEdit || screen == Screen::WifiScan) {
        focusedHeaderButton = 1;
    } else if (screen == Screen::SshList || screen == Screen::SshEdit) {
        focusedHeaderButton = 2;
    } else if (screen == Screen::FontList) {
        focusedHeaderButton = 3;
    } else {
        focusedHeaderButton = 0;
    }
    clampFocusedHeaderButton();
}

struct TerminalFontOption {
    const char* id;
    const char* label;
    const lgfx::IFont* font;
    const lgfx::IFont* japaneseFont;
    uint8_t cellW;
    uint8_t settingsLineHeight;
    uint8_t defaultLineStep;
    uint8_t legacyLineStep;
    uint8_t previousLineStep;
};

constexpr TerminalFontOption TerminalFonts[] = {
    {"mono9", "FreeMono 9pt", &fonts::FreeMono9pt7b, &fonts::lgfxJapanGothic_16, 11, 18, 20, 12, 23},
    {"mono12", "FreeMono 12pt", &fonts::FreeMono12pt7b, &fonts::lgfxJapanGothic_20, 14, 24, 26, 15, 30},
    {"mono18", "FreeMono 18pt", &fonts::FreeMono18pt7b, &fonts::lgfxJapanGothic_28, 21, 35, 38, 23, 44},
    {"mono24", "FreeMono 24pt", &fonts::FreeMono24pt7b, &fonts::lgfxJapanGothic_36, 28, 47, 50, 30, 59},
};

const TerminalFontOption& terminalFont()
{
    for (const auto& option : TerminalFonts) {
        if (config.keyboard.terminalFont == option.id) {
            return option;
        }
    }
    return TerminalFonts[1];
}

uint8_t terminalLineStep()
{
    uint8_t step = config.keyboard.terminalLineStep;
    uint8_t minStep = static_cast<uint8_t>(max<int>(12, terminalFont().settingsLineHeight + 2));
    if (step < minStep) {
        step = static_cast<uint8_t>(max<int>(terminalFont().defaultLineStep, minStep));
    }
    return step;
}

void setUiFont()
{
    screenSprite.setFont(&fonts::AsciiFont8x16);
    screenSprite.setTextSize(1);
}

void setTerminalFont()
{
    screenSprite.setFont(terminalFont().font);
    screenSprite.setTextSize(1);
}

int terminalCellWidth()
{
    setTerminalFont();
    int w = screenSprite.textWidth("M");
    static const char sample[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    for (size_t i = 0; i < strlen(sample); ++i) {
        char text[2] = {sample[i], 0};
        w = max<int>(w, screenSprite.textWidth(text));
    }
    return max<int>(terminalFont().cellW, w);
}

int terminalFontHeight(bool japanese)
{
    screenSprite.setFont(japanese ? terminalFont().japaneseFont : terminalFont().font);
    screenSprite.setTextSize(1);
    return screenSprite.fontHeight();
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t terminalColor(uint32_t color, bool bold)
{
    static const uint16_t normal[] = {
        TFT_BLACK, TFT_MAROON, TFT_DARKGREEN, TFT_OLIVE,
        TFT_NAVY, TFT_PURPLE, TFT_DARKCYAN, TFT_LIGHTGREY,
        TFT_DARKGREY, TFT_RED, TFT_GREEN, TFT_YELLOW,
        TFT_BLUE, TFT_MAGENTA, TFT_CYAN, TFT_WHITE
    };
    if (color & 0x01000000UL) {
        return static_cast<uint16_t>(color & 0xFFFF);
    }
    if (color < 16) {
        uint8_t index = static_cast<uint8_t>(color & 0x0F);
        if (bold && index < 8) {
            index += 8;
        }
        return normal[index];
    }
    if (color >= 16 && color <= 231) {
        uint32_t c = color - 16;
        uint8_t r = static_cast<uint8_t>(c / 36);
        uint8_t g = static_cast<uint8_t>((c / 6) % 6);
        uint8_t b = static_cast<uint8_t>(c % 6);
        auto level = [](uint8_t v) -> uint8_t {
            return v == 0 ? 0 : static_cast<uint8_t>(55 + v * 40);
        };
        return rgb565(level(r), level(g), level(b));
    }
    if (color >= 232 && color <= 255) {
        uint8_t level = static_cast<uint8_t>(8 + (color - 232) * 10);
        return rgb565(level, level, level);
    }
    uint8_t index = static_cast<uint8_t>(color & 0x0F);
    if (bold && index < 8) {
        index += 8;
    }
    return normal[index];
}

void migrateLegacyLineStep()
{
    const auto& font = terminalFont();
    int minStep = max(terminalFontHeight(false), terminalFontHeight(true));
    if (config.keyboard.terminalLineStep == font.legacyLineStep ||
        config.keyboard.terminalLineStep == font.previousLineStep ||
        config.keyboard.terminalLineStep < minStep) {
        config.keyboard.terminalLineStep = static_cast<uint8_t>(max<int>(font.defaultLineStep, minStep));
    }
}

void setTerminalFontForLine(const String& line)
{
    for (size_t i = 0; i < line.length(); ++i) {
        if (static_cast<uint8_t>(line[i]) >= 0x80) {
            screenSprite.setFont(terminalFont().japaneseFont);
            screenSprite.setTextSize(1);
            return;
        }
    }
    setTerminalFont();
}

void setSettingsFontForLine(const String& line = "")
{
    for (size_t i = 0; i < line.length(); ++i) {
        if (static_cast<uint8_t>(line[i]) >= 0x80) {
            screenSprite.setFont(terminalFont().japaneseFont);
            screenSprite.setTextSize(1);
            return;
        }
    }
    screenSprite.setFont(terminalFont().font);
    screenSprite.setTextSize(1);
}

int settingRowH()
{
    return max<int>(44, terminalFont().settingsLineHeight + 12);
}

int settingListTop()
{
    return BodyBtn1.y + BodyBtn1.h + 18;
}

bool hasJapaneseBytes(const String& text)
{
    for (size_t i = 0; i < text.length(); ++i) {
        if (static_cast<uint8_t>(text[i]) >= 0x80) {
            return true;
        }
    }
    return false;
}

uint8_t utf8CharLength(uint8_t c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

void drawMixedTerminalLine(const String& line, int x, int lineTop, int lineHeight)
{
    if (!hasJapaneseBytes(line)) {
        setTerminalFont();
        int y = lineTop + max<int>(0, (lineHeight - screenSprite.fontHeight()) / 2);
        screenSprite.drawString(line, x, y);
        return;
    }

    int cursorX = x;
    size_t i = 0;
    while (i < line.length()) {
        uint8_t c = static_cast<uint8_t>(line[i]);
        bool japaneseRun = c >= 0x80;
        size_t start = i;
        if (japaneseRun) {
            i += utf8CharLength(c);
            while (i < line.length() && static_cast<uint8_t>(line[i]) >= 0x80) {
                i += utf8CharLength(static_cast<uint8_t>(line[i]));
            }
            screenSprite.setFont(terminalFont().japaneseFont);
        } else {
            while (i < line.length() && static_cast<uint8_t>(line[i]) < 0x80) {
                ++i;
            }
            screenSprite.setFont(terminalFont().font);
        }
        screenSprite.setTextSize(1);
        String part = line.substring(start, i);
        int y = lineTop + max<int>(0, (lineHeight - screenSprite.fontHeight()) / 2);
        screenSprite.drawString(part, cursorX, y);
        cursorX += screenSprite.textWidth(part);
    }
}

void drawVtTerminal()
{
    const int cellW = terminalCellWidth();
    const int lineStep = terminalLineStep();
    for (size_t row = 0; row < vt.rows(); ++row) {
        int y = HeaderH + static_cast<int>(row) * lineStep;
        if (y >= screenSprite.height()) {
            break;
        }
        for (size_t col = 0; col < vt.columns(); ++col) {
            int x = 4 + static_cast<int>(col) * cellW;
            if (x >= screenSprite.width()) {
                break;
            }
            const auto& cell = vt.cell(col, row);
            bool cursor = vt.cursorVisible() && cursorVisible && col == vt.cursorColumn() && row == vt.cursorRow();
            if (!cell.dirty && !cursor) {
                continue;
            }
            bool inverse = cell.inverse ^ cursor;
            uint16_t fg = terminalColor(cell.fg, cell.bold);
            uint16_t bg = terminalColor(cell.bg, false);
            if (inverse) {
                std::swap(fg, bg);
            }
            screenSprite.fillRect(x, y, cellW, lineStep, bg);
            if (cell.ch != " ") {
                bool japanese = static_cast<uint8_t>(cell.ch[0]) >= 0x80;
                screenSprite.setFont(japanese ? terminalFont().japaneseFont : terminalFont().font);
                screenSprite.setTextSize(1);
                screenSprite.setTextColor(fg, bg);
                int textY = y + max<int>(0, (lineStep - screenSprite.fontHeight()) / 2);
                screenSprite.drawString(cell.ch, x, textY);
            }
        }
    }
    setTerminalFont();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    vt.clearDirty();
}

void resetCommandEditor()
{
    commandLine = "";
    commandCursor = 0;
    commandHistoryIndex = commandHistory.size();
    remoteLineMode = false;
    cursorVisible = true;
    lastCursorBlink = millis();
}

void clampCommandCursor()
{
    if (commandCursor > commandLine.length()) {
        commandCursor = commandLine.length();
    }
}

void insertCommandText(const String& text)
{
    if (!text.length()) {
        return;
    }
    commandLine = commandLine.substring(0, commandCursor) + text + commandLine.substring(commandCursor);
    commandCursor += text.length();
    cursorVisible = true;
    lastCursorBlink = millis();
    dirty = true;
}

void backspaceCommandText()
{
    if (commandCursor == 0 || !commandLine.length()) {
        return;
    }
    size_t pos = commandCursor - 1;
    while (pos > 0 && (static_cast<uint8_t>(commandLine[pos]) & 0xC0) == 0x80) {
        --pos;
    }
    commandLine = commandLine.substring(0, pos) + commandLine.substring(commandCursor);
    commandCursor = pos;
    cursorVisible = true;
    lastCursorBlink = millis();
    dirty = true;
}

void moveCommandCursor(int delta)
{
    if (delta < 0) {
        if (commandCursor == 0) {
            return;
        }
        --commandCursor;
        while (commandCursor > 0 && (static_cast<uint8_t>(commandLine[commandCursor]) & 0xC0) == 0x80) {
            --commandCursor;
        }
    } else if (delta > 0 && commandCursor < commandLine.length()) {
        commandCursor += utf8CharLength(static_cast<uint8_t>(commandLine[commandCursor]));
        clampCommandCursor();
    }
    cursorVisible = true;
    lastCursorBlink = millis();
    dirty = true;
}

void browseCommandHistory(int delta)
{
    if (commandHistory.empty()) {
        return;
    }
    if (delta < 0) {
        if (commandHistoryIndex == 0) {
            return;
        }
        --commandHistoryIndex;
    } else {
        if (commandHistoryIndex >= commandHistory.size()) {
            return;
        }
        ++commandHistoryIndex;
    }
    if (commandHistoryIndex < commandHistory.size()) {
        commandLine = commandHistory[commandHistoryIndex];
    } else {
        commandLine = "";
    }
    commandCursor = commandLine.length();
    cursorVisible = true;
    lastCursorBlink = millis();
    dirty = true;
}

void rememberCommandHistory(const String& line)
{
    if (!line.length()) {
        return;
    }
    if (commandHistory.empty() || commandHistory.back() != line) {
        commandHistory.push_back(line);
        if (commandHistory.size() > 50) {
            commandHistory.erase(commandHistory.begin());
        }
    }
    commandHistoryIndex = commandHistory.size();
}

void executeLocalCommand()
{
    String line = commandLine;
    line.trim();
    terminal.append(String(LocalPrompt) + line + "\n");
    rememberCommandHistory(line);
    resetCommandEditor();

    if (!line.length()) {
        dirty = true;
        return;
    }

    SshProfile directProfile;
    String error;
    if (parseSshCommand(line, directProfile, error)) {
        connectSshProfile(directProfile);
    } else {
        terminal.append(error + "\n");
    }
    dirty = true;
}

void sendCommandLine()
{
    if (!ssh.connected()) {
        executeLocalCommand();
        return;
    }
    String line = commandLine;
    String payload = line + "\n";
    ssh.write(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
    rememberCommandHistory(line);
    resetCommandEditor();
    dirty = true;
}

bool sendSshText(const String& text)
{
    if (!ssh.connected()) {
        return false;
    }
    return ssh.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

void replaceRemoteCommandLine(const String& line)
{
    if (!ssh.connected()) {
        return;
    }
    String payload;
    payload += static_cast<char>(0x01);  // Ctrl-A: beginning of line in readline shells.
    payload += static_cast<char>(0x0B);  // Ctrl-K: kill to end of line.
    payload += line;
    sendSshText(payload);
    commandLine = line;
    commandCursor = commandLine.length();
}

bool browseRemoteCommandHistory(int delta)
{
    if (vt.alternateScreen() || commandHistory.empty()) {
        return false;
    }
    if (delta < 0) {
        if (commandHistoryIndex == 0) {
            return true;
        }
        --commandHistoryIndex;
    } else {
        if (commandHistoryIndex >= commandHistory.size()) {
            return true;
        }
        ++commandHistoryIndex;
    }
    replaceRemoteCommandLine(commandHistoryIndex < commandHistory.size() ? commandHistory[commandHistoryIndex] : "");
    return true;
}

void trackRemoteCommandText(const String& text)
{
    if (vt.alternateScreen()) {
        return;
    }
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == '\r' || c == '\n') {
            String line = commandLine;
            line.trim();
            rememberCommandHistory(line);
            resetCommandEditor();
        } else if (c == 0x08 || c == 0x7F) {
            backspaceCommandText();
        } else if (c == '\t') {
            commandLine = "";
            commandCursor = 0;
            commandHistoryIndex = commandHistory.size();
        } else if (std::isprint(static_cast<unsigned char>(c))) {
            insertCommandText(String(c));
        }
    }
}

int settingTextY(int rowY)
{
    return rowY + max<int>(6, (settingRowH() - terminalFont().settingsLineHeight) / 2);
}

size_t visibleSettingRows()
{
    const int available = max<int>(1, screenSprite.height() - settingListTop());
    return max<size_t>(1, available / settingRowH());
}

size_t maxSettingOffset(size_t total)
{
    const size_t visible = visibleSettingRows();
    return total > visible ? total - visible : 0;
}

void clampSettingScroll(size_t total)
{
    settingScrollOffset = min(settingScrollOffset, maxSettingOffset(total));
}

void scrollSettingList(int delta, size_t total)
{
    const size_t maxOffset = maxSettingOffset(total);
    int next = static_cast<int>(settingScrollOffset) + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > static_cast<int>(maxOffset)) {
        next = static_cast<int>(maxOffset);
    }
    settingScrollOffset = static_cast<size_t>(next);
    dirty = true;
}

void drawButton(const Rect& r, const char* label, uint16_t fg = TFT_WHITE, uint16_t bg = TFT_DARKGREY)
{
    screenSprite.fillRect(r.x, r.y, r.w, r.h, bg);
    screenSprite.drawRect(r.x, r.y, r.w, r.h, TFT_LIGHTGREY);
    screenSprite.setTextColor(fg, bg);
    int textY = r.y + max<int>(2, (r.h - screenSprite.fontHeight()) / 2);
    screenSprite.drawString(label, r.x + 6, textY);
}

void drawFocusedBodyButton(const Rect& r, const char* label, size_t focusIndex, uint16_t bg = TFT_DARKGREY)
{
    bool focused = !keyboardMenuMode && focusedContentItem == focusIndex;
    drawButton(r, label, TFT_WHITE, focused ? TFT_NAVY : bg);
    if (focused) {
        screenSprite.drawRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, TFT_CYAN);
    }
}

void drawHeaderFocus()
{
    if (!keyboardMenuMode) {
        return;
    }
    clampFocusedHeaderButton();
    const Rect* r = headerButtonAt(focusedHeaderButton);
    if (!r) {
        return;
    }
    screenSprite.drawRect(r->x - 2, r->y - 2, r->w + 4, r->h + 4, TFT_YELLOW);
    screenSprite.drawRect(r->x - 3, r->y - 3, r->w + 6, r->h + 6, TFT_YELLOW);
}

void drawHeader()
{
    screenSprite.fillRect(0, 0, screenSprite.width(), HeaderH, TFT_DARKGREY);
    setUiFont();
    drawButton(BtnTerminal, "TERM");
    drawButton(BtnWifi, "WIFI");
    drawButton(BtnSsh, "SSH");
    drawButton(BtnFont, "FONT");

    if (screen == Screen::Terminal) {
        drawButton(BtnConnect, ssh.connected() ? "DISC" : "CONN", TFT_WHITE, ssh.connected() ? TFT_MAROON : TFT_DARKGREEN);
    } else if (screen == Screen::WifiEdit) {
        drawButton(BtnConnect, "CONN", TFT_WHITE, TFT_DARKGREEN);
        drawButton(BtnSave, "SAVE", TFT_WHITE, TFT_DARKGREEN);
        drawButton(BtnDelete, "DEL", TFT_WHITE, TFT_MAROON);
    } else if (screen == Screen::SshEdit) {
        drawButton(BtnSave, "SAVE", TFT_WHITE, TFT_DARKGREEN);
        drawButton(BtnDelete, "DEL", TFT_WHITE, TFT_MAROON);
    }

    screenSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    String status = wifiStatusText.length() ? wifiStatusText : statusLine;
    if (screen == Screen::FontList) {
        status = String("Font: ") + terminalFont().label + " line " + terminalLineStep();
    }
    if (status.length() > 42) {
        status = status.substring(0, 42);
    }
    int statusX = screenSprite.width() - screenSprite.textWidth(status) - 8;
    statusX = max<int>(540, statusX);
    screenSprite.drawString(status, statusX, 14);
    drawHeaderFocus();
}

void configureTerminal()
{
    const size_t columns = max<int>(20, (screenSprite.width() - 8) / terminalCellWidth());
    size_t rows = max<int>(5, (screenSprite.height() - HeaderH - 4) / terminalLineStep());
    terminal.setViewport(columns, rows);
    vt.resize(columns, rows);
    if (ssh.connected()) {
        ssh.resizePty(static_cast<int>(columns), static_cast<int>(rows));
    }
}

void appendStatus(const String& message)
{
    terminal.append("\n[tab5] ");
    terminal.append(message);
    terminal.append("\n");
    Serial.print("[tab5] ");
    Serial.println(message);
    statusLine = message;
    dirty = true;
}

bool saveConfig()
{
    config.activeWifi = activeWifi;
    config.activeSsh = activeSsh;
    if (!settings.save(config)) {
        appendStatus(String("Save failed: ") + settings.lastError());
        return false;
    }
    appendStatus("Profiles saved");
    return true;
}

void setWifiStatus(const String& message)
{
    wifiStatusText = message;
    headerDirty = true;
}

String wifiDisconnectSummary()
{
    String status = String("st") + static_cast<int>(WiFi.status());
    String passLen = ForceFixedWifiForTest ? String(" p") + String(FixedWifiPassword).length()
                                           : (wifiProfileIndex < config.wifi.size()
                                                  ? String(" p") + config.wifi[wifiProfileIndex].password.length()
                                                  : "");
    if (!wifiLastDisconnectReason) {
        return String("timeout ") + status + passLen;
    }
    String summary = String("r") + wifiLastDisconnectReason;
    if (wifiLastDisconnectName.length()) {
        summary += " ";
        summary += wifiLastDisconnectName;
    }
    summary += " ";
    summary += status;
    summary += passLen;
    return summary;
}

void setWifiFailureStatus(const String& prefix)
{
    wifiLastFailureText = prefix + " " + wifiDisconnectSummary();
    setWifiStatus(wifiLastFailureText);
    terminal.append(String("\n[tab5] ") + wifiLastFailureText + "\n");
}

void handleWifiEvent(arduino_event_id_t event, arduino_event_info_t info)
{
    if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        return;
    }
    wifiLastDisconnectReason = info.wifi_sta_disconnected.reason;
    const char* name = WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(wifiLastDisconnectReason));
    wifiLastDisconnectName = name ? name : "";
    Serial.printf("Wi-Fi disconnected reason=%u %s\n", wifiLastDisconnectReason, wifiLastDisconnectName.c_str());
}

void configureTab5WifiPins()
{
    if (wifiPinsConfigured) {
        return;
    }
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    if (M5.getBoard() == m5::board_t::board_M5Tab5) {
        WiFi.setPins(GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_10,
                     GPIO_NUM_9, GPIO_NUM_8, GPIO_NUM_15);
    }
#endif
    wifiPinsConfigured = true;
}

void wifiBeginTask(void*)
{
    configureTab5WifiPins();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false);
    delay(20);
    WiFi.begin(wifiWorkerSsid.c_str(), wifiWorkerPassword.c_str());
    wifiWorkerDone = true;
    wifiWorkerBusy = false;
    vTaskDelete(nullptr);
}

void beginWifiNow(const String& ssid, const String& password)
{
    configureTab5WifiPins();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false);
    delay(20);
    WiFi.begin(ssid.c_str(), password.c_str());
    wifiWorkerDone = true;
}

bool startWifiBeginWorker(const String& ssid, const String& password)
{
    if (ForceFixedWifiForTest) {
        wifiWorkerSsid = ssid;
        wifiWorkerPassword = password;
        wifiWorkerDone = false;
        wifiWorkerBusy = false;
        wifiDirectBeginPending = true;
        return true;
    }
    if (wifiWorkerBusy) {
        return false;
    }
    wifiWorkerSsid = ssid;
    wifiWorkerPassword = password;
    wifiWorkerDone = false;
    wifiWorkerBusy = true;
    BaseType_t ok = xTaskCreate(wifiBeginTask, "wifi-begin", 4096, nullptr, 1, nullptr);
    if (ok != pdPASS) {
        wifiWorkerBusy = false;
        wifiWorkerDone = false;
        return false;
    }
    return true;
}

void setCrashStage(const char* stage)
{
    crashStageMagic = 0x54414235;
    strlcpy(crashStage, stage, sizeof(crashStage));
}

void adjustTerminalLineStep(int delta)
{
    int next = static_cast<int>(config.keyboard.terminalLineStep) + delta;
    int minStep = max(12, max(terminalFontHeight(false), terminalFontHeight(true)));
    next = constrain(next, minStep, 80);
    if (next == config.keyboard.terminalLineStep) {
        return;
    }
    config.keyboard.terminalLineStep = static_cast<uint8_t>(next);
    configureTerminal();
    saveConfig();
    dirty = true;
}

void beginWifiAttempt(size_t index)
{
    wifiLastRetrySecond = -1;
    if (!ForceFixedWifiForTest && index >= config.wifi.size()) {
        wifiState = WifiConnectState::Failed;
        wifiRetryAt = millis() + 10000;
        String reason = wifiLastFailureText.length() ? wifiLastFailureText : String("WiFi fail ") + wifiDisconnectSummary();
        setWifiStatus(reason + "; retry 10s");
        return;
    }

    wifiProfileIndex = index;
    wifiState = WifiConnectState::Connecting;
    wifiAttemptStart = millis();
    wifiLastDisconnectReason = 0;
    wifiLastDisconnectName = "";
    wifiLastFailureText = "";
    String ssid = ForceFixedWifiForTest ? String(FixedWifiSsid) : config.wifi[wifiProfileIndex].ssid;
    String password = ForceFixedWifiForTest ? String(FixedWifiPassword) : config.wifi[wifiProfileIndex].password;
    if (!startWifiBeginWorker(ssid, password)) {
        setWifiFailureStatus(String("WiFi start fail ") + ssid);
        beginWifiAttempt(wifiProfileIndex + 1);
        return;
    }
    setWifiStatus(String("Wi-Fi direct: ") + ssid + " p" + password.length());
}

void startWifiReconnect(uint32_t timeoutMs = 20000)
{
    wifiAttemptTimeoutMs = timeoutMs;
    wifiLastRetrySecond = -1;
    if (!ForceFixedWifiForTest && config.wifi.empty()) {
        wifiState = WifiConnectState::Failed;
        wifiRetryAt = 0;
        setWifiStatus("Wi-Fi fail: no profile");
        return;
    }
    if (!ForceFixedWifiForTest && activeWifi >= config.wifi.size()) {
        activeWifi = 0;
    }
    setWifiStatus(ForceFixedWifiForTest ? "Wi-Fi direct test" : "Wi-Fi reconnect");
    beginWifiAttempt(ForceFixedWifiForTest ? 0 : activeWifi);
}

void pollWifi()
{
    if (wifiScanActive) {
        return;
    }

    if (wifiDirectBeginPending) {
        wifiDirectBeginPending = false;
        beginWifiNow(wifiWorkerSsid, wifiWorkerPassword);
    }

    if (wifiState == WifiConnectState::Connecting) {
        if (wifiWorkerDone) {
            wifiWorkerDone = false;
            setWifiStatus(String("Wi-Fi try: ") + config.wifi[wifiProfileIndex].ssid);
        }
        if (WiFi.status() == WL_CONNECTED) {
            wifiState = WifiConnectState::Connected;
            setWifiStatus(String("Wi-Fi connected: ") + WiFi.SSID() + " " + WiFi.localIP().toString());
            terminal.append(String("\n[tab5] ") + wifiStatusText + "\n");
            return;
        }
        if (millis() - wifiAttemptStart >= wifiAttemptTimeoutMs) {
            if (wifiWorkerBusy) {
                String ssid = ForceFixedWifiForTest ? String(FixedWifiSsid) : config.wifi[wifiProfileIndex].ssid;
                setWifiStatus(String("Wi-Fi start busy: ") + ssid);
                wifiAttemptStart = millis();
                return;
            }
            String ssid = ForceFixedWifiForTest ? String(FixedWifiSsid) : config.wifi[wifiProfileIndex].ssid;
            setWifiFailureStatus(String("WiFi fail ") + ssid);
            if (ForceFixedWifiForTest) {
                wifiState = WifiConnectState::Failed;
                wifiRetryAt = millis() + 10000;
                return;
            }
            beginWifiAttempt(wifiProfileIndex + 1);
        }
        return;
    }

    if (wifiState == WifiConnectState::Connected && WiFi.status() != WL_CONNECTED) {
        wifiState = WifiConnectState::Failed;
        wifiRetryAt = millis() + 3000;
        setWifiStatus("Wi-Fi lost; reconnect");
        return;
    }

    if (wifiState == WifiConnectState::Failed && config.wifi.size() && wifiRetryAt) {
        uint32_t now = millis();
        if (now >= wifiRetryAt) {
            wifiRetryAt = 0;
            wifiLastRetrySecond = -1;
            startWifiReconnect(wifiAttemptTimeoutMs);
        } else {
            int16_t remaining = static_cast<int16_t>((wifiRetryAt - now + 999) / 1000);
            if (remaining != wifiLastRetrySecond) {
                wifiLastRetrySecond = remaining;
                String reason = wifiLastFailureText.length() ? wifiLastFailureText : String("WiFi fail ") + wifiDisconnectSummary();
                setWifiStatus(reason + String("; retry ") + remaining + "s");
            }
        }
    }
}

void connectActiveSsh()
{
    if (config.ssh.empty()) {
        appendStatus("No SSH profiles");
        return;
    }
    if (activeSsh >= config.ssh.size()) {
        activeSsh = 0;
    }
    ssh.disconnect();
    String err;
    appendStatus(String("Connecting SSH: ") + config.ssh[activeSsh].host);
    setCrashStage("ssh.connect");
    if (ssh.connect(config.ssh[activeSsh], err, static_cast<int>(vt.columns()), static_cast<int>(vt.rows()))) {
        setCrashStage("ssh.connected");
        resetCommandEditor();
        vt.reset();
        configureTerminal();
        appendStatus("SSH connected");
        screen = Screen::Terminal;
    } else {
        setCrashStage("ssh.failed");
        appendStatus(String("SSH failed: ") + err);
    }
}

bool connectSshProfile(const SshProfile& profile)
{
    ssh.disconnect();
    String err;
    appendStatus(String("Connecting SSH: ") + profile.user + "@" + profile.host);
    setCrashStage("ssh.connect.direct");
    if (ssh.connect(profile, err, static_cast<int>(vt.columns()), static_cast<int>(vt.rows()))) {
        setCrashStage("ssh.connected");
        resetCommandEditor();
        vt.reset();
        configureTerminal();
        appendStatus("SSH connected");
        screen = Screen::Terminal;
        return true;
    }
    setCrashStage("ssh.failed");
    appendStatus(String("SSH failed: ") + err);
    return false;
}

void inheritSavedSshCredentials(SshProfile& profile)
{
    for (const auto& saved : config.ssh) {
        if (saved.host == profile.host && saved.user == profile.user && saved.port == profile.port) {
            profile.password = saved.password;
            profile.terminal = saved.terminal.length() ? saved.terminal : "xterm-256color";
            return;
        }
    }
    for (const auto& saved : config.ssh) {
        if (saved.host == profile.host && saved.user == profile.user) {
            profile.password = saved.password;
            profile.terminal = saved.terminal.length() ? saved.terminal : "xterm-256color";
            return;
        }
    }
}

bool parseSshCommand(const String& line, SshProfile& profile, String& error)
{
    String rest = line;
    rest.trim();
    if (!rest.startsWith("ssh ")) {
        error = "Command not found";
        return false;
    }
    rest = rest.substring(4);
    rest.trim();
    if (!rest.length()) {
        error = "Usage: ssh [-p port] user@host";
        return false;
    }

    profile = SshProfile{};
    profile.port = 22;
    profile.terminal = "xterm-256color";

    int pIndex = rest.indexOf("-p ");
    if (pIndex >= 0) {
        String before = rest.substring(0, pIndex);
        String after = rest.substring(pIndex + 3);
        before.trim();
        after.trim();
        int nextSpace = after.indexOf(' ');
        String portText = nextSpace >= 0 ? after.substring(0, nextSpace) : after;
        if (!portText.length() || portText.toInt() <= 0 || portText.toInt() > 65535) {
            error = "Invalid SSH port";
            return false;
        }
        profile.port = static_cast<uint16_t>(portText.toInt());
        String remaining = nextSpace >= 0 ? after.substring(nextSpace + 1) : "";
        remaining.trim();
        rest = before.length() ? before : remaining;
        rest.trim();
    }

    int space = rest.indexOf(' ');
    if (space >= 0) {
        rest = rest.substring(0, space);
    }
    int at = rest.indexOf('@');
    if (at <= 0 || at >= static_cast<int>(rest.length()) - 1) {
        error = "Usage: ssh [-p port] user@host";
        return false;
    }
    profile.user = rest.substring(0, at);
    profile.host = rest.substring(at + 1);
    int colon = profile.host.lastIndexOf(':');
    if (colon > 0 && colon < static_cast<int>(profile.host.length()) - 1) {
        String portText = profile.host.substring(colon + 1);
        bool numeric = true;
        for (size_t i = 0; i < portText.length(); ++i) {
            numeric = numeric && std::isdigit(static_cast<unsigned char>(portText[i]));
        }
        if (numeric) {
            int port = portText.toInt();
            if (port <= 0 || port > 65535) {
                error = "Invalid SSH port";
                return false;
            }
            profile.port = static_cast<uint16_t>(port);
            profile.host = profile.host.substring(0, colon);
        }
    }
    profile.name = String("direct ") + profile.user + "@" + profile.host;
    inheritSavedSshCredentials(profile);
    return true;
}

String safeValue(const String& value, bool secret = false)
{
    if (!secret) {
        return value;
    }
    String masked;
    for (size_t i = 0; i < value.length(); ++i) {
        masked += '*';
    }
    return masked;
}

String wifiFieldValue(uint8_t field)
{
    if (editIndex >= config.wifi.size()) {
        return "";
    }
    const auto& p = config.wifi[editIndex];
    if (field == 0) return p.name;
    if (field == 1) return p.ssid;
    return p.password;
}

void setWifiFieldValue(uint8_t field, const String& value)
{
    if (editIndex >= config.wifi.size()) {
        return;
    }
    auto& p = config.wifi[editIndex];
    if (field == 0) p.name = value;
    if (field == 1) p.ssid = value;
    if (field == 2) p.password = value;
}

String sshFieldValue(uint8_t field)
{
    if (editIndex >= config.ssh.size()) {
        return "";
    }
    const auto& p = config.ssh[editIndex];
    if (field == 0) return p.name;
    if (field == 1) return p.host;
    if (field == 2) return String(p.port);
    if (field == 3) return p.user;
    if (field == 4) return p.password;
    return p.terminal;
}

void setSshFieldValue(uint8_t field, const String& value)
{
    if (editIndex >= config.ssh.size()) {
        return;
    }
    auto& p = config.ssh[editIndex];
    if (field == 0) p.name = value;
    if (field == 1) p.host = value;
    if (field == 2) p.port = static_cast<uint16_t>(constrain(value.toInt(), 1, 65535));
    if (field == 3) p.user = value;
    if (field == 4) p.password = value;
    if (field == 5) p.terminal = value.length() ? value : "xterm-256color";
}

void drawTerminal()
{
    if (ssh.connected()) {
        drawVtTerminal();
        return;
    }
    setTerminalFont();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    const int lineStep = terminalLineStep();
    const bool drawEditor = terminal.atBottom();
    for (size_t row = 0; row < terminal.viewportRows(); ++row) {
        String line = terminal.lineAt(row);
        if (drawEditor && row + 1 == terminal.viewportRows()) {
            int lineTop = HeaderH + static_cast<int>(row) * lineStep;
            clampCommandCursor();
            String prefix = String(LocalPrompt) + commandLine.substring(0, commandCursor);
            String cursorGlyph = " ";
            String suffix = "";
            if (commandCursor < commandLine.length()) {
                size_t next = commandCursor + utf8CharLength(static_cast<uint8_t>(commandLine[commandCursor]));
                cursorGlyph = commandLine.substring(commandCursor, next);
                suffix = commandLine.substring(next);
            }

            size_t visibleStart = 0;
            while (visibleStart < prefix.length() &&
                   screenSprite.textWidth(prefix.substring(visibleStart)) > screenSprite.width() - 16) {
                ++visibleStart;
                while (visibleStart < prefix.length() &&
                       (static_cast<uint8_t>(prefix[visibleStart]) & 0xC0) == 0x80) {
                    ++visibleStart;
                }
            }

            int textY = lineTop + max<int>(0, (lineStep - screenSprite.fontHeight()) / 2);
            String visiblePrefix = prefix.substring(visibleStart);
            int x = 4;
            screenSprite.drawString(visiblePrefix, x, textY);
            x += screenSprite.textWidth(visiblePrefix);
            if (cursorVisible) {
                int cursorW = max<int>(terminalCellWidth(), screenSprite.textWidth(cursorGlyph));
                screenSprite.fillRect(x, lineTop, cursorW, lineStep, TFT_GREEN);
                screenSprite.setTextColor(TFT_BLACK, TFT_GREEN);
                screenSprite.drawString(cursorGlyph, x, textY);
                screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
                x += cursorW;
            } else {
                screenSprite.drawString(cursorGlyph, x, textY);
                x += screenSprite.textWidth(cursorGlyph);
            }
            if (suffix.length()) {
                screenSprite.drawString(suffix, x, textY);
            }
            continue;
        }
        if (line.length() > 0) {
            drawMixedTerminalLine(line, 4, HeaderH + static_cast<int>(row) * lineStep, lineStep);
        }
    }
}

void drawWifiList()
{
    clampSettingScroll(config.wifi.size());
    setSettingsFontForLine();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    drawFocusedBodyButton(BodyBtn1, "SCAN", 0);
    drawFocusedBodyButton(BodyBtn2, "ADD", 1);
    drawFocusedBodyButton(BodyBtn3, "CONNECT", 2, TFT_DARKGREEN);
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    screenSprite.drawString("Wi-Fi profiles", 428, HeaderH + 28);
    for (size_t row = 0; row < visibleSettingRows(); ++row) {
        size_t i = settingScrollOffset + row;
        if (i >= config.wifi.size()) {
            break;
        }
        int y = settingListTop() + static_cast<int>(row) * settingRowH();
        bool selected = !keyboardMenuMode && focusedContentItem == i + 3;
        uint16_t bg = selected ? TFT_NAVY : TFT_BLACK;
        screenSprite.fillRect(4, y, screenSprite.width() - 8, settingRowH() - 4, bg);
        screenSprite.drawRect(4, y, screenSprite.width() - 8, settingRowH() - 4, selected ? TFT_CYAN : TFT_DARKGREY);
        String line = String(i == activeWifi ? "* " : "  ") + config.wifi[i].name + " | " + config.wifi[i].ssid;
        setSettingsFontForLine(line);
        screenSprite.setTextColor(TFT_WHITE, bg);
        screenSprite.drawString(line, 12, settingTextY(y));
        setSettingsFontForLine();
        screenSprite.setTextColor(TFT_WHITE, bg);
        screenSprite.drawString(i == activeWifi ? "ACTIVE" : "SELECT", screenSprite.width() - 170, settingTextY(y));
    }
}

void drawWifiScan()
{
    clampSettingScroll(scannedNetworks.size());
    setSettingsFontForLine();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    drawFocusedBodyButton(BodyBtn1, "RESCAN", 0);
    drawFocusedBodyButton(BodyBtn2, "BACK", 1);
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    String title = wifiScanActive ? "Wi-Fi Scan: running..." : String("Wi-Fi Scan: ") + scannedNetworks.size() + " SSIDs";
    screenSprite.drawString(title, 240, HeaderH + 18);
    for (size_t row = 0; row < visibleSettingRows(); ++row) {
        size_t i = settingScrollOffset + row;
        if (i >= scannedNetworks.size()) {
            break;
        }
        int y = settingListTop() + static_cast<int>(row) * settingRowH();
        bool selected = !keyboardMenuMode && focusedContentItem == i + 2;
        uint16_t bg = selected ? TFT_NAVY : TFT_BLACK;
        screenSprite.fillRect(4, y, screenSprite.width() - 8, settingRowH() - 4, bg);
        screenSprite.drawRect(4, y, screenSprite.width() - 8, settingRowH() - 4, selected ? TFT_CYAN : TFT_DARKGREY);
        String security = scannedNetworks[i].auth == WIFI_AUTH_OPEN ? "open" : "secured";
        String line = scannedNetworks[i].ssid + " | " + scannedNetworks[i].rssi + " dBm | " + security;
        setSettingsFontForLine(line);
        screenSprite.setTextColor(TFT_WHITE, bg);
        screenSprite.drawString(line, 12, settingTextY(y));
    }
}

void drawSshList()
{
    clampSettingScroll(config.ssh.size());
    setSettingsFontForLine();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    drawFocusedBodyButton(BodyBtn1, "ADD", 0);
    drawFocusedBodyButton(BodyBtn2, "EDIT", 1);
    drawFocusedBodyButton(BodyBtn3, "CONNECT", 2, TFT_DARKGREEN);
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    screenSprite.drawString("SSH profiles", 428, HeaderH + 28);
    for (size_t row = 0; row < visibleSettingRows(); ++row) {
        size_t i = settingScrollOffset + row;
        if (i >= config.ssh.size()) {
            break;
        }
        int y = settingListTop() + static_cast<int>(row) * settingRowH();
        bool selected = !keyboardMenuMode && focusedContentItem == i + 3;
        uint16_t bg = selected ? TFT_NAVY : TFT_BLACK;
        screenSprite.fillRect(4, y, screenSprite.width() - 8, settingRowH() - 4, bg);
        screenSprite.drawRect(4, y, screenSprite.width() - 8, settingRowH() - 4, selected ? TFT_CYAN : TFT_DARKGREY);
        String line = String(i == activeSsh ? "* " : "  ") + config.ssh[i].name + " | " + config.ssh[i].user + "@" +
                      config.ssh[i].host + ":" + config.ssh[i].port;
        setSettingsFontForLine(line);
        screenSprite.setTextColor(TFT_WHITE, bg);
        screenSprite.drawString(line, 12, settingTextY(y));
        setSettingsFontForLine();
        screenSprite.setTextColor(TFT_WHITE, bg);
        screenSprite.drawString(i == activeSsh ? "ACTIVE" : "SELECT", screenSprite.width() - 170, settingTextY(y));
    }
}

void drawEditFields(const char* title, const char* const* labels, uint8_t count, bool sshFields)
{
    setSettingsFontForLine();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    screenSprite.drawString(title, 8, HeaderH + 8);
    for (uint8_t i = 0; i < count; ++i) {
        int y = settingListTop() + i * settingRowH();
        uint16_t bg = i == editField ? TFT_NAVY : TFT_BLACK;
        screenSprite.fillRect(4, y, screenSprite.width() - 8, settingRowH() - 4, bg);
        screenSprite.drawRect(4, y, screenSprite.width() - 8, settingRowH() - 4, i == editField ? TFT_CYAN : TFT_DARKGREY);
        screenSprite.setTextColor(TFT_WHITE, bg);
        setSettingsFontForLine(labels[i]);
        screenSprite.drawString(labels[i], 12, settingTextY(y));
        String value = sshFields ? sshFieldValue(i) : wifiFieldValue(i);
        bool secret = sshFields && i == 4;
        value = safeValue(value, secret);
        if (value.length() > 28) {
            value = value.substring(0, 28);
        }
        setSettingsFontForLine(value);
        screenSprite.drawString(value, 180, settingTextY(y));
    }
}

void drawFontList()
{
    setSettingsFontForLine();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    drawFocusedBodyButton(FontMinusBtn, "-", 0);
    drawFocusedBodyButton(FontPlusBtn, "+", 1);
    drawFocusedBodyButton(FontSaveBtn, "SAVE", 2, TFT_DARKGREEN);
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    screenSprite.drawString("Terminal font", 428, HeaderH + 16);
    screenSprite.drawString(String("Line step: ") + terminalLineStep() + " px", 428, HeaderH + 40);

    constexpr size_t fontCount = sizeof(TerminalFonts) / sizeof(TerminalFonts[0]);
    for (size_t i = 0; i < fontCount; ++i) {
        int y = settingListTop() + static_cast<int>(i) * settingRowH();
        bool selected = config.keyboard.terminalFont == TerminalFonts[i].id;
        bool focused = !keyboardMenuMode && focusedContentItem == i + 3;
        uint16_t bg = selected ? TFT_NAVY : TFT_BLACK;
        screenSprite.fillRect(4, y, screenSprite.width() - 8, settingRowH() - 4, bg);
        screenSprite.drawRect(4, y, screenSprite.width() - 8, settingRowH() - 4, focused ? TFT_CYAN : TFT_DARKGREY);
        screenSprite.setTextColor(TFT_WHITE, bg);
        String line = String(selected ? "* " : "  ") + TerminalFonts[i].label + " / Japanese auto" +
                      "  default line " + TerminalFonts[i].defaultLineStep + " px";
        setSettingsFontForLine(line);
        screenSprite.drawString(line, 12, settingTextY(y));
    }
}

void draw()
{
    screenSprite.startWrite();
    if (!(screen == Screen::Terminal && ssh.connected())) {
        screenSprite.fillScreen(TFT_BLACK);
    }
    setUiFont();
    drawHeader();

    if (screen == Screen::Terminal) {
        drawTerminal();
    } else if (screen == Screen::WifiList) {
        drawWifiList();
    } else if (screen == Screen::WifiScan) {
        drawWifiScan();
    } else if (screen == Screen::SshList) {
        drawSshList();
    } else if (screen == Screen::WifiEdit) {
        static const char* const labels[] = {"Name", "SSID", "Password"};
        drawEditFields("Edit Wi-Fi", labels, 3, false);
    } else if (screen == Screen::SshEdit) {
        static const char* const labels[] = {"Name", "Host", "Port", "User", "Password", "Term"};
        drawEditFields("Edit SSH", labels, 6, true);
    } else if (screen == Screen::FontList) {
        drawFontList();
    }

    screenSprite.endWrite();
    screenSprite.pushSprite(0, 0);
    dirty = false;
    headerDirty = false;
}

void drawHeaderOnly()
{
    screenSprite.startWrite();
    drawHeader();
    screenSprite.endWrite();
    screenSprite.pushSprite(0, 0);
    headerDirty = false;
}

void beginWifiEdit(size_t index, bool isNew)
{
    editIndex = index;
    editField = 0;
    editIsNew = isNew;
    screen = Screen::WifiEdit;
    dirty = true;
}

void beginSshEdit(size_t index, bool isNew)
{
    editIndex = index;
    editField = 0;
    editIsNew = isNew;
    screen = Screen::SshEdit;
    dirty = true;
}

void addProfile()
{
    if (screen == Screen::WifiList) {
        WifiProfile p;
        p.name = String("wifi-") + (config.wifi.size() + 1);
        p.ssid = "";
        p.password = "";
        config.wifi.push_back(p);
        activeWifi = config.wifi.size() - 1;
        beginWifiEdit(activeWifi, true);
    } else if (screen == Screen::SshList) {
        SshProfile p;
        p.name = String("ssh-") + (config.ssh.size() + 1);
        p.host = "";
        p.port = 22;
        p.user = "";
        p.password = "";
        p.terminal = "xterm-256color";
        config.ssh.push_back(p);
        activeSsh = config.ssh.size() - 1;
        beginSshEdit(activeSsh, true);
    }
}

int findWifiProfileBySsid(const String& ssid)
{
    for (size_t i = 0; i < config.wifi.size(); ++i) {
        if (config.wifi[i].ssid == ssid) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool scannedSsidExists(const String& ssid)
{
    for (const auto& network : scannedNetworks) {
        if (network.ssid == ssid) {
            return true;
        }
    }
    return false;
}

void scanWifiNetworks()
{
    if (wifiScanActive) {
        setWifiStatus("Wi-Fi scan running");
        dirty = true;
        return;
    }

    statusLine = "Scanning Wi-Fi...";
    setWifiStatus("Wi-Fi scanning");
    scannedNetworks.clear();
    screen = Screen::WifiScan;
    settingScrollOffset = 0;
    focusedContentItem = 0;
    wifiScanActive = true;
    wifiState = WifiConnectState::Idle;
    wifiRetryAt = 0;
    dirty = true;

    configureTab5WifiPins();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    WiFi.scanDelete();
    if (!WiFi.scanNetworks(true, true)) {
        wifiScanActive = false;
        appendStatus("Wi-Fi scan start failed");
        screen = Screen::WifiList;
    }
}

void finishWifiScan(int count)
{
    wifiScanActive = false;
    scannedNetworks.clear();
    if (count < 0) {
        appendStatus("Wi-Fi scan failed");
        screen = Screen::WifiList;
        return;
    }

    for (int i = 0; i < count; ++i) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length() || scannedSsidExists(ssid)) {
            continue;
        }
        scannedNetworks.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i)});
    }
    WiFi.scanDelete();

    std::sort(scannedNetworks.begin(), scannedNetworks.end(), [](const ScannedNetwork& a, const ScannedNetwork& b) {
        return a.rssi > b.rssi;
    });

    statusLine = String("Wi-Fi scan: ") + scannedNetworks.size() + " SSIDs";
    setWifiStatus(statusLine);
    screen = Screen::WifiScan;
    settingScrollOffset = 0;
    focusedContentItem = 2;
    dirty = true;
}

void pollWifiScan()
{
    if (!wifiScanActive) {
        return;
    }

    int count = WiFi.scanComplete();
    if (count == -1) {
        return;
    }
    finishWifiScan(count);
}

void selectScannedWifi(size_t index)
{
    if (index >= scannedNetworks.size()) {
        return;
    }

    int existing = findWifiProfileBySsid(scannedNetworks[index].ssid);
    if (existing >= 0) {
        activeWifi = static_cast<size_t>(existing);
        beginWifiEdit(activeWifi, false);
    } else {
        WifiProfile p;
        p.ssid = scannedNetworks[index].ssid;
        p.name = p.ssid.length() ? p.ssid : String("wifi-") + (config.wifi.size() + 1);
        p.password = "";
        config.wifi.push_back(p);
        activeWifi = config.wifi.size() - 1;
        beginWifiEdit(activeWifi, true);
    }
    editField = 2;
    dirty = true;
}

void deleteEditingProfile()
{
    if (screen == Screen::WifiEdit && editIndex < config.wifi.size()) {
        config.wifi.erase(config.wifi.begin() + editIndex);
        if (activeWifi >= config.wifi.size()) {
            activeWifi = 0;
        }
        saveConfig();
        screen = Screen::WifiList;
    } else if (screen == Screen::SshEdit && editIndex < config.ssh.size()) {
        config.ssh.erase(config.ssh.begin() + editIndex);
        if (activeSsh >= config.ssh.size()) {
            activeSsh = 0;
        }
        saveConfig();
        screen = Screen::SshList;
    }
    dirty = true;
}

void saveEditingProfile()
{
    (void)editIsNew;
    if (saveConfig()) {
        if (screen == Screen::WifiEdit) {
            screen = Screen::WifiList;
        } else if (screen == Screen::SshEdit) {
            screen = Screen::SshList;
        }
    }
    dirty = true;
}

bool handleHeaderTouch(int x, int y)
{
    keyboardMenuMode = false;
    if (headerButtonContains(BtnTerminal, x, y)) {
        screen = Screen::Terminal;
        dirty = true;
    } else if (headerButtonContains(BtnWifi, x, y)) {
        screen = Screen::WifiList;
        settingScrollOffset = 0;
        focusedContentItem = 0;
        dirty = true;
    } else if (headerButtonContains(BtnSsh, x, y)) {
        screen = Screen::SshList;
        settingScrollOffset = 0;
        focusedContentItem = 0;
        dirty = true;
    } else if (headerButtonContains(BtnFont, x, y)) {
        screen = Screen::FontList;
        settingScrollOffset = 0;
        focusedContentItem = 0;
        dirty = true;
    } else if (screen == Screen::Terminal && headerButtonContains(BtnConnect, x, y)) {
        if (ssh.connected()) {
            ssh.disconnect();
            resetCommandEditor();
            configureTerminal();
            appendStatus("SSH disconnected");
        } else {
            connectActiveSsh();
        }
    } else if (screen == Screen::WifiEdit && headerButtonContains(BtnConnect, x, y)) {
        if (saveConfig()) {
            startWifiReconnect(20000);
        }
        dirty = true;
    } else if ((screen == Screen::WifiEdit || screen == Screen::SshEdit) && headerButtonContains(BtnSave, x, y)) {
        saveEditingProfile();
    } else if ((screen == Screen::WifiEdit || screen == Screen::SshEdit) && headerButtonContains(BtnDelete, x, y)) {
        deleteEditingProfile();
    } else {
        return false;
    }
    return true;
}

void executeFocusedHeaderButton()
{
    clampFocusedHeaderButton();
    const Rect* r = headerButtonAt(focusedHeaderButton);
    if (!r) {
        return;
    }
    handleHeaderTouch(r->x + r->w / 2, HeaderH / 2);
    keyboardMenuMode = false;
    clampFocusedHeaderButton();
    dirty = true;
}

bool isLeftKey(const KeyAction& action)
{
    return action.type == KeyActionType::Text && action.text == "\x1B[D";
}

bool isRightKey(const KeyAction& action)
{
    return action.type == KeyActionType::Text && action.text == "\x1B[C";
}

bool isUpKey(const KeyAction& action)
{
    return action.type == KeyActionType::Text && action.text == "\x1B[A";
}

bool isDownKey(const KeyAction& action)
{
    return action.type == KeyActionType::Text && action.text == "\x1B[B";
}

bool isEnterKey(const KeyAction& action)
{
    return action.type == KeyActionType::Text && (action.text == "\r" || action.text == "\n");
}

bool isTabKey(const KeyAction& action)
{
    return action.type == KeyActionType::Text && action.text == "\t";
}

bool handleKeyboardMenuAction(const KeyAction& action)
{
    if (action.type == KeyActionType::Menu) {
        if (!keyboardMenuMode) {
            focusCurrentScreenButton();
            keyboardMenuMode = true;
        } else {
            keyboardMenuMode = false;
        }
        dirty = true;
        return true;
    }

    if (!keyboardMenuMode) {
        return false;
    }

    const size_t count = headerButtonCount();
    if (!count) {
        return true;
    }
    if (isLeftKey(action) || isUpKey(action)) {
        focusedHeaderButton = focusedHeaderButton == 0 ? count - 1 : focusedHeaderButton - 1;
        dirty = true;
        return true;
    }
    if (isRightKey(action) || isDownKey(action) || isTabKey(action)) {
        focusedHeaderButton = (focusedHeaderButton + 1) % count;
        dirty = true;
        return true;
    }
    if (action.type == KeyActionType::Scroll) {
        focusedHeaderButton = action.value > 0 ? (focusedHeaderButton == 0 ? count - 1 : focusedHeaderButton - 1)
                                               : (focusedHeaderButton + 1) % count;
        dirty = true;
        return true;
    }
    if (isEnterKey(action)) {
        executeFocusedHeaderButton();
        return true;
    }
    return true;
}

void handleListTouch(int x, int y)
{
    if (screen == Screen::WifiList && y < settingListTop()) {
        if (BodyBtn1.contains(x, y)) {
            focusedContentItem = 0;
            scanWifiNetworks();
        } else if (BodyBtn2.contains(x, y)) {
            focusedContentItem = 1;
            addProfile();
        } else if (BodyBtn3.contains(x, y)) {
            focusedContentItem = 2;
            saveConfig();
            startWifiReconnect(20000);
        }
        return;
    }

    if (screen == Screen::WifiScan && y < settingListTop()) {
        if (BodyBtn1.contains(x, y)) {
            focusedContentItem = 0;
            scanWifiNetworks();
        } else if (BodyBtn2.contains(x, y)) {
            focusedContentItem = 1;
            screen = Screen::WifiList;
            settingScrollOffset = 0;
            dirty = true;
        }
        return;
    }

    if (screen == Screen::SshList && y < settingListTop()) {
        if (BodyBtn1.contains(x, y)) {
            focusedContentItem = 0;
            addProfile();
        } else if (BodyBtn2.contains(x, y)) {
            focusedContentItem = 1;
            if (activeSsh < config.ssh.size()) {
                beginSshEdit(activeSsh, false);
            }
        } else if (BodyBtn3.contains(x, y)) {
            focusedContentItem = 2;
            connectActiveSsh();
        }
        return;
    }

    if (screen == Screen::FontList) {
        if (y < settingListTop()) {
            if (FontMinusBtn.contains(x, y)) {
                focusedContentItem = 0;
                adjustTerminalLineStep(-1);
            } else if (FontPlusBtn.contains(x, y)) {
                focusedContentItem = 1;
                adjustTerminalLineStep(1);
            } else if (FontSaveBtn.contains(x, y)) {
                focusedContentItem = 2;
                configureTerminal();
                saveConfig();
            }
            return;
        }
        constexpr size_t fontCount = sizeof(TerminalFonts) / sizeof(TerminalFonts[0]);
        const int rowH = settingRowH();
        size_t index = static_cast<size_t>((y - settingListTop()) / rowH);
        if (index < fontCount) {
            focusedContentItem = index + 3;
            config.keyboard.terminalFont = TerminalFonts[index].id;
            config.keyboard.terminalLineStep =
                max<uint8_t>(TerminalFonts[index].defaultLineStep, max(terminalFontHeight(false), terminalFontHeight(true)));
            configureTerminal();
            saveConfig();
            dirty = true;
        }
        return;
    }

    if (y < settingListTop()) {
        return;
    }
    size_t index = settingScrollOffset + static_cast<size_t>((y - settingListTop()) / settingRowH());
    if (screen == Screen::WifiList && index < config.wifi.size()) {
        if (x > M5.Display.width() - 220) {
            activeWifi = index;
            appendStatus(String("Active Wi-Fi: ") + config.wifi[activeWifi].name);
            saveConfig();
        } else {
            focusedContentItem = index + 3;
            activeWifi = index;
            appendStatus(String("Active Wi-Fi: ") + config.wifi[activeWifi].name);
            saveConfig();
            dirty = true;
        }
    } else if (screen == Screen::WifiScan && index < scannedNetworks.size()) {
        focusedContentItem = index + 2;
        selectScannedWifi(index);
    } else if (screen == Screen::SshList && index < config.ssh.size()) {
        if (x > M5.Display.width() - 220) {
            activeSsh = index;
            appendStatus(String("Active SSH: ") + config.ssh[activeSsh].name);
            saveConfig();
        } else {
            focusedContentItem = index + 3;
            activeSsh = index;
            appendStatus(String("Active SSH: ") + config.ssh[activeSsh].name);
            saveConfig();
            dirty = true;
        }
    }
}

void editFocusedSshProfile()
{
    if (focusedContentItem >= 3) {
        size_t index = focusedContentItem - 3;
        if (index < config.ssh.size()) {
            beginSshEdit(index, false);
            return;
        }
    }
    if (activeSsh < config.ssh.size()) {
        beginSshEdit(activeSsh, false);
    }
}

void editFocusedWifiProfile()
{
    if (focusedContentItem >= 3) {
        size_t index = focusedContentItem - 3;
        if (index < config.wifi.size()) {
            beginWifiEdit(index, false);
            return;
        }
    }
    if (activeWifi < config.wifi.size()) {
        beginWifiEdit(activeWifi, false);
    }
}

void selectFocusedWifiProfile()
{
    if (focusedContentItem < 3) {
        return;
    }
    size_t index = focusedContentItem - 3;
    if (index < config.wifi.size()) {
        activeWifi = index;
        appendStatus(String("Active Wi-Fi: ") + config.wifi[activeWifi].name);
        saveConfig();
        dirty = true;
    }
}

void selectFocusedSshProfile()
{
    if (focusedContentItem < 3) {
        return;
    }
    size_t index = focusedContentItem - 3;
    if (index < config.ssh.size()) {
        activeSsh = index;
        appendStatus(String("Active SSH: ") + config.ssh[activeSsh].name);
        saveConfig();
        dirty = true;
    }
}

void handleEditTouch(int, int y)
{
    if (y < settingListTop()) {
        return;
    }
    uint8_t field = static_cast<uint8_t>((y - settingListTop()) / settingRowH());
    uint8_t maxField = screen == Screen::WifiEdit ? 3 : 6;
    if (field < maxField) {
        editField = field;
        dirty = true;
    }
}

void handleTouch()
{
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) {
        return;
    }

    int x = touch.x;
    int y = touch.y;
    const int activeHeaderTouchH = screen == Screen::Terminal ? HeaderTouchH : HeaderH + 20;
    if (y < activeHeaderTouchH && handleHeaderTouch(x, y)) {
        return;
    } else if (screen == Screen::WifiList || screen == Screen::WifiScan || screen == Screen::SshList ||
               screen == Screen::FontList) {
        handleListTouch(x, y);
    } else if (screen == Screen::WifiEdit || screen == Screen::SshEdit) {
        handleEditTouch(x, y);
    }
}

void editAppendChar(char c)
{
    if (screen != Screen::WifiEdit && screen != Screen::SshEdit) {
        return;
    }

    const bool isSsh = screen == Screen::SshEdit;
    const uint8_t maxField = isSsh ? 6 : 3;
    if (c == '\r' || c == '\n') {
        saveEditingProfile();
        return;
    }
    if (c == '\t') {
        editField = (editField + 1) % maxField;
        dirty = true;
        return;
    }

    String value = isSsh ? sshFieldValue(editField) : wifiFieldValue(editField);
    if (c == 0x08 || c == 0x7F) {
        if (value.length()) {
            value.remove(value.length() - 1);
        }
    } else if (std::isprint(static_cast<unsigned char>(c))) {
        if (!(isSsh && editField == 2 && !std::isdigit(static_cast<unsigned char>(c)))) {
            value += c;
        }
    }

    if (isSsh) {
        setSshFieldValue(editField, value);
    } else {
        setWifiFieldValue(editField, value);
    }
    dirty = true;
}

void moveEditField(int delta)
{
    if (screen != Screen::WifiEdit && screen != Screen::SshEdit) {
        return;
    }
    const uint8_t maxField = screen == Screen::WifiEdit ? 3 : 6;
    int next = static_cast<int>(editField) + delta;
    if (next < 0) {
        next = maxField - 1;
    } else if (next >= maxField) {
        next = 0;
    }
    editField = static_cast<uint8_t>(next);
    dirty = true;
}

size_t wifiContentCount()
{
    if (screen == Screen::WifiList) {
        return 3 + config.wifi.size();
    }
    if (screen == Screen::WifiScan) {
        return 2 + scannedNetworks.size();
    }
    if (screen == Screen::SshList) {
        return 3 + config.ssh.size();
    }
    if (screen == Screen::FontList) {
        return 3 + (sizeof(TerminalFonts) / sizeof(TerminalFonts[0]));
    }
    return 0;
}

void clampWifiContentFocus()
{
    size_t count = wifiContentCount();
    if (!count) {
        focusedContentItem = 0;
        return;
    }
    if (focusedContentItem >= count) {
        focusedContentItem = count - 1;
    }
}

void ensureWifiFocusedRowVisible()
{
    size_t firstRowFocus = screen == Screen::WifiScan ? 2 : 3;
    if (focusedContentItem < firstRowFocus) {
        return;
    }
    size_t rowIndex = focusedContentItem - firstRowFocus;
    size_t visible = visibleSettingRows();
    if (rowIndex < settingScrollOffset) {
        settingScrollOffset = rowIndex;
    } else if (rowIndex >= settingScrollOffset + visible) {
        settingScrollOffset = rowIndex - visible + 1;
    }
}

void moveWifiContentFocus(int delta)
{
    size_t count = wifiContentCount();
    if (!count) {
        return;
    }
    int next = static_cast<int>(focusedContentItem) + delta;
    if (next < 0) {
        next = count - 1;
    } else if (next >= static_cast<int>(count)) {
        next = 0;
    }
    focusedContentItem = static_cast<size_t>(next);
    ensureWifiFocusedRowVisible();
    dirty = true;
}

void executeWifiContentFocus()
{
    clampWifiContentFocus();
    if (screen == Screen::WifiList) {
        if (focusedContentItem == 0) {
            scanWifiNetworks();
        } else if (focusedContentItem == 1) {
            addProfile();
        } else if (focusedContentItem == 2) {
            saveConfig();
            startWifiReconnect(20000);
        } else {
            selectFocusedWifiProfile();
        }
    } else if (screen == Screen::WifiScan) {
        if (focusedContentItem == 0) {
            scanWifiNetworks();
        } else if (focusedContentItem == 1) {
            screen = Screen::WifiList;
            focusedContentItem = 0;
            settingScrollOffset = 0;
            dirty = true;
        } else {
            size_t index = focusedContentItem - 2;
            if (index < scannedNetworks.size()) {
                selectScannedWifi(index);
            }
        }
    } else if (screen == Screen::SshList) {
        if (focusedContentItem == 0) {
            addProfile();
        } else if (focusedContentItem == 1) {
            editFocusedSshProfile();
        } else if (focusedContentItem == 2) {
            connectActiveSsh();
        } else {
            selectFocusedSshProfile();
        }
    } else if (screen == Screen::FontList) {
        if (focusedContentItem == 0) {
            adjustTerminalLineStep(-1);
        } else if (focusedContentItem == 1) {
            adjustTerminalLineStep(1);
        } else if (focusedContentItem == 2) {
            configureTerminal();
            saveConfig();
        } else {
            constexpr size_t fontCount = sizeof(TerminalFonts) / sizeof(TerminalFonts[0]);
            size_t index = focusedContentItem - 3;
            if (index < fontCount) {
                config.keyboard.terminalFont = TerminalFonts[index].id;
                config.keyboard.terminalLineStep =
                    max<uint8_t>(TerminalFonts[index].defaultLineStep, max(terminalFontHeight(false), terminalFontHeight(true)));
                configureTerminal();
                saveConfig();
                dirty = true;
            }
        }
    }
}

bool handleWifiContentAction(const KeyAction& action)
{
    if (keyboardMenuMode) {
        return false;
    }
    if (screen != Screen::WifiList && screen != Screen::WifiScan && screen != Screen::SshList &&
        screen != Screen::FontList) {
        return false;
    }
    clampWifiContentFocus();
    if (isTabKey(action) || isRightKey(action) || isDownKey(action)) {
        moveWifiContentFocus(1);
        return true;
    }
    if (isLeftKey(action) || isUpKey(action)) {
        moveWifiContentFocus(-1);
        return true;
    }
    if (isEnterKey(action)) {
        executeWifiContentFocus();
        return true;
    }
    if (action.type == KeyActionType::Scroll) {
        moveWifiContentFocus(action.value > 0 ? -1 : 1);
        return true;
    }
    return false;
}

void handleDisconnectedTerminalText(const KeyAction& action)
{
    if (isEnterKey(action)) {
        executeLocalCommand();
        return;
    }
    if (isLeftKey(action)) {
        moveCommandCursor(-1);
        return;
    }
    if (isRightKey(action)) {
        moveCommandCursor(1);
        return;
    }
    if (isUpKey(action)) {
        browseCommandHistory(-1);
        return;
    }
    if (isDownKey(action)) {
        browseCommandHistory(1);
        return;
    }
    if (isTabKey(action)) {
        return;
    }

    for (size_t i = 0; i < action.text.length(); ++i) {
        char c = action.text[i];
        if (c == 0x08 || c == 0x7F) {
            backspaceCommandText();
        } else if (std::isprint(static_cast<unsigned char>(c))) {
            insertCommandText(String(c));
        }
    }
}

void handleTerminalAction(const KeyAction& action)
{
    switch (action.type) {
        case KeyActionType::Text:
            if (ssh.connected()) {
                if (isUpKey(action) && browseRemoteCommandHistory(-1)) {
                    dirty = true;
                    break;
                }
                if (isDownKey(action) && browseRemoteCommandHistory(1)) {
                    dirty = true;
                    break;
                }
                sendSshText(action.text);
                trackRemoteCommandText(action.text);
            } else {
                handleDisconnectedTerminalText(action);
            }
            dirty = true;
            break;
        case KeyActionType::Scroll:
            terminal.scroll(action.value);
            dirty = true;
            break;
        case KeyActionType::ConnectNext:
            if (!config.ssh.empty()) {
                activeSsh = (activeSsh + 1) % config.ssh.size();
                connectActiveSsh();
            }
            break;
        case KeyActionType::ConnectPrevious:
            if (!config.ssh.empty()) {
                activeSsh = activeSsh == 0 ? config.ssh.size() - 1 : activeSsh - 1;
                connectActiveSsh();
            }
            break;
        case KeyActionType::Menu:
            screen = Screen::WifiList;
            dirty = true;
            break;
        default:
            break;
    }
}

void handleAction(const KeyAction& action)
{
    if (action.type == KeyActionType::Menu && screen == Screen::Terminal && ssh.connected()) {
        sendSshText(String(static_cast<char>(0x1B)));
        dirty = true;
        return;
    }

    if (action.type == KeyActionType::Menu) {
        handleKeyboardMenuAction(action);
        return;
    }

    if (keyboardMenuMode) {
        handleKeyboardMenuAction(action);
        return;
    }

    if (screen == Screen::FontList && action.type == KeyActionType::Text) {
        if (action.text == "-") {
            adjustTerminalLineStep(-1);
            return;
        }
        if (action.text == "+") {
            adjustTerminalLineStep(1);
            return;
        }
    }

    if ((screen == Screen::WifiList || screen == Screen::WifiScan || screen == Screen::SshList ||
         screen == Screen::FontList) &&
        action.type != KeyActionType::Menu) {
        if (handleWifiContentAction(action)) {
            return;
        }
    }

    if (handleWifiContentAction(action)) {
        return;
    }

    if (screen == Screen::WifiEdit || screen == Screen::SshEdit) {
        if (isTabKey(action) || isRightKey(action) || isDownKey(action)) {
            moveEditField(1);
        } else if (isLeftKey(action) || isUpKey(action)) {
            moveEditField(-1);
        } else if (action.type == KeyActionType::Text) {
            for (size_t i = 0; i < action.text.length(); ++i) {
                editAppendChar(action.text[i]);
            }
        } else if (action.type == KeyActionType::Menu) {
            screen = screen == Screen::WifiEdit ? Screen::WifiList : Screen::SshList;
            dirty = true;
        }
        return;
    }

    if (screen == Screen::Terminal) {
        handleTerminalAction(action);
    } else if (action.type == KeyActionType::Text && (isUpKey(action) || isLeftKey(action))) {
        if (screen == Screen::WifiList) {
            scrollSettingList(-1, config.wifi.size());
        } else if (screen == Screen::WifiScan) {
            scrollSettingList(-1, scannedNetworks.size());
        } else if (screen == Screen::SshList) {
            scrollSettingList(-1, config.ssh.size());
        }
    } else if (action.type == KeyActionType::Text && (isDownKey(action) || isRightKey(action) || isTabKey(action))) {
        if (screen == Screen::WifiList) {
            scrollSettingList(1, config.wifi.size());
        } else if (screen == Screen::WifiScan) {
            scrollSettingList(1, scannedNetworks.size());
        } else if (screen == Screen::SshList) {
            scrollSettingList(1, config.ssh.size());
        }
    } else if (action.type == KeyActionType::Scroll) {
        if (screen == Screen::WifiList) {
            scrollSettingList(action.value, config.wifi.size());
        } else if (screen == Screen::WifiScan) {
            scrollSettingList(action.value, scannedNetworks.size());
        } else if (screen == Screen::SshList) {
            scrollSettingList(action.value, config.ssh.size());
        }
    } else if (action.type == KeyActionType::Menu) {
        screen = screen == Screen::WifiScan ? Screen::WifiList : Screen::Terminal;
        settingScrollOffset = 0;
        dirty = true;
    }
}

void pollSsh()
{
    if (!ssh.connected()) {
        return;
    }
    char buffer[512];
    setCrashStage("ssh.read");
    int n = ssh.read(buffer, sizeof(buffer));
    if (n > 0) {
        setCrashStage("ssh.append");
        vt.write(buffer, static_cast<size_t>(n));
        dirty = true;
    } else if (n < 0) {
        setCrashStage("ssh.read.error");
        ssh.disconnect();
        resetCommandEditor();
        configureTerminal();
        appendStatus("SSH disconnected by remote");
    }
    setCrashStage("loop");
}

void serialPrintHelp()
{
    Serial.println("Tab5 SSH serial API");
    Serial.println("  help");
    Serial.println("  status");
    Serial.println("  crash");
    Serial.println("  wifi status");
    Serial.println("  ssh list");
    Serial.println("  ssh active <index>");
    Serial.println("  ssh connect [index]");
    Serial.println("  ssh send <text>");
    Serial.println("  ssh raw <hex bytes>");
    Serial.println("  ssh disconnect");
    Serial.println("  term dump");
}

void serialPrintStatus()
{
    Serial.printf("screen=%u wifi=%s wl=%d ssh=%s activeWifi=%u activeSsh=%u stage=%s\r\n",
                  static_cast<unsigned>(screen),
                  wifiStatusText.c_str(),
                  static_cast<int>(WiFi.status()),
                  ssh.connected() ? "connected" : "disconnected",
                  static_cast<unsigned>(activeWifi),
                  static_cast<unsigned>(activeSsh),
                  crashStage);
}

void serialPrintSshProfiles()
{
    Serial.printf("ssh profiles: %u\r\n", static_cast<unsigned>(config.ssh.size()));
    for (size_t i = 0; i < config.ssh.size(); ++i) {
        const auto& p = config.ssh[i];
        Serial.printf("%c %u: %s %s@%s:%u term=%s\r\n",
                      i == activeSsh ? '*' : ' ',
                      static_cast<unsigned>(i),
                      p.name.c_str(),
                      p.user.c_str(),
                      p.host.c_str(),
                      static_cast<unsigned>(p.port),
                      p.terminal.c_str());
    }
}

void serialDumpTerminal()
{
    Serial.printf("term cols=%u rows=%u cursor=%u,%u alt=%u\r\n",
                  static_cast<unsigned>(vt.columns()),
                  static_cast<unsigned>(vt.rows()),
                  static_cast<unsigned>(vt.cursorColumn()),
                  static_cast<unsigned>(vt.cursorRow()),
                  vt.alternateScreen() ? 1 : 0);
    for (size_t row = 0; row < vt.rows(); ++row) {
        String line;
        line.reserve(vt.columns());
        for (size_t col = 0; col < vt.columns(); ++col) {
            String ch = vt.cell(col, row).ch;
            if (ch.length() == 1 && static_cast<uint8_t>(ch[0]) >= 0x20 && static_cast<uint8_t>(ch[0]) < 0x7F) {
                line += ch;
            } else if (ch == " ") {
                line += ' ';
            } else {
                line += '?';
            }
        }
        while (line.endsWith(" ")) {
            line.remove(line.length() - 1);
        }
        Serial.printf("%02u|%s\r\n", static_cast<unsigned>(row), line.c_str());
    }
}

int hexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool sendRawHex(const String& hex)
{
    uint8_t bytes[128];
    size_t count = 0;
    int high = -1;
    for (size_t i = 0; i < hex.length(); ++i) {
        int v = hexNibble(hex[i]);
        if (v < 0) {
            continue;
        }
        if (high < 0) {
            high = v;
        } else {
            if (count >= sizeof(bytes)) {
                return false;
            }
            bytes[count++] = static_cast<uint8_t>((high << 4) | v);
            high = -1;
        }
    }
    if (high >= 0 || count == 0 || !ssh.connected()) {
        return false;
    }
    return ssh.write(bytes, count);
}

bool parseTrailingIndex(const String& command, size_t prefixLen, size_t& index)
{
    String rest = command.substring(prefixLen);
    rest.trim();
    if (!rest.length()) {
        return false;
    }
    for (size_t i = 0; i < rest.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(rest[i]))) {
            return false;
        }
    }
    index = static_cast<size_t>(rest.toInt());
    return true;
}

void handleSerialCommand(String command)
{
    command.trim();
    if (!command.length()) {
        return;
    }
    Serial.print("> ");
    Serial.println(command);

    if (command == "help" || command == "?") {
        serialPrintHelp();
    } else if (command == "status") {
        serialPrintStatus();
    } else if (command == "crash") {
        Serial.printf("resetStageMagic=0x%08x stage=%s\r\n", static_cast<unsigned>(crashStageMagic), crashStage);
    } else if (command == "wifi status") {
        Serial.printf("wifiStatus=%s wl=%d ip=%s ssid=%s\r\n",
                      wifiStatusText.c_str(),
                      static_cast<int>(WiFi.status()),
                      WiFi.localIP().toString().c_str(),
                      WiFi.SSID().c_str());
    } else if (command == "ssh list") {
        serialPrintSshProfiles();
    } else if (command == "term dump") {
        serialDumpTerminal();
    } else if (command.startsWith("ssh active")) {
        size_t index = 0;
        if (!parseTrailingIndex(command, strlen("ssh active"), index) || index >= config.ssh.size()) {
            Serial.println("ERR invalid ssh index");
            return;
        }
        activeSsh = index;
        saveConfig();
        Serial.printf("OK activeSsh=%u\r\n", static_cast<unsigned>(activeSsh));
    } else if (command == "ssh connect" || command.startsWith("ssh connect ")) {
        size_t index = 0;
        if (parseTrailingIndex(command, strlen("ssh connect"), index)) {
            if (index >= config.ssh.size()) {
                Serial.println("ERR invalid ssh index");
                return;
            }
            activeSsh = index;
            saveConfig();
        }
        Serial.printf("OK connecting activeSsh=%u\r\n", static_cast<unsigned>(activeSsh));
        connectActiveSsh();
        serialPrintStatus();
    } else if (command.startsWith("ssh send ")) {
        if (!ssh.connected()) {
            Serial.println("ERR ssh disconnected");
            return;
        }
        String text = command.substring(strlen("ssh send "));
        String payload = text + "\n";
        bool ok = ssh.write(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
        Serial.println(ok ? "OK sent" : "ERR ssh write failed");
    } else if (command.startsWith("ssh raw ")) {
        String hex = command.substring(strlen("ssh raw "));
        Serial.println(sendRawHex(hex) ? "OK raw sent" : "ERR raw send failed");
    } else if (command == "ssh disconnect") {
        ssh.disconnect();
        resetCommandEditor();
        configureTerminal();
        appendStatus("SSH disconnected");
        Serial.println("OK");
    } else {
        Serial.println("ERR unknown command; type help");
    }
}

void pollSerialApi()
{
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            handleSerialCommand(serialCommand);
            serialCommand = "";
        } else if (c == '\b' || c == 0x7F) {
            if (serialCommand.length()) {
                serialCommand.remove(serialCommand.length() - 1);
            }
        } else if (std::isprint(static_cast<unsigned char>(c))) {
            if (serialCommand.length() < 160) {
                serialCommand += c;
            }
        }
    }
}

void initScreenSprite()
{
    screenSprite.setPsram(true);
    screenSprite.setColorDepth(8);
    screenSpriteReady = screenSprite.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
    if (!screenSpriteReady) {
        screenSprite.setColorDepth(4);
        screenSpriteReady = screenSprite.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
    }
    if (!screenSpriteReady) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("Sprite alloc failed", 8, 8);
        while (true) {
            delay(1000);
        }
    }
}

}

void tab5SetCrashStage(const char* stage)
{
    setCrashStage(stage);
}

void setup()
{
    setCrashStage("setup.start");
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    M5.Display.setRotation(3);
    M5.Display.setBrightness(100);
    initScreenSprite();
    configureTerminal();

    Serial.begin(115200);
    WiFi.onEvent(handleWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    terminal.append("Tab5 SSH Client\n");
    esp_reset_reason_t resetReason = esp_reset_reason();
    appendStatus(String("Reset reason: ") + static_cast<int>(resetReason));
    if (crashStageMagic == 0x54414235 && strlen(crashStage)) {
        appendStatus(String("Prev stage: ") + crashStage);
    }

    if (!settings.begin()) {
        appendStatus(settings.lastError());
    } else if (!settings.load(config)) {
        appendStatus(settings.lastError());
    } else {
        migrateLegacyLineStep();
        activeWifi = config.activeWifi;
        activeSsh = config.activeSsh;
        appendStatus(String("Loaded profiles: Wi-Fi ") + config.wifi.size() + ", SSH " + config.ssh.size());
    }

    keyboard.configure(config.keyboard);
    keyboard.begin();
    appendStatus(keyboard.status());
    configureTerminal();

    draw();
    setCrashStage("loop");
    startWifiReconnect(20000);
}

void loop()
{
    M5.update();
    pollSerialApi();
    handleTouch();
    keyboard.update();
    while (keyboard.available()) {
        handleAction(keyboard.read());
    }
    pollWifi();
    pollWifiScan();
    pollSsh();

    if (screen == Screen::Terminal && millis() - lastCursorBlink >= 500) {
        lastCursorBlink = millis();
        if (ssh.connected()) {
            vt.markCursorDirty();
        }
        cursorVisible = !cursorVisible;
        if (ssh.connected()) {
            vt.markCursorDirty();
        }
        dirty = true;
    }

    if (dirty && millis() - lastDraw > 5) {
        lastDraw = millis();
        draw();
    } else if (headerDirty && millis() - lastDraw > 5) {
        lastDraw = millis();
        drawHeaderOnly();
    }
    delay(2);
}
