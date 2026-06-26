#include "SettingsStore.hpp"
#include "PythonRunner.hpp"
#include "SshClient.hpp"
#include "Tab5KeyboardInput.hpp"
#include "TerminalBuffer.hpp"
#include "TerminalEmulator.hpp"
#include "WifiProfiles.hpp"
#include "fonts/TerminusBitmap.hpp"

#include <FS.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <esp_system.h>
#include <time.h>

namespace {
AppConfig config;
SettingsStore settings;
WifiProfiles wifiProfiles;
SshClient ssh;
PythonRunner python;
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
    ConfigEdit,
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
size_t editCursor = 0;
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
bool timeSyncStarted = false;
bool timeSynced = false;
uint32_t lastTimeSyncAttempt = 0;
bool sdReady = false;
bool sdInitAttempted = false;
String sdLastError = "not initialized";
String sdCwd = "/";
String serialCommand;
String commandLine;
size_t commandCursor = 0;
std::vector<String> commandHistory;
size_t commandHistoryIndex = 0;
bool pythonReplMode = false;
int touchScrollRemainderY = 0;
bool touchScrollActive = false;
bool remoteLineMode = false;
uint32_t lastCursorBlink = 0;
bool cursorVisible = true;
uint32_t lastSshReceive = 0;
RTC_DATA_ATTR uint32_t crashStageMagic = 0;
RTC_DATA_ATTR char crashStage[64] = "";

constexpr bool ForceFixedWifiForTest = false;
constexpr const char* FixedWifiSsid = "kumakero2.4";
constexpr const char* FixedWifiPassword = "4roses6126";
constexpr const char* LocalPrompt = "[tab5] ";
constexpr const char* PythonPrompt = "[py] ";
constexpr int SD_SPI_CS_PIN = 42;
constexpr int SD_SPI_SCK_PIN = 43;
constexpr int SD_SPI_MOSI_PIN = 44;
constexpr int SD_SPI_MISO_PIN = 39;

constexpr int HeaderH = 44;
constexpr int HeaderTouchH = HeaderH * 3;
constexpr int ContentTopGap = 30;

constexpr Rect BtnTerminal{4, 4, 72, 36};
constexpr Rect BtnWifi{82, 4, 72, 36};
constexpr Rect BtnSsh{160, 4, 72, 36};
constexpr Rect BtnFont{238, 4, 72, 36};
constexpr Rect BtnConfig{316, 4, 72, 36};
constexpr Rect BtnConnect{402, 4, 80, 36};
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
void inheritSavedSshCredentials(SshProfile& profile);
void connectActiveSsh();
bool parseTrailingIndex(const String& command, size_t prefixLen, size_t& index);
void configureTerminal();
void draw();
bool saveConfig();
bool sendSshText(const String& text);
void appendStatus(const String& message);
void startTimeSync(bool force = false);
void setWifiStatus(const String& message);
bool drawFallbackUnicodeGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg);

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
    if (index == i++) return &BtnConfig;

    if (screen == Screen::Terminal) {
        if (index == i++) return &BtnConnect;
    } else if (screen == Screen::WifiEdit) {
        if (index == i++) return &BtnConnect;
        if (index == i++) return &BtnSave;
        if (index == i++) return &BtnDelete;
    } else if (screen == Screen::SshEdit) {
        if (index == i++) return &BtnSave;
        if (index == i++) return &BtnDelete;
    } else if (screen == Screen::ConfigEdit) {
        if (index == i++) return &BtnSave;
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
    } else if (screen == Screen::ConfigEdit) {
        focusedHeaderButton = 4;
    } else {
        focusedHeaderButton = 0;
    }
    clampFocusedHeaderButton();
}

struct TerminalFontOption {
    const char* id;
    const char* label;
    const lgfx::IFont* japaneseFont;
    uint8_t cellW;
    uint8_t cellH;
    uint8_t settingsLineHeight;
    uint8_t defaultLineStep;
    uint8_t legacyLineStep;
    uint8_t previousLineStep;
};

constexpr TerminalFontOption TerminalFonts[] = {
    {"mono9", "Terminus 8x16", &fonts::lgfxJapanGothic_16, 8, 16, 18, 18, 12, 20},
    {"mono12", "Terminus 10x20", &fonts::lgfxJapanGothic_20, 10, 20, 22, 22, 15, 26},
    {"mono18", "Terminus 14x28", &fonts::lgfxJapanGothic_28, 14, 28, 30, 30, 23, 38},
    {"mono24", "Terminus 18x36", &fonts::lgfxJapanGothic_36, 18, 36, 38, 38, 30, 50},
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
    screenSprite.setFont(terminalFont().japaneseFont);
    screenSprite.setTextSize(1);
}

int terminalCellWidth()
{
    return terminalFont().cellW;
}

int terminalFontHeight(bool japanese)
{
    if (!japanese) {
        return terminalFont().cellH;
    }
    screenSprite.setFont(terminalFont().japaneseFont);
    screenSprite.setTextSize(1);
    return screenSprite.fontHeight();
}

const TerminusBitmap::Font& terminalBitmapFont()
{
    return TerminusBitmap::fontForHeight(terminalFont().cellH);
}

bool fontSupportsCodepoint(const lgfx::IFont* font, uint32_t cp)
{
    if (!font || cp > 0xFFFF) {
        return false;
    }
    lgfx::FontMetrics metrics;
    font->getDefaultMetric(&metrics);
    return font->updateFontMetric(&metrics, static_cast<uint16_t>(cp));
}

const lgfx::IFont* fontForTerminalCodepoint(uint32_t cp)
{
    struct FontCacheEntry {
        uint32_t cp;
        const lgfx::IFont* fallback;
        const lgfx::IFont* resolved;
        bool valid;
    };
    static FontCacheEntry cache[96] = {};

    const lgfx::IFont* fallback = terminalFont().japaneseFont;
    FontCacheEntry& entry = cache[cp % (sizeof(cache) / sizeof(cache[0]))];
    if (entry.valid && entry.cp == cp && entry.fallback == fallback) {
        return entry.resolved;
    }

    const lgfx::IFont* resolved = nullptr;
    if (fontSupportsCodepoint(fallback, cp)) {
        resolved = fallback;
    }

    entry = {cp, fallback, resolved, true};
    return resolved;
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
    bool knownFont = false;
    for (const auto& option : TerminalFonts) {
        if (config.keyboard.terminalFont == option.id) {
            knownFont = true;
            break;
        }
    }
    if (!knownFont || config.keyboard.terminalFont.startsWith("efont")) {
        config.keyboard.terminalFont = "mono12";
    }
    const auto& font = terminalFont();
    int minStep = max(terminalFontHeight(false), terminalFontHeight(true));
    if (config.keyboard.terminalLineStep == font.legacyLineStep ||
        config.keyboard.terminalLineStep == font.previousLineStep ||
        config.keyboard.terminalLineStep < minStep) {
        config.keyboard.terminalLineStep = static_cast<uint8_t>(max<int>(font.defaultLineStep, minStep));
    }
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
    screenSprite.setFont(&fonts::AsciiFont8x16);
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

uint8_t utf8CharLength(uint8_t c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

uint32_t utf8Codepoint(const String& text)
{
    if (!text.length()) return 0;
    const uint8_t c0 = static_cast<uint8_t>(text[0]);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0 && text.length() >= 2) {
        return ((c0 & 0x1F) << 6) | (static_cast<uint8_t>(text[1]) & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && text.length() >= 3) {
        return ((c0 & 0x0F) << 12) |
               ((static_cast<uint8_t>(text[1]) & 0x3F) << 6) |
               (static_cast<uint8_t>(text[2]) & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && text.length() >= 4) {
        return ((c0 & 0x07) << 18) |
               ((static_cast<uint8_t>(text[1]) & 0x3F) << 12) |
               ((static_cast<uint8_t>(text[2]) & 0x3F) << 6) |
               (static_cast<uint8_t>(text[3]) & 0x3F);
    }
    return c0;
}

uint32_t utf8CodepointAt(const String& text, size_t index)
{
    if (index >= text.length()) return 0;
    const uint8_t c0 = static_cast<uint8_t>(text[index]);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0 && index + 1 < text.length()) {
        return ((c0 & 0x1F) << 6) | (static_cast<uint8_t>(text[index + 1]) & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && index + 2 < text.length()) {
        return ((c0 & 0x0F) << 12) |
               ((static_cast<uint8_t>(text[index + 1]) & 0x3F) << 6) |
               (static_cast<uint8_t>(text[index + 2]) & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && index + 3 < text.length()) {
        return ((c0 & 0x07) << 18) |
               ((static_cast<uint8_t>(text[index + 1]) & 0x3F) << 12) |
               ((static_cast<uint8_t>(text[index + 2]) & 0x3F) << 6) |
               (static_cast<uint8_t>(text[index + 3]) & 0x3F);
    }
    return c0;
}

bool isJapaneseTerminalCodepoint(uint32_t cp)
{
    return (cp >= 0x3000 && cp <= 0x30FF) ||
           (cp >= 0x31F0 && cp <= 0x31FF) ||
           (cp >= 0x3400 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

int terminalCodepointWidth(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 0x80 || TerminusBitmap::hasGlyph(terminalBitmapFont(), cp)) {
        return terminalCellWidth();
    }
    if (isJapaneseTerminalCodepoint(cp)) {
        return terminalCellWidth() * 2;
    }
    return terminalCellWidth();
}

int terminalTextWidth(const String& text)
{
    int width = 0;
    for (size_t i = 0; i < text.length();) {
        uint8_t c = static_cast<uint8_t>(text[i]);
        uint32_t cp = utf8CodepointAt(text, i);
        width += terminalCodepointWidth(cp);
        i += utf8CharLength(c);
    }
    return width;
}

bool drawTerminusGlyph(uint32_t cp, int x, int lineTop, int lineStep, uint16_t fg)
{
    const auto& font = terminalBitmapFont();
    if (!TerminusBitmap::hasGlyph(font, cp)) {
        return false;
    }
    int y = lineTop + max<int>(0, (lineStep - font.targetH) / 2);
    return TerminusBitmap::drawGlyph(screenSprite, font, cp, x, y, fg);
}

int drawTerminalText(const String& text, int x, int lineTop, int lineStep, uint16_t fg, uint16_t bg)
{
    int cursorX = x;
    for (size_t i = 0; i < text.length();) {
        uint8_t c = static_cast<uint8_t>(text[i]);
        size_t next = i + utf8CharLength(c);
        String glyph = text.substring(i, next);
        uint32_t cp = utf8CodepointAt(text, i);
        if (drawTerminusGlyph(cp, cursorX, lineTop, lineStep, fg)) {
            cursorX += terminalCellWidth();
        } else if (isJapaneseTerminalCodepoint(cp)) {
            screenSprite.setFont(terminalFont().japaneseFont);
            screenSprite.setTextSize(1);
            screenSprite.setTextColor(fg, bg);
            int textY = lineTop + max<int>(0, (lineStep - screenSprite.fontHeight()) / 2);
            screenSprite.drawString(glyph, cursorX, textY);
            cursorX += terminalCellWidth() * 2;
        } else if (drawFallbackUnicodeGlyph(cp, cursorX, lineTop, terminalCellWidth(), lineStep, fg, bg)) {
            cursorX += terminalCellWidth();
        } else {
            screenSprite.setFont(terminalFont().japaneseFont);
            screenSprite.setTextSize(1);
            screenSprite.setTextColor(fg, bg);
            int textY = lineTop + max<int>(0, (lineStep - screenSprite.fontHeight()) / 2);
            screenSprite.drawString(glyph, cursorX, textY);
            cursorX += terminalCellWidth();
        }
        i = next;
    }
    return cursorX - x;
}

void drawTightJapaneseRun(const String& text, int x, int y, int w, int lineStep, uint16_t fg, uint16_t bg)
{
    screenSprite.setClipRect(x, y, w, lineStep);
    screenSprite.setFont(terminalFont().japaneseFont);
    screenSprite.setTextSize(1);
    screenSprite.setTextColor(fg, bg);
    const int textY = y + max<int>(0, (lineStep - screenSprite.fontHeight()) / 2);
    const int tighten = max<int>(1, terminalCellWidth() / 8);
    int cursorX = x;
    for (size_t i = 0; i < text.length();) {
        size_t next = i + utf8CharLength(static_cast<uint8_t>(text[i]));
        String glyph = text.substring(i, next);
        screenSprite.drawString(glyph, cursorX, textY);
        cursorX += max<int>(1, screenSprite.textWidth(glyph) - tighten);
        i = next;
    }
    screenSprite.clearClipRect();
}

bool drawBlockGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg)
{
    if (cp == 0x2588 || cp == 0x2589 || cp == 0x258A || cp == 0x258B ||
        cp == 0x258C || cp == 0x258D || cp == 0x258E || cp == 0x258F) {
        int fillW = w;
        if (cp >= 0x2589) {
            fillW = max<int>(1, (w * static_cast<int>(0x2590 - cp)) / 8);
        }
        screenSprite.fillRect(x, y, fillW, h, fg);
        return true;
    }
    if (cp == 0x2580) {
        screenSprite.fillRect(x, y, w, max<int>(1, h / 2), fg);
        return true;
    }
    if (cp == 0x2584) {
        int half = max<int>(1, h / 2);
        screenSprite.fillRect(x, y + h - half, w, half, fg);
        return true;
    }
    if (cp == 0x258C) {
        screenSprite.fillRect(x, y, max<int>(1, w / 2), h, fg);
        return true;
    }
    if (cp == 0x2590) {
        screenSprite.fillRect(x + w / 2, y, max<int>(1, w - w / 2), h, fg);
        return true;
    }
    if (cp >= 0x2596 && cp <= 0x259F) {
        const int leftW = max<int>(1, w / 2);
        const int rightX = x + leftW;
        const int rightW = max<int>(1, w - leftW);
        const int topH = max<int>(1, h / 2);
        const int bottomY = y + topH;
        const int bottomH = max<int>(1, h - topH);
        auto quad = [&](bool tl, bool tr, bool bl, bool br) {
            if (tl) screenSprite.fillRect(x, y, leftW, topH, fg);
            if (tr) screenSprite.fillRect(rightX, y, rightW, topH, fg);
            if (bl) screenSprite.fillRect(x, bottomY, leftW, bottomH, fg);
            if (br) screenSprite.fillRect(rightX, bottomY, rightW, bottomH, fg);
        };
        switch (cp) {
            case 0x2596: quad(false, false, true, false); break;
            case 0x2597: quad(false, false, false, true); break;
            case 0x2598: quad(true, false, false, false); break;
            case 0x2599: quad(true, false, true, true); break;
            case 0x259A: quad(true, false, false, true); break;
            case 0x259B: quad(true, true, true, false); break;
            case 0x259C: quad(true, true, false, true); break;
            case 0x259D: quad(false, true, false, false); break;
            case 0x259E: quad(false, true, true, false); break;
            case 0x259F: quad(false, true, true, true); break;
        }
        return true;
    }
    if (cp == 0x2591 || cp == 0x2592 || cp == 0x2593) {
        int step = cp == 0x2591 ? 4 : (cp == 0x2592 ? 3 : 2);
        for (int yy = 0; yy < h; ++yy) {
            for (int xx = (yy % step); xx < w; xx += step) {
                screenSprite.drawPixel(x + xx, y + yy, fg);
            }
        }
        return true;
    }
    (void)bg;
    return false;
}

bool drawBoxGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg)
{
    const int cx = x + w / 2;
    const int cy = y + h / 2;
    const int thick = max<int>(1, min<int>(w, h) / 8);
    auto hline = [&](int x1, int x2) {
        screenSprite.fillRect(min(x1, x2), cy - thick / 2, abs(x2 - x1) + 1, thick, fg);
    };
    auto vline = [&](int y1, int y2) {
        screenSprite.fillRect(cx - thick / 2, min(y1, y2), thick, abs(y2 - y1) + 1, fg);
    };
    auto junction = [&](bool left, bool right, bool up, bool down) {
        if (left) hline(x, cx);
        if (right) hline(cx, x + w - 1);
        if (up) vline(y, cy);
        if (down) vline(cy, y + h - 1);
    };
    auto smoothCorner = [&](bool right, bool down) {
        const int insetX = max<int>(1, w / 4);
        const int insetY = max<int>(1, h / 4);
        const int x1 = right ? cx + insetX / 2 : cx - insetX / 2;
        const int x2 = right ? cx + insetX : cx - insetX;
        const int y1 = down ? cy + insetY / 2 : cy - insetY / 2;
        const int y2 = down ? cy + insetY : cy - insetY;
        screenSprite.fillRect(min(cx, x1), min(cy, y1), max<int>(1, abs(x1 - cx) + thick), thick, fg);
        screenSprite.fillRect(min(x1, x2), min(y1, y2), thick, max<int>(1, abs(y2 - y1) + thick), fg);
    };

    switch (cp) {
        case 0x2500: hline(x, x + w - 1); return true;
        case 0x2502: vline(y, y + h - 1); return true;
        case 0x256D: hline(cx, x + w - 1); vline(cy, y + h - 1); smoothCorner(true, true); return true;
        case 0x256E: hline(x, cx); vline(cy, y + h - 1); smoothCorner(false, true); return true;
        case 0x2570: hline(cx, x + w - 1); vline(y, cy); smoothCorner(true, false); return true;
        case 0x256F: hline(x, cx); vline(y, cy); smoothCorner(false, false); return true;
        case 0x250C: hline(cx, x + w - 1); vline(cy, y + h - 1); return true;
        case 0x2510: hline(x, cx); vline(cy, y + h - 1); return true;
        case 0x2514: hline(cx, x + w - 1); vline(y, cy); return true;
        case 0x2518: hline(x, cx); vline(y, cy); return true;
        case 0x251C: junction(false, true, true, true); return true;
        case 0x2524: junction(true, false, true, true); return true;
        case 0x252C: junction(true, true, false, true); return true;
        case 0x2534: junction(true, true, true, false); return true;
        case 0x253C: junction(true, true, true, true); return true;
        case 0x251D:
        case 0x251E:
        case 0x251F:
        case 0x2520:
        case 0x2521:
        case 0x2522:
        case 0x2523:
            junction(false, true, true, true); return true;
        case 0x2525:
        case 0x2526:
        case 0x2527:
        case 0x2528:
        case 0x2529:
        case 0x252A:
        case 0x252B:
            junction(true, false, true, true); return true;
        case 0x252D:
        case 0x252E:
        case 0x252F:
        case 0x2530:
        case 0x2531:
        case 0x2532:
        case 0x2533:
            junction(true, true, false, true); return true;
        case 0x2535:
        case 0x2536:
        case 0x2537:
        case 0x2538:
        case 0x2539:
        case 0x253A:
        case 0x253B:
            junction(true, true, true, false); return true;
        case 0x253D:
        case 0x253E:
        case 0x253F:
        case 0x2540:
        case 0x2541:
        case 0x2542:
        case 0x2543:
        case 0x2544:
        case 0x2545:
        case 0x2546:
        case 0x2547:
        case 0x2548:
        case 0x2549:
        case 0x254A:
        case 0x254B:
            junction(true, true, true, true); return true;
        case 0x2550:
            screenSprite.fillRect(x, cy - thick, w, thick, fg);
            screenSprite.fillRect(x, cy + thick, w, thick, fg);
            return true;
        case 0x2551:
            screenSprite.fillRect(cx - thick, y, thick, h, fg);
            screenSprite.fillRect(cx + thick, y, thick, h, fg);
            return true;
        case 0x2552:
        case 0x2553:
        case 0x2554:
            junction(false, true, false, true); return true;
        case 0x2555:
        case 0x2556:
        case 0x2557:
            junction(true, false, false, true); return true;
        case 0x2558:
        case 0x2559:
        case 0x255A:
            junction(false, true, true, false); return true;
        case 0x255B:
        case 0x255C:
        case 0x255D:
            junction(true, false, true, false); return true;
        case 0x255E:
        case 0x255F:
        case 0x2560:
            junction(false, true, true, true); return true;
        case 0x2561:
        case 0x2562:
        case 0x2563:
            junction(true, false, true, true); return true;
        case 0x2564:
        case 0x2565:
        case 0x2566:
            junction(true, true, false, true); return true;
        case 0x2567:
        case 0x2568:
        case 0x2569:
            junction(true, true, true, false); return true;
        case 0x256A:
        case 0x256B:
        case 0x256C:
            junction(true, true, true, true); return true;
        case 0x2571:
            screenSprite.drawLine(x, y + h - 1, x + w - 1, y, fg);
            return true;
        case 0x2572:
            screenSprite.drawLine(x, y, x + w - 1, y + h - 1, fg);
            return true;
        case 0x2573:
            screenSprite.drawLine(x, y + h - 1, x + w - 1, y, fg);
            screenSprite.drawLine(x, y, x + w - 1, y + h - 1, fg);
            return true;
        case 0x2574: hline(x, cx); return true;
        case 0x2575: vline(y, cy); return true;
        case 0x2576: hline(cx, x + w - 1); return true;
        case 0x2577: vline(cy, y + h - 1); return true;
        default:
            return false;
    }
}

bool isUnicodeBlank(uint32_t cp)
{
    return cp == 0x00A0 || cp == 0x1680 || cp == 0x180E ||
           (cp >= 0x2000 && cp <= 0x200F) ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000 ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF;
}

bool isFallbackSymbolRange(uint32_t cp)
{
    return (cp >= 0x2190 && cp <= 0x21FF) || // arrows
           (cp >= 0x2300 && cp <= 0x23FF) || // technical symbols
           (cp >= 0x2460 && cp <= 0x24FF) || // enclosed alphanumerics
           (cp >= 0x25A0 && cp <= 0x25FF) || // geometric shapes
           (cp >= 0x2600 && cp <= 0x27BF) || // misc symbols and dingbats
           (cp >= 0x27C0 && cp <= 0x27FF) || // supplemental arrows and brackets
           (cp >= 0x2800 && cp <= 0x28FF) || // braille patterns
           (cp >= 0xE000 && cp <= 0xF8FF) || // private-use icons, including Nerd Font
           (cp >= 0x1F000 && cp <= 0x1FAFF); // emoji and symbol planes
}

bool drawBrailleGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg)
{
    if (cp < 0x2800 || cp > 0x28FF) {
        return false;
    }
    const uint8_t bits = static_cast<uint8_t>(cp - 0x2800);
    const int dot = max<int>(1, min<int>(w, h) / 5);
    const int left = x + max<int>(1, w / 4 - dot / 2);
    const int right = x + max<int>(1, (w * 3) / 4 - dot / 2);
    const int top = y + max<int>(1, h / 8);
    const int gap = max<int>(dot + 1, (h - dot - 2) / 4);
    auto drawDot = [&](uint8_t bit, int dx, int dy) {
        if (bits & bit) {
            screenSprite.fillCircle(dx + dot / 2, dy + dot / 2, max<int>(1, dot / 2), fg);
        }
    };
    drawDot(0x01, left, top);
    drawDot(0x02, left, top + gap);
    drawDot(0x04, left, top + gap * 2);
    drawDot(0x40, left, top + gap * 3);
    drawDot(0x08, right, top);
    drawDot(0x10, right, top + gap);
    drawDot(0x20, right, top + gap * 2);
    drawDot(0x80, right, top + gap * 3);
    return true;
}

bool drawPowerlineGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg)
{
    switch (cp) {
        case 0xE0B0:
        case 0xE0B1:
            screenSprite.fillTriangle(x, y, x, y + h - 1, x + w - 1, y + h / 2, fg);
            if (cp == 0xE0B1) {
                screenSprite.fillTriangle(x + 2, y + 3, x + 2, y + h - 4, x + w - 4, y + h / 2, bg);
            }
            return true;
        case 0xE0B2:
        case 0xE0B3:
            screenSprite.fillTriangle(x + w - 1, y, x + w - 1, y + h - 1, x, y + h / 2, fg);
            if (cp == 0xE0B3) {
                screenSprite.fillTriangle(x + w - 3, y + 3, x + w - 3, y + h - 4, x + 3, y + h / 2, bg);
            }
            return true;
        default:
            return false;
    }
}

bool drawFallbackUnicodeGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg)
{
    if (isUnicodeBlank(cp)) {
        return true;
    }
    if (drawBrailleGlyph(cp, x, y, w, h, fg) || drawPowerlineGlyph(cp, x, y, w, h, fg, bg)) {
        return true;
    }
    if (!isFallbackSymbolRange(cp)) {
        return false;
    }

    const int pad = max<int>(1, min<int>(w, h) / 8);
    const int cx = x + w / 2;
    const int cy = y + h / 2;
    const int r = max<int>(2, min<int>(w, h) / 3);

    if (cp >= 0x2190 && cp <= 0x21FF) {
        bool drawn = false;
        if (cp == 0x2190 || cp == 0x2194) {
            screenSprite.drawLine(x + pad, cy, x + w - pad - 1, cy, fg);
            screenSprite.fillTriangle(x + pad, cy, x + pad + r / 2, cy - r / 2, x + pad + r / 2, cy + r / 2, fg);
            drawn = true;
        }
        if (cp == 0x2192 || cp == 0x2194 || cp == 0x21D2) {
            screenSprite.drawLine(x + pad, cy, x + w - pad - 1, cy, fg);
            screenSprite.fillTriangle(x + w - pad - 1, cy, x + w - pad - r / 2, cy - r / 2,
                                      x + w - pad - r / 2, cy + r / 2, fg);
            drawn = true;
        }
        if (cp == 0x2191 || cp == 0x2195) {
            screenSprite.drawLine(cx, y + pad, cx, y + h - pad - 1, fg);
            screenSprite.fillTriangle(cx, y + pad, cx - r / 2, y + pad + r / 2, cx + r / 2, y + pad + r / 2, fg);
            drawn = true;
        }
        if (cp == 0x2193 || cp == 0x2195) {
            screenSprite.drawLine(cx, y + pad, cx, y + h - pad - 1, fg);
            screenSprite.fillTriangle(cx, y + h - pad - 1, cx - r / 2, y + h - pad - r / 2,
                                      cx + r / 2, y + h - pad - r / 2, fg);
            drawn = true;
        }
        if (cp == 0x21B5) {
            screenSprite.drawLine(x + w - pad - 1, y + pad, x + w - pad - 1, cy, fg);
            screenSprite.drawLine(x + pad, cy, x + w - pad - 1, cy, fg);
            screenSprite.fillTriangle(x + pad, cy, x + pad + r / 2, cy - r / 2, x + pad + r / 2, cy + r / 2, fg);
            drawn = true;
        }
        if (!drawn) {
            screenSprite.drawLine(x + pad, cy, x + w - pad - 1, cy, fg);
            screenSprite.fillTriangle(x + w - pad - 1, cy, x + w - pad - r / 2, cy - r / 2,
                                      x + w - pad - r / 2, cy + r / 2, fg);
        }
        return true;
    }

    if (cp == 0x25CF || cp == 0x26AB || cp == 0x26AA || cp == 0x1F534 || cp == 0x1F535) {
        screenSprite.fillCircle(cx, cy, r, fg);
        return true;
    }
    if (cp == 0x25CB || cp == 0x25EF) {
        screenSprite.drawCircle(cx, cy, r, fg);
        return true;
    }
    if (cp == 0x25A0 || cp == 0x25A1 || cp == 0x25AA || cp == 0x25AB) {
        if (cp == 0x25A0 || cp == 0x25AA) {
            screenSprite.fillRect(x + pad, y + pad, w - pad * 2, h - pad * 2, fg);
        } else {
            screenSprite.drawRect(x + pad, y + pad, w - pad * 2, h - pad * 2, fg);
        }
        return true;
    }
    if (cp == 0x25B6 || cp == 0x25B8 || cp == 0x25BA || cp == 0x25C0 || cp == 0x25C2 || cp == 0x25C4) {
        if (cp == 0x25C0 || cp == 0x25C2 || cp == 0x25C4) {
            screenSprite.fillTriangle(x + pad, cy, x + w - pad - 1, y + pad, x + w - pad - 1, y + h - pad - 1, fg);
        } else {
            screenSprite.fillTriangle(x + w - pad - 1, cy, x + pad, y + pad, x + pad, y + h - pad - 1, fg);
        }
        return true;
    }
    if (cp == 0x276E || cp == 0x276F || cp == 0x276C || cp == 0x276D ||
        cp == 0x2770 || cp == 0x2771 || cp == 0x2772 || cp == 0x2773 ||
        cp == 0x2039 || cp == 0x203A) {
        const bool left = cp == 0x276E || cp == 0x276C || cp == 0x2770 || cp == 0x2772 || cp == 0x2039;
        const int thick = max<int>(1, min<int>(w, h) / 8);
        const int topY = y + pad;
        const int bottomY = y + h - pad - 1;
        const int innerX = left ? x + pad : x + w - pad - 1;
        const int outerX = left ? x + w - pad - 1 : x + pad;
        for (int t = 0; t < thick; ++t) {
            screenSprite.drawLine(outerX + (left ? -t : t), topY, innerX + (left ? t : -t), cy, fg);
            screenSprite.drawLine(innerX + (left ? t : -t), cy, outerX + (left ? -t : t), bottomY, fg);
        }
        return true;
    }
    if (cp == 0x2768 || cp == 0x2769 || cp == 0x276A || cp == 0x276B || cp == 0x2774 || cp == 0x2775) {
        const bool left = cp == 0x2768 || cp == 0x276A || cp == 0x2774;
        const int thick = max<int>(1, min<int>(w, h) / 10);
        const int topY = y + pad;
        const int bottomY = y + h - pad - 1;
        const int midY = cy;
        if (cp == 0x2774 || cp == 0x2775) {
            const int outerX = left ? x + w - pad - 1 : x + pad;
            const int innerX = left ? x + pad : x + w - pad - 1;
            for (int t = 0; t < thick; ++t) {
                screenSprite.drawLine(outerX, topY, innerX, topY + (midY - topY) / 2, fg);
                screenSprite.drawLine(innerX, topY + (midY - topY) / 2, innerX, midY - t, fg);
                screenSprite.drawLine(innerX, midY + t, innerX, midY + (bottomY - midY) / 2, fg);
                screenSprite.drawLine(innerX, midY + (bottomY - midY) / 2, outerX, bottomY, fg);
            }
        } else {
            const int outerX = left ? x + w - pad - 1 : x + pad;
            const int innerX = left ? x + pad : x + w - pad - 1;
            for (int t = 0; t < thick; ++t) {
                screenSprite.drawLine(outerX, topY, innerX + (left ? t : -t), midY, fg);
                screenSprite.drawLine(innerX + (left ? t : -t), midY, outerX, bottomY, fg);
            }
        }
        return true;
    }
    if ((cp >= 0x2794 && cp <= 0x27BF) || cp == 0x27F6 || cp == 0x27F8 || cp == 0x27FA) {
        const int tailX = x + pad;
        const int headX = x + w - pad - 1;
        const int headW = max<int>(3, w / 3);
        const int thick = max<int>(1, min<int>(w, h) / 8);
        screenSprite.fillRect(tailX, cy - thick / 2, max<int>(1, headX - tailX - headW / 2), thick, fg);
        screenSprite.fillTriangle(headX, cy, headX - headW, y + pad, headX - headW, y + h - pad - 1, fg);
        if (cp == 0x27F8 || cp == 0x27FA) {
            screenSprite.fillRect(tailX, cy + thick + 1, max<int>(1, headX - tailX - headW / 2), thick, fg);
        }
        return true;
    }

    if (cp >= 0x1F000 && cp <= 0x1FAFF) {
        screenSprite.drawRoundRect(x + pad, y + pad, w - pad * 2, h - pad * 2, max<int>(2, r / 2), fg);
        screenSprite.fillCircle(x + w / 3, y + h / 3, max<int>(1, r / 5), fg);
        screenSprite.fillCircle(x + (w * 2) / 3, y + h / 3, max<int>(1, r / 5), fg);
        screenSprite.drawLine(x + w / 3, y + (h * 2) / 3, x + (w * 2) / 3, y + (h * 2) / 3, fg);
        return true;
    }

    screenSprite.drawRect(x + pad, y + pad, w - pad * 2, h - pad * 2, fg);
    screenSprite.drawLine(x + pad, y + pad, x + w - pad - 1, y + h - pad - 1, fg);
    screenSprite.drawLine(x + w - pad - 1, y + pad, x + pad, y + h - pad - 1, fg);
    (void)bg;
    return true;
}

void drawMixedTerminalLine(const String& line, int x, int lineTop, int lineHeight)
{
    drawTerminalText(line, x, lineTop, lineHeight, TFT_GREEN, TFT_BLACK);
}

void drawVtTerminal()
{
    static bool lastViewingHistory = false;
    static size_t lastScrollbackOffset = 0;
    const int cellW = terminalCellWidth();
    const int lineStep = terminalLineStep();
    const int gridRight = 4 + static_cast<int>(vt.columns()) * cellW;
    const int gridBottom = HeaderH + static_cast<int>(vt.rows()) * lineStep;
    const bool viewingHistory = vt.scrollbackOffset() > 0;
    int scrollDelta = 0;
    bool canShiftRows = false;

    if (viewingHistory && lastViewingHistory) {
        scrollDelta = static_cast<int>(vt.scrollbackOffset()) - static_cast<int>(lastScrollbackOffset);
        canShiftRows = scrollDelta != 0 && abs(scrollDelta) < static_cast<int>(vt.rows());
    }
    if (!viewingHistory && lastViewingHistory) {
        vt.markAllDirty();
    }

    if (gridRight < screenSprite.width()) {
        screenSprite.fillRect(gridRight, HeaderH, screenSprite.width() - gridRight,
                              max<int>(0, min<int>(screenSprite.height(), gridBottom) - HeaderH), TFT_BLACK);
    }
    if (gridBottom < screenSprite.height()) {
        screenSprite.fillRect(0, gridBottom, screenSprite.width(), screenSprite.height() - gridBottom, TFT_BLACK);
    }

    if (canShiftRows) {
        const int bodyH = max<int>(0, min<int>(screenSprite.height(), gridBottom) - HeaderH);
        screenSprite.setClipRect(0, HeaderH, screenSprite.width(), bodyH);
        screenSprite.scroll(0, scrollDelta * lineStep);
        screenSprite.clearClipRect();
    }

    auto drawRow = [&](size_t row, bool force) {
        int y = HeaderH + static_cast<int>(row) * lineStep;
        if (y >= screenSprite.height()) {
            return;
        }
        for (size_t col = 0; col < vt.columns();) {
            int x = 4 + static_cast<int>(col) * cellW;
            if (x >= screenSprite.width()) {
                break;
            }
            const auto& cell = vt.displayCell(col, row);
            bool cursor = !viewingHistory && cursorVisible && col == vt.cursorColumn() && row == vt.cursorRow();
            if (!force && !cell.dirty && !cursor) {
                ++col;
                continue;
            }
            bool inverse = cell.inverse ^ cursor;
            uint16_t fg = terminalColor(cell.fg, cell.bold);
            uint16_t bg = terminalColor(cell.bg, false);
            if (inverse) {
                std::swap(fg, bg);
            }
            if (cell.continuation && !cursor) {
                ++col;
                continue;
            }
            const int drawW = cell.wide ? min<int>(cellW * 2, screenSprite.width() - x) : cellW;

            if (cell.wide && !cursor && cell.ch != " " && isJapaneseTerminalCodepoint(utf8Codepoint(cell.ch))) {
                size_t endCol = col;
                String runText;
                bool runDirty = false;
                while (endCol < vt.columns()) {
                    const auto& runCell = vt.displayCell(endCol, row);
                    bool runCursor = !viewingHistory && cursorVisible && endCol == vt.cursorColumn() &&
                                     row == vt.cursorRow();
                    if (runCursor || !runCell.wide || runCell.continuation || runCell.ch == " " ||
                        runCell.fg != cell.fg || runCell.bg != cell.bg || runCell.bold != cell.bold ||
                        runCell.inverse != cell.inverse || !isJapaneseTerminalCodepoint(utf8Codepoint(runCell.ch))) {
                        break;
                    }
                    runText += runCell.ch;
                    runDirty = runDirty || runCell.dirty;
                    endCol += 2;
                }
                if (endCol > col) {
                    if (force || runDirty) {
                        const int runW = min<int>((endCol - col) * cellW, screenSprite.width() - x);
                        screenSprite.fillRect(x, y, runW, lineStep, bg);
                        drawTightJapaneseRun(runText, x, y, runW, lineStep, fg, bg);
                    }
                    col = endCol;
                    continue;
                }
            }

            screenSprite.fillRect(x, y, drawW, lineStep, bg);
            if (cell.ch != " ") {
                uint32_t cp = utf8Codepoint(cell.ch);
                if (drawTerminusGlyph(cp, x, y, lineStep, fg)) {
                    ++col;
                    continue;
                }
                const lgfx::IFont* glyphFont = fontForTerminalCodepoint(cp);
                if (!glyphFont) {
                    if (drawBlockGlyph(cp, x, y, drawW, lineStep, fg, bg)) {
                        ++col;
                        continue;
                    }
                    if (drawBoxGlyph(cp, x, y, drawW, lineStep, fg)) {
                        ++col;
                        continue;
                    }
                    if (drawFallbackUnicodeGlyph(cp, x, y, drawW, lineStep, fg, bg)) {
                        ++col;
                        continue;
                    }
                    glyphFont = terminalFont().japaneseFont;
                }
                screenSprite.setFont(glyphFont);
                screenSprite.setTextSize(1);
                screenSprite.setTextColor(fg, bg);
                int textY = y + max<int>(0, (lineStep - screenSprite.fontHeight()) / 2);
                screenSprite.drawString(cell.ch, x, textY);
            }
            ++col;
        }
    };

    if (canShiftRows) {
        const int exposed = abs(scrollDelta);
        if (scrollDelta > 0) {
            for (int row = 0; row < exposed; ++row) {
                drawRow(static_cast<size_t>(row), true);
            }
        } else {
            const int rows = static_cast<int>(vt.rows());
            for (int row = max(0, rows - exposed); row < rows; ++row) {
                drawRow(static_cast<size_t>(row), true);
            }
        }
    } else {
        const bool force = viewingHistory;
        for (size_t row = 0; row < vt.rows(); ++row) {
            drawRow(row, force);
        }
    }
    setTerminalFont();
    screenSprite.setTextColor(TFT_GREEN, TFT_BLACK);
    vt.clearDirty();
    lastViewingHistory = viewingHistory;
    lastScrollbackOffset = vt.scrollbackOffset();
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

void appendCliLine(const String& line)
{
    terminal.append(line);
    terminal.append("\n");
}

void appendPythonCliLine(const String& line)
{
    appendCliLine(line);
}

void serialPythonLine(const String& line)
{
    Serial.println(line);
}

void appendCliHelp()
{
    appendCliLine("Tab5 CLI commands:");
    appendCliLine("  help, man <command>, clear, status, history");
    appendCliLine("  uname, whoami, hostname, date, uptime, echo");
    appendCliLine("  wifi status, wifi list, ip addr");
    appendCliLine("  ssh list, ssh connect <index>, ssh disconnect");
    appendCliLine("  ssh user@host[:port] [password]");
    appendCliLine("  sd status, ls [path], cat <path>, sd write <path> <text>");
    appendCliLine("  scp get <remote> <sd-local>, scp put <sd-local> <remote>");
    appendCliLine("  py repl, py run <sd.py>, py exec <statement>");
    appendCliLine("  ble status, ble scan, ble forget");
}

void appendCliManEntry(const char* name, const char* synopsis, const char* description)
{
    appendCliLine(String("NAME"));
    appendCliLine(String("  ") + name);
    appendCliLine(String("SYNOPSIS"));
    appendCliLine(String("  ") + synopsis);
    appendCliLine(String("DESCRIPTION"));
    appendCliLine(String("  ") + description);
}

bool appendCliMan(const String& topic)
{
    String key = topic;
    key.trim();
    key.toLowerCase();
    while (key.indexOf("  ") >= 0) {
        key.replace("  ", " ");
    }
    if (!key.length() || key == "help" || key == "man") {
        appendCliManEntry("man", "man <command>", "Show help for Tab5 CLI built-in commands.");
        appendCliLine("Try: man clear, man date, man wifi, man ssh");
        return true;
    }
    if (key == "clear" || key == "cls" || key == "reset") {
        appendCliManEntry("clear", "clear", "Clear the Tab5 CLI screen buffer.");
        return true;
    }
    if (key == "status") {
        appendCliManEntry("status", "status", "Show Wi-Fi, SSH, device, time, and active profile status.");
        return true;
    }
    if (key == "history") {
        appendCliManEntry("history", "history", "Show local Tab5 CLI command history.");
        return true;
    }
    if (key == "whoami" || key == "hostname") {
        appendCliManEntry(key.c_str(), key.c_str(), "Print the configured Tab5 device name.");
        return true;
    }
    if (key == "uname") {
        appendCliManEntry("uname", "uname [-a]", "Print Tab5 CLI firmware and platform information.");
        return true;
    }
    if (key == "date") {
        appendCliManEntry("date", "date", "Print NTP-synced local time using the configured region and UTC offset.");
        return true;
    }
    if (key == "time" || key == "time sync" || key == "ntp" || key == "ntp sync") {
        appendCliManEntry("time sync", "time sync | ntp sync", "Request network time synchronization over Wi-Fi.");
        return true;
    }
    if (key == "uptime") {
        appendCliManEntry("uptime", "uptime", "Print time elapsed since the firmware booted.");
        return true;
    }
    if (key == "echo") {
        appendCliManEntry("echo", "echo <text>", "Print text back to the Tab5 CLI screen.");
        return true;
    }
    if (key == "wifi" || key == "wifi status") {
        appendCliManEntry("wifi status", "wifi status", "Show Wi-Fi connection state, IP address, and SSID.");
        appendCliLine("SEE ALSO");
        appendCliLine("  wifi list");
        return true;
    }
    if (key == "wifi list") {
        appendCliManEntry("wifi list", "wifi list", "List saved Wi-Fi profiles and the active profile marker.");
        return true;
    }
    if (key == "ip" || key == "ip addr" || key == "ip a" || key == "ifconfig") {
        appendCliManEntry("ip addr", "ip addr | ip a | ifconfig", "Show the Tab5 wlan0 address and Wi-Fi state.");
        return true;
    }
    if (key == "ssh" || key == "ssh list") {
        appendCliManEntry("ssh list", "ssh list", "List saved SSH profiles and the active profile marker.");
        appendCliLine("SEE ALSO");
        appendCliLine("  ssh connect, ssh disconnect");
        return true;
    }
    if (key == "ssh connect") {
        appendCliManEntry("ssh connect", "ssh connect <index>", "Connect to a saved SSH profile by list index.");
        return true;
    }
    if (key == "ssh disconnect") {
        appendCliManEntry("ssh disconnect", "ssh disconnect", "Disconnect the active SSH session.");
        return true;
    }
    if (key == "ssh direct" || key == "ssh user@host" || key == "ssh user@host[:port]") {
        appendCliManEntry("ssh direct", "ssh user@host[:port] [password]", "Connect without a saved profile.");
        return true;
    }
    if (key == "scp" || key == "scp get" || key == "scp put") {
        appendCliManEntry("scp", "scp get <remote> <sd-local> [profile] | scp put <sd-local> <remote> [profile]",
                          "Copy files between the active SSH profile and the Tab5 microSD card.");
        appendCliLine("Direct endpoints are supported: user@host:/path.");
        return true;
    }
    if (key == "ble" || key == "ble status" || key == "ble scan" || key == "ble forget") {
        appendCliManEntry("ble", "ble status | ble scan | ble forget",
                          "Manage Bluetooth keyboard settings. Pairing backend depends on the Tab5 radio stack build.");
        return true;
    }
    if (key == "py" || key == "python" || key == "py repl" || key == "py run" || key == "py exec") {
        appendCliManEntry("py", "py repl | py run <sd.py> | py exec <statement>",
                          "Run the built-in MicroPython-compatible subset with SD scripts and GPIO helpers.");
        appendCliLine("Examples:");
        appendCliLine("  py exec print('hello')");
        appendCliLine("  py exec blink(2, 3, 100)");
        appendCliLine("  py run /scripts/blink.py");
        return true;
    }
    if (key == "sd" || key == "sd status") {
        appendCliManEntry("sd status", "sd status", "Show microSD mount state, type, size, and current directory.");
        appendCliLine("SEE ALSO");
        appendCliLine("  sd ls, sd cat, sd write, sd append, sd mkdir, sd rm");
        return true;
    }
    if (key == "ls" || key == "dir" || key == "sd ls") {
        appendCliManEntry("sd ls", "ls [path] | sd ls [path]", "List files on the Tab5 microSD card.");
        return true;
    }
    if (key == "cd" || key == "pwd" || key == "cat" || key == "sd cat") {
        appendCliManEntry(key.c_str(), key.c_str(), "Operate on the Tab5 microSD card.");
        appendCliLine("Examples: pwd, cd /scripts, cat boot.py");
        return true;
    }
    if (key == "sd write" || key == "sd append" || key == "sd mkdir" || key == "sd rm") {
        appendCliManEntry(key.c_str(), key.c_str(), "Create, append, make directories, or remove files on microSD.");
        appendCliLine("Examples: sd write /hello.py print(123), sd append /notes.txt text");
        return true;
    }
    appendCliLine(String("No manual entry for ") + topic);
    appendCliLine("type 'help' to list Tab5 CLI commands");
    return true;
}

void appendWifiList()
{
    if (config.wifi.empty()) {
        appendCliLine("no Wi-Fi profiles");
        return;
    }
    for (size_t i = 0; i < config.wifi.size(); ++i) {
        appendCliLine(String(i == activeWifi ? "* " : "  ") + i + " " + config.wifi[i].name + " " + config.wifi[i].ssid);
    }
}

void appendSshList()
{
    if (config.ssh.empty()) {
        appendCliLine("no SSH profiles");
        return;
    }
    for (size_t i = 0; i < config.ssh.size(); ++i) {
        const auto& p = config.ssh[i];
        appendCliLine(String(i == activeSsh ? "* " : "  ") + i + " " + p.name + " " + p.user + "@" + p.host + ":" + p.port);
    }
}

String normalizeSdPath(const String& input)
{
    String path = input;
    path.trim();
    if (!path.length()) {
        path = sdCwd;
    } else if (!path.startsWith("/")) {
        path = sdCwd;
        if (!path.endsWith("/")) {
            path += "/";
        }
        path += input;
    }
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    if (!path.startsWith("/")) {
        path = "/" + path;
    }
    if (path.length() > 1 && path.endsWith("/")) {
        path.remove(path.length() - 1);
    }
    return path;
}

bool ensureSdReady()
{
    if (sdReady) {
        return true;
    }
    if (!sdInitAttempted) {
        sdInitAttempted = true;
        SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
        if (SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
            sdReady = true;
            sdLastError = "";
            return true;
        }
        sdLastError = "SD card not detected";
    }
    return false;
}

String formatBytes(uint64_t bytes)
{
    if (bytes < 1024) {
        return String(static_cast<unsigned long>(bytes)) + " B";
    }
    if (bytes < 1024ULL * 1024ULL) {
        return String(static_cast<double>(bytes) / 1024.0, 1) + " KB";
    }
    return String(static_cast<double>(bytes) / (1024.0 * 1024.0), 1) + " MB";
}

void appendSdStatus()
{
    if (!ensureSdReady()) {
        appendCliLine(String("sd=not ready: ") + sdLastError);
        return;
    }
    uint8_t type = SD.cardType();
    String typeName = "unknown";
    if (type == CARD_MMC) typeName = "MMC";
    else if (type == CARD_SD) typeName = "SDSC";
    else if (type == CARD_SDHC) typeName = "SDHC";
    appendCliLine(String("sd=ready type=") + typeName +
                  " size=" + formatBytes(SD.cardSize()) +
                  " used=" + formatBytes(SD.usedBytes()) +
                  " total=" + formatBytes(SD.totalBytes()));
    appendCliLine(String("cwd=") + sdCwd);
}

void appendSdList(const String& inputPath)
{
    if (!ensureSdReady()) {
        appendCliLine(String("sd: ") + sdLastError);
        return;
    }
    String path = normalizeSdPath(inputPath);
    File root = SD.open(path, FILE_READ);
    if (!root) {
        appendCliLine(String("sd ls: cannot open ") + path);
        return;
    }
    if (!root.isDirectory()) {
        appendCliLine(formatBytes(root.size()) + " " + path);
        root.close();
        return;
    }
    appendCliLine(String("sd: ") + path);
    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (file.isDirectory()) {
            appendCliLine(String("  <DIR>      ") + name + "/");
        } else {
            appendCliLine(String("  ") + formatBytes(file.size()) + "  " + name);
        }
        file = root.openNextFile();
    }
    root.close();
}

void appendSdCat(const String& inputPath)
{
    if (!ensureSdReady()) {
        appendCliLine(String("sd: ") + sdLastError);
        return;
    }
    String path = normalizeSdPath(inputPath);
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        appendCliLine(String("sd cat: cannot open ") + path);
        return;
    }
    String line;
    size_t printed = 0;
    while (file.available() && printed < 12000) {
        char c = static_cast<char>(file.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            appendCliLine(line);
            line = "";
        } else {
            line += c;
            if (line.length() >= 160) {
                appendCliLine(line);
                line = "";
            }
        }
        ++printed;
    }
    if (line.length()) {
        appendCliLine(line);
    }
    if (file.available()) {
        appendCliLine("... truncated");
    }
    file.close();
}

bool writeSdText(const String& inputPath, const String& text, bool append)
{
    if (!ensureSdReady()) {
        appendCliLine(String("sd: ") + sdLastError);
        return false;
    }
    String path = normalizeSdPath(inputPath);
    if (!append && SD.exists(path)) {
        SD.remove(path);
    }
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        appendCliLine(String("sd write: cannot open ") + path);
        return false;
    }
    file.print(text);
    file.print("\n");
    file.close();
    appendCliLine(String(append ? "appended " : "wrote ") + path);
    return true;
}

bool handleSdCliCommand(const String& command, const String& lower)
{
    if (lower == "sd" || lower == "sd status") {
        appendSdStatus();
        return true;
    }
    if (lower == "pwd" || lower == "sd pwd") {
        appendCliLine(sdCwd);
        return true;
    }
    if (lower == "ls" || lower == "dir" || lower == "sd ls" || lower == "sd dir") {
        appendSdList("");
        return true;
    }
    if (lower.startsWith("ls ") || lower.startsWith("dir ")) {
        appendSdList(command.substring(command.indexOf(' ') + 1));
        return true;
    }
    if (lower.startsWith("sd ls ") || lower.startsWith("sd dir ")) {
        appendSdList(command.substring(command.indexOf(' ', 3) + 1));
        return true;
    }
    if (lower.startsWith("cd ") || lower.startsWith("sd cd ")) {
        String arg = lower.startsWith("sd cd ") ? command.substring(6) : command.substring(3);
        String path = normalizeSdPath(arg);
        if (!ensureSdReady()) {
            appendCliLine(String("sd: ") + sdLastError);
            return true;
        }
        File dir = SD.open(path, FILE_READ);
        if (!dir || !dir.isDirectory()) {
            appendCliLine(String("sd cd: not a directory: ") + path);
        } else {
            sdCwd = path;
            appendCliLine(sdCwd);
        }
        return true;
    }
    if (lower.startsWith("cat ") || lower.startsWith("sd cat ")) {
        appendSdCat(lower.startsWith("sd cat ") ? command.substring(7) : command.substring(4));
        return true;
    }
    if (lower.startsWith("sd write ") || lower.startsWith("sd append ")) {
        bool append = lower.startsWith("sd append ");
        size_t baseLen = append ? strlen("sd append ") : strlen("sd write ");
        String rest = command.substring(baseLen);
        rest.trim();
        int split = rest.indexOf(' ');
        if (split <= 0) {
            appendCliLine(String("usage: ") + (append ? "sd append" : "sd write") + " <path> <text>");
            return true;
        }
        writeSdText(rest.substring(0, split), rest.substring(split + 1), append);
        return true;
    }
    if (lower.startsWith("sd mkdir ")) {
        if (!ensureSdReady()) {
            appendCliLine(String("sd: ") + sdLastError);
            return true;
        }
        String path = normalizeSdPath(command.substring(strlen("sd mkdir ")));
        appendCliLine(SD.mkdir(path) ? String("created ") + path : String("sd mkdir failed: ") + path);
        return true;
    }
    if (lower.startsWith("sd rm ")) {
        if (!ensureSdReady()) {
            appendCliLine(String("sd: ") + sdLastError);
            return true;
        }
        String path = normalizeSdPath(command.substring(strlen("sd rm ")));
        appendCliLine(SD.remove(path) ? String("removed ") + path : String("sd rm failed: ") + path);
        return true;
    }
    return false;
}

bool parseScpProfileIndex(String& rest, size_t& index)
{
    rest.trim();
    int split = rest.lastIndexOf(' ');
    if (split < 0) {
        index = activeSsh;
        return activeSsh < config.ssh.size();
    }
    String tail = rest.substring(split + 1);
    for (size_t i = 0; i < tail.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(tail[i]))) {
            index = activeSsh;
            return activeSsh < config.ssh.size();
        }
    }
    size_t parsed = static_cast<size_t>(tail.toInt());
    if (parsed >= config.ssh.size()) {
        return false;
    }
    rest = rest.substring(0, split);
    rest.trim();
    index = parsed;
    return true;
}

bool parseDirectScpEndpoint(const String& endpoint, SshProfile& profile, String& remotePath)
{
    int at = endpoint.indexOf('@');
    int colon = endpoint.indexOf(':', at + 1);
    if (at <= 0 || colon <= at + 1 || colon >= static_cast<int>(endpoint.length()) - 1) {
        return false;
    }
    profile = SshProfile{};
    profile.user = endpoint.substring(0, at);
    profile.host = endpoint.substring(at + 1, colon);
    profile.port = 22;
    profile.terminal = "xterm-256color";
    remotePath = endpoint.substring(colon + 1);
    profile.name = String("direct ") + profile.user + "@" + profile.host;
    inheritSavedSshCredentials(profile);
    return profile.user.length() && profile.host.length() && remotePath.length();
}

bool splitScpPaths(String rest, String& first, String& second, String& trailing)
{
    rest.trim();
    int split = rest.indexOf(' ');
    if (split <= 0) {
        return false;
    }
    first = rest.substring(0, split);
    rest = rest.substring(split + 1);
    rest.trim();
    split = rest.indexOf(' ');
    if (split < 0) {
        second = rest;
        trailing = "";
    } else {
        second = rest.substring(0, split);
        trailing = rest.substring(split + 1);
        trailing.trim();
    }
    first.trim();
    second.trim();
    return first.length() && second.length();
}

bool handleScpCliCommand(const String& command, const String& lower)
{
    if (lower == "scp" || lower == "scp help") {
        appendCliLine("usage:");
        appendCliLine("  scp get <remote> <sd-local> [profile-index]");
        appendCliLine("  scp put <sd-local> <remote> [profile-index]");
        appendCliLine("  scp get user@host:/remote <sd-local> [password]");
        appendCliLine("  scp put <sd-local> user@host:/remote [password]");
        return true;
    }
    const bool isGet = lower.startsWith("scp get ");
    const bool isPut = lower.startsWith("scp put ");
    if (!isGet && !isPut) {
        return false;
    }
    if (!ensureSdReady()) {
        appendCliLine(String("sd: ") + sdLastError);
        return true;
    }
    String rest = command.substring(isGet ? strlen("scp get ") : strlen("scp put "));
    String first;
    String second;
    String trailing;
    if (!splitScpPaths(rest, first, second, trailing)) {
        appendCliLine(String("usage: scp ") + (isGet ? "get" : "put") +
                      (isGet ? " <remote> <sd-local> [profile-index]" : " <sd-local> <remote> [profile-index]"));
        return true;
    }

    SshProfile profile;
    String remotePath;
    String localPath;
    const bool direct = isGet ? parseDirectScpEndpoint(first, profile, remotePath)
                              : parseDirectScpEndpoint(second, profile, remotePath);
    if (direct) {
        if (trailing.length()) {
            profile.password = trailing;
        }
        localPath = isGet ? second : first;
    } else {
        if (config.ssh.empty()) {
            appendCliLine("scp: no SSH profiles");
            return true;
        }
        rest = command.substring(isGet ? strlen("scp get ") : strlen("scp put "));
        size_t profileIndex = activeSsh;
        if (!parseScpProfileIndex(rest, profileIndex)) {
            appendCliLine("scp: invalid profile index");
            return true;
        }
        if (!splitScpPaths(rest, first, second, trailing)) {
            appendCliLine("scp: missing paths");
            return true;
        }
        profile = config.ssh[profileIndex];
        remotePath = isGet ? first : second;
        localPath = isGet ? second : first;
    }

    String error;
    appendCliLine(String("scp ") + (isGet ? "get " : "put ") + profile.user + "@" + profile.host);
    bool ok = false;
    if (isGet) {
        ok = ssh.scpDownload(profile, remotePath, SD, normalizeSdPath(localPath), error);
    } else {
        ok = ssh.scpUpload(profile, SD, normalizeSdPath(localPath), remotePath, error);
    }
    appendCliLine(ok ? "scp: done" : String("scp: failed: ") + error);
    return true;
}

bool handleBleCliCommand(const String&, const String& lower)
{
    if (lower == "ble" || lower == "ble status") {
        appendCliLine(keyboard.bleStatus());
        return true;
    }
    if (lower == "ble scan") {
        String result;
        bool ok = keyboard.bleScan(result);
        appendCliLine(String(ok ? "ble scan: " : "ble scan failed: ") + result);
        return true;
    }
    if (lower.startsWith("ble pair ")) {
        String indexText = lower.substring(strlen("ble pair "));
        indexText.trim();
        String result;
        bool ok = keyboard.blePair(static_cast<size_t>(indexText.toInt()), result);
        appendCliLine(String(ok ? "ble pair: " : "ble pair failed: ") + result);
        return true;
    }
    if (lower == "ble forget") {
        String result;
        bool ok = keyboard.bleForget(result);
        config.keyboard.bleKeyboardName = "";
        config.keyboard.bleKeyboardAddress = "";
        saveConfig();
        appendCliLine(String(ok ? "ble forget: " : "ble forget failed: ") + result);
        return true;
    }
    if (lower == "ble enable" || lower == "ble disable") {
        config.keyboard.bleKeyboardEnabled = lower == "ble enable";
        keyboard.configure(config.keyboard);
        saveConfig();
        appendCliLine(keyboard.bleStatus());
        return true;
    }
    return false;
}

bool handlePythonCliCommand(const String& command, const String& lower)
{
    if (lower == "py" || lower == "python" || lower == "py help" || lower == "python help") {
        appendCliLine("py commands:");
        appendCliLine("  py repl");
        appendCliLine("  py run <sd.py>");
        appendCliLine("  py exec <statement>");
        appendCliLine("  py reset");
        appendCliLine("GPIO subset: Pin, pin(), digitalWrite(), digitalRead(), blink()");
        return true;
    }
    if (lower == "py repl" || lower == "python repl") {
        pythonReplMode = true;
        appendCliLine("MicroPython-compatible REPL subset. Type exit() to return.");
        return true;
    }
    if (lower == "py reset" || lower == "python reset") {
        python.reset();
        appendCliLine("py: state reset");
        return true;
    }
    if (lower.startsWith("py exec ") || lower.startsWith("python exec ")) {
        size_t prefix = lower.startsWith("py exec ") ? strlen("py exec ") : strlen("python exec ");
        String statement = command.substring(prefix);
        if (!python.runLine(statement, appendPythonCliLine)) {
            appendCliLine(String("py: ") + python.lastError());
        }
        return true;
    }
    if (lower.startsWith("py run ") || lower.startsWith("python run ")) {
        if (!ensureSdReady()) {
            appendCliLine(String("sd: ") + sdLastError);
            return true;
        }
        size_t prefix = lower.startsWith("py run ") ? strlen("py run ") : strlen("python run ");
        String path = normalizeSdPath(command.substring(prefix));
        appendCliLine(String("py run ") + path);
        uint32_t start = millis();
        bool ok = python.runFile(SD, path, appendPythonCliLine);
        appendCliLine(ok ? String("py: done in ") + (millis() - start) + " ms"
                         : String("py: failed: ") + python.lastError());
        return true;
    }
    return false;
}

String formattedDateTime()
{
    time_t now = time(nullptr);
    if (now < 1700000000) {
        return "";
    }
    now += static_cast<time_t>(config.system.utcOffsetMinutes) * 60;
    struct tm tmLocal;
    gmtime_r(&now, &tmLocal);
    const int offsetHours = config.system.utcOffsetMinutes / 60;
    const int offsetMinutes = abs(config.system.utcOffsetMinutes % 60);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d UTC%+03d:%02d",
             tmLocal.tm_year + 1900,
             tmLocal.tm_mon + 1,
             tmLocal.tm_mday,
             tmLocal.tm_hour,
             tmLocal.tm_min,
             tmLocal.tm_sec,
             offsetHours,
             offsetMinutes);
    return String(buffer);
}

void startTimeSync(bool force)
{
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    if (!force && timeSyncStarted && millis() - lastTimeSyncAttempt < 3600000UL) {
        return;
    }
    timeSyncStarted = true;
    timeSynced = false;
    lastTimeSyncAttempt = millis();
    configTime(0, 0, config.system.ntpServer.c_str(), "time.nict.jp", "pool.ntp.org");
    setWifiStatus(String("Time sync: ") + config.system.ntpServer);
}

void pollTimeSync()
{
    if (!timeSyncStarted || timeSynced) {
        return;
    }
    if (time(nullptr) >= 1700000000) {
        timeSynced = true;
        appendCliLine(String("[time] ") + formattedDateTime() + " " + config.system.region);
    }
}

bool handleTab5CliCommand(const String& line)
{
    String command = line;
    command.trim();
    String lower = command;
    lower.toLowerCase();

    if (lower == "help" || lower == "?") {
        appendCliHelp();
        return true;
    }
    if (lower == "man") {
        appendCliHelp();
        appendCliLine("Usage: man <command>");
        return true;
    }
    if (lower.startsWith("man ")) {
        return appendCliMan(command.substring(4));
    }
    if (lower == "clear" || lower == "cls" || lower == "reset") {
        terminal.clear();
        return true;
    }
    if (lower == "status") {
        appendCliLine(String("screen=Tab5 CLI wifi=") + wifiStatusText);
        appendCliLine(String("ip=") + WiFi.localIP().toString() + " ssid=" + WiFi.SSID());
        appendCliLine(String("ssh=") + (ssh.connected() ? "connected" : "disconnected"));
        appendCliLine(String("device=") + config.system.deviceName + " region=" + config.system.region);
        appendCliLine(String("time=") + (timeSynced ? formattedDateTime() : "not synced"));
        appendCliLine(String("keymap=") + config.keyboard.layout + " keyboard=" + keyboard.status());
        appendCliLine(String("ble=") + keyboard.bleStatus());
        appendCliLine(String("sd=") + (sdReady ? "ready" : sdLastError));
        appendCliLine(String("activeWifi=") + activeWifi + " activeSsh=" + activeSsh);
        return true;
    }
    if (lower == "history") {
        for (size_t i = 0; i < commandHistory.size(); ++i) {
            appendCliLine(String(i + 1) + "  " + commandHistory[i]);
        }
        return true;
    }
    if (lower == "whoami") {
        appendCliLine(config.system.deviceName);
        return true;
    }
    if (lower == "hostname") {
        appendCliLine(config.system.deviceName);
        return true;
    }
    if (lower == "uname" || lower == "uname -a") {
        appendCliLine("Tab5 CLI tab5 0.1.0 esp32p4 arduino");
        return true;
    }
    if (lower == "date") {
        String text = formattedDateTime();
        if (text.length()) {
            appendCliLine(text + " " + config.system.region);
        } else {
            appendCliLine("time not synced; connect Wi-Fi or run 'time sync'");
            startTimeSync(true);
        }
        return true;
    }
    if (lower == "time sync" || lower == "ntp sync") {
        startTimeSync(true);
        appendCliLine("time sync requested");
        return true;
    }
    if (lower == "uptime") {
        uint32_t seconds = millis() / 1000;
        appendCliLine(String("up ") + (seconds / 3600) + "h " + ((seconds / 60) % 60) + "m " + (seconds % 60) + "s");
        return true;
    }
    if (handleSdCliCommand(command, lower)) {
        return true;
    }
    if (handleScpCliCommand(command, lower)) {
        return true;
    }
    if (handlePythonCliCommand(command, lower)) {
        return true;
    }
    if (handleBleCliCommand(command, lower)) {
        return true;
    }
    if (lower == "wifi status") {
        appendCliLine(wifiStatusText);
        appendCliLine(String("wl=") + static_cast<int>(WiFi.status()) + " ip=" + WiFi.localIP().toString() + " ssid=" + WiFi.SSID());
        return true;
    }
    if (lower == "wifi list") {
        appendWifiList();
        return true;
    }
    if (lower == "ip addr" || lower == "ip a" || lower == "ifconfig") {
        appendCliLine("wlan0:");
        appendCliLine(String("  inet ") + WiFi.localIP().toString());
        appendCliLine(String("  ssid ") + WiFi.SSID());
        appendCliLine(String("  status ") + static_cast<int>(WiFi.status()));
        return true;
    }
    if (lower == "ssh list") {
        appendSshList();
        return true;
    }
    if (lower == "ssh disconnect") {
        ssh.disconnect();
        resetCommandEditor();
        configureTerminal();
        appendCliLine("SSH disconnected");
        return true;
    }
    if (lower.startsWith("ssh connect")) {
        size_t index = 0;
        if (parseTrailingIndex(command, strlen("ssh connect"), index) && index < config.ssh.size()) {
            activeSsh = index;
            saveConfig();
            connectActiveSsh();
        } else {
            appendCliLine("usage: ssh connect <index>");
        }
        return true;
    }
    if (lower.startsWith("echo ")) {
        appendCliLine(command.substring(5));
        return true;
    }
    return false;
}

void executeLocalCommand()
{
    String line = commandLine;
    line.trim();
    terminal.append(String(pythonReplMode ? PythonPrompt : LocalPrompt) + line + "\n");
    rememberCommandHistory(line);
    resetCommandEditor();

    if (pythonReplMode) {
        if (line == "exit()" || line == "quit()" || line == "exit" || line == "quit") {
            pythonReplMode = false;
            appendCliLine("py: exit");
        } else if (line.length() && !python.runLine(line, appendPythonCliLine)) {
            appendCliLine(String("py: ") + python.lastError());
        }
        dirty = true;
        return;
    }

    if (!line.length()) {
        dirty = true;
        return;
    }

    if (handleTab5CliCommand(line)) {
        dirty = true;
        return;
    }

    SshProfile directProfile;
    String error;
    if (parseSshCommand(line, directProfile, error)) {
        connectSshProfile(directProfile);
    } else {
        appendCliLine(String(line) + ": command not found");
        appendCliLine("type 'help' for Tab5 CLI commands, or use ssh user@host[:port]");
    }
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
    drawButton(BtnConfig, "CONF");

    if (screen == Screen::Terminal) {
        drawButton(BtnConnect, ssh.connected() ? "DISC" : "CONN", TFT_WHITE, ssh.connected() ? TFT_MAROON : TFT_DARKGREEN);
    } else if (screen == Screen::WifiEdit) {
        drawButton(BtnConnect, "CONN", TFT_WHITE, TFT_DARKGREEN);
        drawButton(BtnSave, "SAVE", TFT_WHITE, TFT_DARKGREEN);
        drawButton(BtnDelete, "DEL", TFT_WHITE, TFT_MAROON);
    } else if (screen == Screen::SshEdit) {
        drawButton(BtnSave, "SAVE", TFT_WHITE, TFT_DARKGREEN);
        drawButton(BtnDelete, "DEL", TFT_WHITE, TFT_MAROON);
    } else if (screen == Screen::ConfigEdit) {
        drawButton(BtnSave, "SAVE", TFT_WHITE, TFT_DARKGREEN);
    }

    screenSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    String wifiStateText = "WiFi down";
    if (WiFi.status() == WL_CONNECTED) {
        wifiStateText = "WiFi ok";
    } else if (wifiState == WifiConnectState::Connecting) {
        wifiStateText = "WiFi conn";
    } else if (wifiState == WifiConnectState::Failed && wifiRetryAt) {
        wifiStateText = "WiFi retry";
    }
    String status = wifiStateText + (ssh.connected() ? "  SSH ok" : "  SSH down");
    if (screen == Screen::FontList) {
        status = String("Font: ") + terminalFont().label + " line " + terminalLineStep();
    } else if (screen == Screen::ConfigEdit) {
        status = String(config.system.deviceName) + " " + config.system.region;
    } else if (screen == Screen::Terminal && ssh.connected() && vt.scrollbackOffset() > 0) {
        status = String("Scroll +") + vt.scrollbackOffset();
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
    terminal.append("[tab5] ");
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

void appendWifiProgress(const String& message)
{
    appendStatus(message);
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
    appendWifiProgress(wifiLastFailureText);
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
        appendWifiProgress(reason + "; retry 10s");
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
    appendWifiProgress(String("Wi-Fi begin: ") + ssid);
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
        appendWifiProgress("Wi-Fi fail: no profile");
        return;
    }
    if (!ForceFixedWifiForTest && activeWifi >= config.wifi.size()) {
        activeWifi = 0;
    }
    setWifiStatus(ForceFixedWifiForTest ? "Wi-Fi direct test" : "Wi-Fi reconnect");
    appendWifiProgress(ForceFixedWifiForTest ? "Wi-Fi direct test" : "Wi-Fi reconnect");
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
            String ssid = ForceFixedWifiForTest ? String(FixedWifiSsid) : config.wifi[wifiProfileIndex].ssid;
            setWifiStatus(String("Wi-Fi try: ") + ssid);
            appendWifiProgress(String("Wi-Fi try: ") + ssid);
        }
        if (WiFi.status() == WL_CONNECTED) {
            wifiState = WifiConnectState::Connected;
            setWifiStatus(String("Wi-Fi connected: ") + WiFi.SSID() + " " + WiFi.localIP().toString());
            appendWifiProgress(wifiStatusText);
            startTimeSync(false);
            return;
        }
        if (millis() - wifiAttemptStart >= wifiAttemptTimeoutMs) {
            if (wifiWorkerBusy) {
                String ssid = ForceFixedWifiForTest ? String(FixedWifiSsid) : config.wifi[wifiProfileIndex].ssid;
                setWifiStatus(String("Wi-Fi start busy: ") + ssid);
                appendWifiProgress(String("Wi-Fi start busy: ") + ssid);
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
        appendWifiProgress("Wi-Fi lost; reconnect");
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
                appendWifiProgress(reason + String("; retry ") + remaining + "s");
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
    screen = Screen::Terminal;
    dirty = true;
    appendStatus(String("Connecting SSH: ") + config.ssh[activeSsh].host);
    draw();
    setCrashStage("ssh.connect");
    if (ssh.connect(config.ssh[activeSsh], err, static_cast<int>(vt.columns()), static_cast<int>(vt.rows()))) {
        setCrashStage("ssh.connected");
        resetCommandEditor();
        vt.reset();
        vt.scrollbackToBottom();
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
    screen = Screen::Terminal;
    dirty = true;
    appendStatus(String("Connecting SSH: ") + profile.user + "@" + profile.host);
    draw();
    setCrashStage("ssh.connect.direct");
    if (ssh.connect(profile, err, static_cast<int>(vt.columns()), static_cast<int>(vt.rows()))) {
        setCrashStage("ssh.connected");
        resetCommandEditor();
        vt.reset();
        vt.scrollbackToBottom();
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

String configFieldValue(uint8_t field)
{
    if (field == 0) return config.system.deviceName;
    if (field == 1) return config.system.region;
    if (field == 2) return String(config.system.utcOffsetMinutes);
    if (field == 3) return config.system.ntpServer;
    if (field == 4) return config.keyboard.layout;
    if (field == 5) return config.keyboard.bleKeyboardEnabled ? "on" : "off";
    if (field == 6) return config.keyboard.bleKeyboardName;
    return config.keyboard.bleKeyboardAddress;
}

void setConfigFieldValue(uint8_t field, const String& value)
{
    if (field == 0) config.system.deviceName = value.length() ? value : "tab5";
    if (field == 1) config.system.region = value.length() ? value : "Asia/Tokyo";
    if (field == 2) config.system.utcOffsetMinutes = static_cast<int16_t>(constrain(value.toInt(), -720, 840));
    if (field == 3) config.system.ntpServer = value.length() ? value : "pool.ntp.org";
    if (field == 4) {
        String layout = value;
        layout.toLowerCase();
        config.keyboard.layout = layout == "jp" ? "jp" : "us";
    }
    if (field == 5) {
        String enabled = value;
        enabled.toLowerCase();
        config.keyboard.bleKeyboardEnabled = enabled == "on" || enabled == "1" || enabled == "yes" || enabled == "true";
    }
    if (field == 6) config.keyboard.bleKeyboardName = value;
    if (field == 7) config.keyboard.bleKeyboardAddress = value;
}

uint8_t editFieldCount()
{
    if (screen == Screen::WifiEdit) return 3;
    if (screen == Screen::SshEdit) return 6;
    if (screen == Screen::ConfigEdit) return 8;
    return 0;
}

String currentEditFieldValue()
{
    if (screen == Screen::WifiEdit) return wifiFieldValue(editField);
    if (screen == Screen::SshEdit) return sshFieldValue(editField);
    if (screen == Screen::ConfigEdit) return configFieldValue(editField);
    return "";
}

void setCurrentEditFieldValue(const String& value)
{
    if (screen == Screen::WifiEdit) setWifiFieldValue(editField, value);
    if (screen == Screen::SshEdit) setSshFieldValue(editField, value);
    if (screen == Screen::ConfigEdit) setConfigFieldValue(editField, value);
}

bool isChoiceEditField()
{
    return screen == Screen::ConfigEdit && (editField == 4 || editField == 5);
}

void toggleChoiceEditField()
{
    if (!isChoiceEditField()) {
        return;
    }
    if (editField == 4) {
        config.keyboard.layout = config.keyboard.layout == "jp" ? "us" : "jp";
        editCursor = config.keyboard.layout.length();
    } else if (editField == 5) {
        config.keyboard.bleKeyboardEnabled = !config.keyboard.bleKeyboardEnabled;
        editCursor = configFieldValue(editField).length();
    }
    dirty = true;
}

void clampEditCursor()
{
    String value = currentEditFieldValue();
    if (editCursor > value.length()) {
        editCursor = value.length();
    }
}

void setEditCursorToEnd()
{
    editCursor = currentEditFieldValue().length();
}

void moveEditCursor(int delta)
{
    clampEditCursor();
    String value = currentEditFieldValue();
    if (delta < 0) {
        if (!editCursor) return;
        --editCursor;
        while (editCursor > 0 && (static_cast<uint8_t>(value[editCursor]) & 0xC0) == 0x80) {
            --editCursor;
        }
    } else if (delta > 0 && editCursor < value.length()) {
        editCursor += utf8CharLength(static_cast<uint8_t>(value[editCursor]));
        if (editCursor > value.length()) editCursor = value.length();
    }
    cursorVisible = true;
    lastCursorBlink = millis();
    dirty = true;
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
            String prefix = String(pythonReplMode ? PythonPrompt : LocalPrompt) + commandLine.substring(0, commandCursor);
            String cursorGlyph = " ";
            String suffix = "";
            if (commandCursor < commandLine.length()) {
                size_t next = commandCursor + utf8CharLength(static_cast<uint8_t>(commandLine[commandCursor]));
                cursorGlyph = commandLine.substring(commandCursor, next);
                suffix = commandLine.substring(next);
            }

            size_t visibleStart = 0;
            while (visibleStart < prefix.length() &&
                   terminalTextWidth(prefix.substring(visibleStart)) > screenSprite.width() - 16) {
                ++visibleStart;
                while (visibleStart < prefix.length() &&
                       (static_cast<uint8_t>(prefix[visibleStart]) & 0xC0) == 0x80) {
                    ++visibleStart;
                }
            }

            String visiblePrefix = prefix.substring(visibleStart);
            int x = 4;
            x += drawTerminalText(visiblePrefix, x, lineTop, lineStep, TFT_GREEN, TFT_BLACK);
            if (cursorVisible) {
                int cursorW = max<int>(terminalCellWidth(), terminalTextWidth(cursorGlyph));
                screenSprite.fillRect(x, lineTop, cursorW, lineStep, TFT_GREEN);
                drawTerminalText(cursorGlyph, x, lineTop, lineStep, TFT_BLACK, TFT_GREEN);
                x += cursorW;
            } else {
                x += drawTerminalText(cursorGlyph, x, lineTop, lineStep, TFT_GREEN, TFT_BLACK);
            }
            if (suffix.length()) {
                drawTerminalText(suffix, x, lineTop, lineStep, TFT_GREEN, TFT_BLACK);
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
        String rawValue;
        if (screen == Screen::WifiEdit) rawValue = wifiFieldValue(i);
        else if (screen == Screen::SshEdit) rawValue = sshFieldValue(i);
        else rawValue = configFieldValue(i);
        bool secret = (screen == Screen::SshEdit && i == 4) || (screen == Screen::WifiEdit && i == 2);
        bool choiceField = screen == Screen::ConfigEdit && (i == 4 || i == 5);
        String value = safeValue(rawValue, secret);
        size_t cursor = (i == editField && !choiceField) ? min(editCursor, rawValue.length()) : rawValue.length();
        if (secret) cursor = min(cursor, value.length());
        String prefix = value.substring(0, cursor);
        String cursorGlyph = " ";
        String suffix = "";
        if (cursor < value.length()) {
            size_t next = cursor + utf8CharLength(static_cast<uint8_t>(value[cursor]));
            cursorGlyph = value.substring(cursor, next);
            suffix = value.substring(next);
        }
        while (prefix.length() && screenSprite.textWidth(prefix + cursorGlyph + suffix) > screenSprite.width() - 195) {
            uint8_t len = utf8CharLength(static_cast<uint8_t>(prefix[0]));
            prefix = prefix.substring(len);
        }
        setSettingsFontForLine(value);
        int x = 180;
        int textY = settingTextY(y);
        screenSprite.drawString(prefix, x, textY);
        x += screenSprite.textWidth(prefix);
        if (i == editField && !choiceField && cursorVisible) {
            int cursorW = max<int>(terminalCellWidth(), screenSprite.textWidth(cursorGlyph));
            screenSprite.fillRect(x, y + 3, cursorW, settingRowH() - 10, TFT_GREEN);
            screenSprite.setTextColor(TFT_BLACK, TFT_GREEN);
            screenSprite.drawString(cursorGlyph, x, textY);
            screenSprite.setTextColor(TFT_WHITE, bg);
            x += cursorW;
        } else {
            screenSprite.drawString(cursorGlyph, x, textY);
            x += screenSprite.textWidth(cursorGlyph);
        }
        if (suffix.length()) {
            screenSprite.drawString(suffix, x, textY);
        }
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
        String line = String(selected ? "* " : "  ") + TerminalFonts[i].label + "  default line " +
                      TerminalFonts[i].defaultLineStep + " px";
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
    } else if (screen == Screen::ConfigEdit) {
        static const char* const labels[] = {"Device", "Region", "UTC min", "NTP", "Keymap", "BLE KB", "BLE Name", "BLE Addr"};
        drawEditFields("Config", labels, 8, false);
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
    setEditCursorToEnd();
    dirty = true;
}

void beginSshEdit(size_t index, bool isNew)
{
    editIndex = index;
    editField = 0;
    editIsNew = isNew;
    screen = Screen::SshEdit;
    setEditCursorToEnd();
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
        } else if (screen == Screen::ConfigEdit) {
            startTimeSync(true);
            keyboard.configure(config.keyboard);
            screen = Screen::Terminal;
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
    } else if (headerButtonContains(BtnConfig, x, y)) {
        screen = Screen::ConfigEdit;
        editField = 0;
        setEditCursorToEnd();
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
    } else if ((screen == Screen::WifiEdit || screen == Screen::SshEdit || screen == Screen::ConfigEdit) &&
               headerButtonContains(BtnSave, x, y)) {
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
    uint8_t maxField = editFieldCount();
    if (field < maxField) {
        bool toggle = screen == Screen::ConfigEdit && (field == 4 || field == 5);
        editField = field;
        setEditCursorToEnd();
        if (toggle) {
            toggleChoiceEditField();
        }
        dirty = true;
    }
}

void handleTouch()
{
    auto touch = M5.Touch.getDetail();
    if (screen == Screen::Terminal) {
        if (touch.wasPressed()) {
            touchScrollRemainderY = 0;
            touchScrollActive = false;
        }
        if (touch.isFlicking() || touch.isDragging()) {
            touchScrollRemainderY += touch.deltaY();
            const int step = max<int>(1, terminalLineStep());
            int lines = touchScrollRemainderY / step;
            if (lines) {
                if (ssh.connected()) {
                    vt.scrollback(lines);
                } else {
                    terminal.scroll(lines);
                }
                touchScrollRemainderY -= lines * step;
                touchScrollActive = true;
                dirty = true;
                return;
            }
            if (touchScrollActive) {
                return;
            }
        }
        if (touch.wasReleased()) {
            bool consumed = touchScrollActive;
            touchScrollRemainderY = 0;
            touchScrollActive = false;
            if (consumed) {
                return;
            }
        }
        if (touch.wasFlicked() || touch.wasDragged()) {
            touchScrollRemainderY = 0;
            if (touchScrollActive) {
                touchScrollActive = false;
                return;
            }
        }
    } else {
        touchScrollRemainderY = 0;
        touchScrollActive = false;
    }

    if (screen == Screen::Terminal && touch.wasFlicked() && !touchScrollActive) {
        const int dy = touch.distanceY();
        if (abs(dy) > 30) {
            int lines = max<int>(3, abs(dy) / max<int>(1, terminalLineStep()));
            if (ssh.connected()) {
                vt.scrollback(dy > 0 ? lines : -lines);
            } else {
                terminal.scroll(dy > 0 ? lines : -lines);
            }
            dirty = true;
            return;
        }
    }

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
    } else if (screen == Screen::WifiEdit || screen == Screen::SshEdit || screen == Screen::ConfigEdit) {
        handleEditTouch(x, y);
    }
}

void editAppendChar(char c)
{
    if (screen != Screen::WifiEdit && screen != Screen::SshEdit && screen != Screen::ConfigEdit) {
        return;
    }

    const uint8_t maxField = editFieldCount();
    if (c == '\r' || c == '\n') {
        saveEditingProfile();
        return;
    }
    if (c == '\t') {
        editField = (editField + 1) % maxField;
        setEditCursorToEnd();
        dirty = true;
        return;
    }
    if (isChoiceEditField()) {
        if (c == ' ' || c == '+' || c == '-') {
            toggleChoiceEditField();
        }
        return;
    }

    String value = currentEditFieldValue();
    clampEditCursor();
    if (c == 0x08 || c == 0x7F) {
        if (editCursor > 0) {
            size_t pos = editCursor - 1;
            while (pos > 0 && (static_cast<uint8_t>(value[pos]) & 0xC0) == 0x80) {
                --pos;
            }
            value = value.substring(0, pos) + value.substring(editCursor);
            editCursor = pos;
        }
    } else if (std::isprint(static_cast<unsigned char>(c))) {
        bool numeric = (screen == Screen::SshEdit && editField == 2) || (screen == Screen::ConfigEdit && editField == 2);
        bool allowed = !numeric || std::isdigit(static_cast<unsigned char>(c)) ||
                       (screen == Screen::ConfigEdit && editField == 2 && c == '-' && editCursor == 0);
        if (allowed) {
            value = value.substring(0, editCursor) + String(c) + value.substring(editCursor);
            ++editCursor;
        }
    }

    setCurrentEditFieldValue(value);
    dirty = true;
}

void moveEditField(int delta)
{
    if (screen != Screen::WifiEdit && screen != Screen::SshEdit && screen != Screen::ConfigEdit) {
        return;
    }
    const uint8_t maxField = editFieldCount();
    int next = static_cast<int>(editField) + delta;
    if (next < 0) {
        next = maxField - 1;
    } else if (next >= maxField) {
        next = 0;
    }
    editField = static_cast<uint8_t>(next);
    setEditCursorToEnd();
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
                vt.scrollbackToBottom();
                sendSshText(action.text);
                trackRemoteCommandText(action.text);
                cursorVisible = true;
                lastCursorBlink = millis();
                vt.markCursorDirty();
            } else {
                handleDisconnectedTerminalText(action);
            }
            dirty = true;
            break;
        case KeyActionType::Scroll:
            if (ssh.connected()) {
                vt.scrollback(action.value);
            } else {
                terminal.scroll(action.value);
            }
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

    if (screen == Screen::WifiEdit || screen == Screen::SshEdit || screen == Screen::ConfigEdit) {
        if (isTabKey(action) || isDownKey(action)) {
            moveEditField(1);
        } else if (isUpKey(action)) {
            moveEditField(-1);
        } else if (isLeftKey(action)) {
            if (isChoiceEditField()) toggleChoiceEditField();
            else moveEditCursor(-1);
        } else if (isRightKey(action)) {
            if (isChoiceEditField()) toggleChoiceEditField();
            else moveEditCursor(1);
        } else if (action.type == KeyActionType::Text) {
            for (size_t i = 0; i < action.text.length(); ++i) {
                editAppendChar(action.text[i]);
            }
        } else if (action.type == KeyActionType::Menu) {
            if (screen == Screen::WifiEdit) screen = Screen::WifiList;
            else if (screen == Screen::SshEdit) screen = Screen::SshList;
            else screen = Screen::Terminal;
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
    bool received = false;
    uint32_t start = millis();
    for (uint8_t reads = 0; reads < 8 && millis() - start < 8; ++reads) {
        setCrashStage("ssh.read");
        int n = ssh.read(buffer, sizeof(buffer));
        if (n > 0) {
            setCrashStage("ssh.append");
            vt.write(buffer, static_cast<size_t>(n));
            received = true;
            lastSshReceive = millis();
            continue;
        }
        if (n < 0) {
            setCrashStage("ssh.read.error");
            ssh.disconnect();
            resetCommandEditor();
            configureTerminal();
            appendStatus("SSH disconnected by remote");
            setCrashStage("loop");
            return;
        }
        break;
    }
    if (received) {
        dirty = true;
    }
    setCrashStage("loop");
}

void serialPrintHelp()
{
    Serial.println("Tab5 SSH serial API");
    Serial.println("  help");
    Serial.println("  status");
    Serial.println("  crash");
    Serial.println("  sd status");
    Serial.println("  sd ls [path]");
    Serial.println("  sd cat <path>");
    Serial.println("  sd write <path> <text>");
    Serial.println("  sd append <path> <text>");
    Serial.println("  wifi status");
    Serial.println("  ssh list");
    Serial.println("  ssh active <index>");
    Serial.println("  ssh connect [index]");
    Serial.println("  ssh send <text>");
    Serial.println("  ssh raw <hex bytes>");
    Serial.println("  ssh disconnect");
    Serial.println("  scp get <remote> <sd-local> [profile-index]");
    Serial.println("  scp put <sd-local> <remote> [profile-index]");
    Serial.println("  scp get user@host:/remote <sd-local> [password]");
    Serial.println("  scp put <sd-local> user@host:/remote [password]");
    Serial.println("  ble status|enable|disable|scan|pair <index>|forget");
    Serial.println("  py exec <statement>");
    Serial.println("  py run <sd.py>");
    Serial.println("  py reset");
    Serial.println("  term dump");
}

void serialPrintStatus()
{
    Serial.printf("screen=%u wifi=%s wl=%d ssh=%s activeWifi=%u activeSsh=%u keymap=%s sd=%s keyboard=%s ble=%s stage=%s\r\n",
                  static_cast<unsigned>(screen),
                  wifiStatusText.c_str(),
                  static_cast<int>(WiFi.status()),
                  ssh.connected() ? "connected" : "disconnected",
                  static_cast<unsigned>(activeWifi),
                  static_cast<unsigned>(activeSsh),
                  config.keyboard.layout.c_str(),
                  sdReady ? "ready" : sdLastError.c_str(),
                  keyboard.status().c_str(),
                  keyboard.bleStatus().c_str(),
                  crashStage);
}

void serialPrintSdStatus()
{
    if (!ensureSdReady()) {
        Serial.printf("sd=not ready error=%s\r\n", sdLastError.c_str());
        return;
    }
    Serial.printf("sd=ready type=%u size=%llu used=%llu total=%llu cwd=%s\r\n",
                  static_cast<unsigned>(SD.cardType()),
                  static_cast<unsigned long long>(SD.cardSize()),
                  static_cast<unsigned long long>(SD.usedBytes()),
                  static_cast<unsigned long long>(SD.totalBytes()),
                  sdCwd.c_str());
}

void serialPrintSdList(const String& inputPath)
{
    if (!ensureSdReady()) {
        Serial.printf("ERR sd %s\r\n", sdLastError.c_str());
        return;
    }
    String path = normalizeSdPath(inputPath);
    File root = SD.open(path, FILE_READ);
    if (!root) {
        Serial.printf("ERR cannot open %s\r\n", path.c_str());
        return;
    }
    if (!root.isDirectory()) {
        Serial.printf("FILE %s %u\r\n", path.c_str(), static_cast<unsigned>(root.size()));
        root.close();
        return;
    }
    Serial.printf("DIR %s\r\n", path.c_str());
    File file = root.openNextFile();
    while (file) {
        Serial.printf("%c %u %s\r\n", file.isDirectory() ? 'd' : 'f',
                      static_cast<unsigned>(file.size()),
                      file.name());
        file = root.openNextFile();
    }
    root.close();
}

void serialPrintSdCat(const String& inputPath)
{
    if (!ensureSdReady()) {
        Serial.printf("ERR sd %s\r\n", sdLastError.c_str());
        return;
    }
    String path = normalizeSdPath(inputPath);
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        Serial.printf("ERR cannot open %s\r\n", path.c_str());
        return;
    }
    Serial.printf("BEGIN %s %u\r\n", path.c_str(), static_cast<unsigned>(file.size()));
    size_t sent = 0;
    while (file.available() && sent < 4096) {
        Serial.write(file.read());
        ++sent;
    }
    file.close();
    Serial.println();
    Serial.println(sent >= 4096 ? "END truncated" : "END");
}

void serialWriteSdText(const String& command, bool append)
{
    if (!ensureSdReady()) {
        Serial.printf("ERR sd %s\r\n", sdLastError.c_str());
        return;
    }
    size_t prefix = append ? strlen("sd append ") : strlen("sd write ");
    String rest = command.substring(prefix);
    rest.trim();
    int split = rest.indexOf(' ');
    if (split <= 0) {
        Serial.println("ERR usage");
        return;
    }
    String path = normalizeSdPath(rest.substring(0, split));
    String text = rest.substring(split + 1);
    if (!append && SD.exists(path)) {
        SD.remove(path);
    }
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        Serial.printf("ERR cannot open %s\r\n", path.c_str());
        return;
    }
    file.print(text);
    file.print("\n");
    file.close();
    Serial.printf("OK %s %s\r\n", append ? "appended" : "wrote", path.c_str());
}

void serialRunScpCommand(const String& command)
{
    String lower = command;
    lower.toLowerCase();
    const bool isGet = lower.startsWith("scp get ");
    const bool isPut = lower.startsWith("scp put ");
    if (!isGet && !isPut) {
        Serial.println("ERR usage");
        return;
    }
    if (!ensureSdReady()) {
        Serial.printf("ERR sd %s\r\n", sdLastError.c_str());
        return;
    }
    String rest = command.substring(isGet ? strlen("scp get ") : strlen("scp put "));
    String first;
    String second;
    String trailing;
    if (!splitScpPaths(rest, first, second, trailing)) {
        Serial.println("ERR missing paths");
        return;
    }

    SshProfile profile;
    String remotePath;
    String localPath;
    const bool direct = isGet ? parseDirectScpEndpoint(first, profile, remotePath)
                              : parseDirectScpEndpoint(second, profile, remotePath);
    if (direct) {
        if (trailing.length()) {
            profile.password = trailing;
        }
        localPath = isGet ? second : first;
    } else {
        if (config.ssh.empty()) {
            Serial.println("ERR no SSH profiles");
            return;
        }
        rest = command.substring(isGet ? strlen("scp get ") : strlen("scp put "));
        size_t profileIndex = activeSsh;
        if (!parseScpProfileIndex(rest, profileIndex)) {
            Serial.println("ERR invalid profile index");
            return;
        }
        if (!splitScpPaths(rest, first, second, trailing)) {
            Serial.println("ERR missing paths");
            return;
        }
        profile = config.ssh[profileIndex];
        remotePath = isGet ? first : second;
        localPath = isGet ? second : first;
    }
    String error;
    bool ok = isGet
        ? ssh.scpDownload(profile, remotePath, SD, normalizeSdPath(localPath), error)
        : ssh.scpUpload(profile, SD, normalizeSdPath(localPath), remotePath, error);
    Serial.println(ok ? "OK scp done" : String("ERR scp failed: ") + error);
}

void serialRunBleCommand(const String& command)
{
    String lower = command;
    lower.toLowerCase();
    if (lower == "ble status") {
        Serial.println(keyboard.bleStatus());
    } else if (lower == "ble enable" || lower == "ble disable") {
        config.keyboard.bleKeyboardEnabled = lower == "ble enable";
        keyboard.configure(config.keyboard);
        saveConfig();
        Serial.println(keyboard.bleStatus());
    } else if (lower == "ble scan") {
        String result;
        bool ok = keyboard.bleScan(result);
        Serial.println(String(ok ? "OK " : "ERR ") + result);
    } else if (lower.startsWith("ble pair ")) {
        String indexText = lower.substring(strlen("ble pair "));
        indexText.trim();
        String result;
        bool ok = keyboard.blePair(static_cast<size_t>(indexText.toInt()), result);
        Serial.println(String(ok ? "OK " : "ERR ") + result);
    } else if (lower == "ble forget") {
        String result;
        bool ok = keyboard.bleForget(result);
        config.keyboard.bleKeyboardName = "";
        config.keyboard.bleKeyboardAddress = "";
        saveConfig();
        Serial.println(String(ok ? "OK " : "ERR ") + result);
    } else {
        Serial.println("ERR usage");
    }
}

void serialRunPythonCommand(const String& command)
{
    String lower = command;
    lower.toLowerCase();
    if (lower == "py reset") {
        python.reset();
        Serial.println("OK py state reset");
    } else if (lower.startsWith("py exec ")) {
        String statement = command.substring(strlen("py exec "));
        bool ok = python.runLine(statement, serialPythonLine);
        Serial.println(ok ? "OK py exec" : String("ERR py ") + python.lastError());
    } else if (lower.startsWith("py run ")) {
        if (!ensureSdReady()) {
            Serial.printf("ERR sd %s\r\n", sdLastError.c_str());
            return;
        }
        String path = normalizeSdPath(command.substring(strlen("py run ")));
        uint32_t start = millis();
        bool ok = python.runFile(SD, path, serialPythonLine);
        Serial.println(ok ? String("OK py done ") + (millis() - start) + " ms"
                          : String("ERR py ") + python.lastError());
    } else {
        Serial.println("ERR usage");
    }
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
    } else if (command == "sd status") {
        serialPrintSdStatus();
    } else if (command == "sd ls" || command.startsWith("sd ls ")) {
        serialPrintSdList(command.length() > strlen("sd ls") ? command.substring(strlen("sd ls ")) : "");
    } else if (command.startsWith("sd cat ")) {
        serialPrintSdCat(command.substring(strlen("sd cat ")));
    } else if (command.startsWith("sd write ")) {
        serialWriteSdText(command, false);
    } else if (command.startsWith("sd append ")) {
        serialWriteSdText(command, true);
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
    } else if (command.startsWith("scp ")) {
        serialRunScpCommand(command);
    } else if (command == "ble status" || command == "ble enable" || command == "ble disable" ||
               command == "ble scan" || command == "ble forget" || command.startsWith("ble pair ")) {
        serialRunBleCommand(command);
    } else if (command == "py reset" || command.startsWith("py exec ") || command.startsWith("py run ")) {
        serialRunPythonCommand(command);
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

void tab5SshProgress(const char* stage)
{
    static uint32_t lastProgressDraw = 0;
    appendStatus(String("SSH: ") + stage);
    uint32_t now = millis();
    if (now - lastProgressDraw >= 300 || strcmp(stage, "ssh_connect") == 0 || strcmp(stage, "ssh_ready") == 0) {
        lastProgressDraw = now;
        draw();
    }
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
    terminal.append("Tab5 CLI\n");
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
    if (ensureSdReady()) {
        appendStatus(String("SD ready: ") + formatBytes(SD.cardSize()));
    } else {
        appendStatus(sdLastError);
    }
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
    pollTimeSync();
    pollSsh();

    if ((screen == Screen::Terminal || screen == Screen::WifiEdit || screen == Screen::SshEdit ||
         screen == Screen::ConfigEdit) &&
        millis() - lastCursorBlink >= 500) {
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

    uint32_t drawInterval = 5;
    if (ssh.connected() && millis() - lastSshReceive < 25 && millis() - lastDraw < 250) {
        drawInterval = 25;
    }

    if (dirty && millis() - lastDraw > drawInterval) {
        lastDraw = millis();
        draw();
    } else if (headerDirty && millis() - lastDraw > drawInterval) {
        lastDraw = millis();
        drawHeaderOnly();
    }
    delay(2);
}
