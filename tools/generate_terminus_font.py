from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = Path.home() / "AppData" / "Local" / "Temp" / "terminus-font-git"
OUT_H = ROOT / "src" / "fonts" / "TerminusBitmap.hpp"
OUT_CPP = ROOT / "src" / "fonts" / "TerminusBitmap.cpp"

RANGES = [
    (0x20, 0x7E),
    (0x2500, 0x259F),
    (0xE0A0, 0xE0A3),
    (0xE0B0, 0xE0B3),
]

SIZES = [
    ("term16", "ter-u16n.bdf", 8, 16),
    ("term20", "ter-u20n.bdf", 10, 20),
    ("term28", "ter-u28n.bdf", 14, 28),
    ("term36", "ter-u32n.bdf", 18, 36),
]


def wanted(cp: int) -> bool:
    return any(lo <= cp <= hi for lo, hi in RANGES)


def parse_bdf(path: Path) -> dict[int, tuple[int, int, list[int]]]:
    glyphs = {}
    lines = path.read_text(encoding="ascii", errors="ignore").splitlines()
    i = 0
    while i < len(lines):
        if not lines[i].startswith("STARTCHAR"):
            i += 1
            continue
        cp = None
        width = 0
        height = 0
        bitmap = []
        i += 1
        while i < len(lines) and lines[i] != "ENDCHAR":
            line = lines[i]
            if line.startswith("ENCODING "):
                cp = int(line.split()[1])
            elif line.startswith("BBX "):
                parts = line.split()
                width = int(parts[1])
                height = int(parts[2])
            elif line == "BITMAP":
                i += 1
                while i < len(lines) and lines[i] != "ENDCHAR":
                    bitmap.append(int(lines[i], 16))
                    i += 1
                break
            i += 1
        if cp is not None and wanted(cp):
            glyphs[cp] = (width, height, bitmap)
        i += 1
    return glyphs


def emit_font(name: str, glyphs: dict[int, tuple[int, int, list[int]]], target_w: int, target_h: int):
    codes = sorted(glyphs)
    row_bytes = (glyphs[codes[0]][0] + 7) // 8
    offsets = []
    data = bytearray()
    for cp in codes:
        width, height, bitmap = glyphs[cp]
        if (width + 7) // 8 != row_bytes:
            raise RuntimeError(f"mixed row byte width in {name}: U+{cp:04X}")
        offsets.append(len(data))
        for row in bitmap[:height]:
            for b in range(row_bytes):
                shift = (row_bytes - b - 1) * 8
                data.append((row >> shift) & 0xFF)
    offsets.append(len(data))
    return {
        "name": name,
        "codes": codes,
        "offsets": offsets,
        "data": data,
        "src_w": glyphs[codes[0]][0],
        "src_h": glyphs[codes[0]][1],
        "target_w": target_w,
        "target_h": target_h,
        "row_bytes": row_bytes,
    }


def byte_lines(values, per_line=16):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(f"0x{v:02X}" for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def int_lines(values, fmt, per_line=12):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(fmt.format(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def main():
    fonts = []
    for name, filename, target_w, target_h in SIZES:
        glyphs = parse_bdf(SRC / filename)
        fonts.append(emit_font(name, glyphs, target_w, target_h))

    OUT_H.write_text("""#pragma once

#include <Arduino.h>
#include <M5GFX.h>

namespace TerminusBitmap {

struct Font {
    uint8_t srcW;
    uint8_t srcH;
    uint8_t targetW;
    uint8_t targetH;
    uint8_t rowBytes;
    uint16_t glyphCount;
    const uint16_t* codepoints;
    const uint32_t* offsets;
    const uint8_t* bitmap;
};

const Font& fontForHeight(uint8_t targetHeight);
bool hasGlyph(const Font& font, uint32_t cp);
bool drawGlyph(M5Canvas& canvas, const Font& font, uint32_t cp, int x, int y, uint16_t fg);

} // namespace TerminusBitmap
""", encoding="utf-8")

    parts = [
        '#include "TerminusBitmap.hpp"\n\n',
        "// Generated from Terminus Font BDF files.\n",
        "// Terminus Font copyright (C) Dimitar Toshkov Zhekov.\n",
        "// Licensed under the SIL Open Font License, Version 1.1.\n\n",
        "namespace TerminusBitmap {\n\n",
    ]
    for f in fonts:
        parts.append(f"static const uint16_t {f['name']}Codepoints[] PROGMEM = {{\n")
        parts.append(int_lines(f["codes"], "0x{:04X}", 10))
        parts.append("\n};\n\n")
        parts.append(f"static const uint32_t {f['name']}Offsets[] PROGMEM = {{\n")
        parts.append(int_lines(f["offsets"], "{}", 10))
        parts.append("\n};\n\n")
        parts.append(f"static const uint8_t {f['name']}Bitmap[] PROGMEM = {{\n")
        parts.append(byte_lines(f["data"]))
        parts.append("\n};\n\n")
        parts.append(
            f"static const Font {f['name']} = "
            f"{{{f['src_w']}, {f['src_h']}, {f['target_w']}, {f['target_h']}, {f['row_bytes']}, "
            f"{len(f['codes'])}, {f['name']}Codepoints, {f['name']}Offsets, {f['name']}Bitmap}};\n\n"
        )

    parts.append("""const Font& fontForHeight(uint8_t targetHeight)
{
    if (targetHeight <= 16) return term16;
    if (targetHeight <= 20) return term20;
    if (targetHeight <= 28) return term28;
    return term36;
}

static int findGlyph(const Font& font, uint32_t cp)
{
    int lo = 0;
    int hi = static_cast<int>(font.glyphCount) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t value = pgm_read_word(&font.codepoints[mid]);
        if (value == cp) return mid;
        if (value < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

bool hasGlyph(const Font& font, uint32_t cp)
{
    return cp <= 0xFFFF && findGlyph(font, cp) >= 0;
}

bool drawGlyph(M5Canvas& canvas, const Font& font, uint32_t cp, int x, int y, uint16_t fg)
{
    int glyph = findGlyph(font, cp);
    if (glyph < 0) return false;
    uint32_t offset = pgm_read_dword(&font.offsets[glyph]);
    const int xScale = font.targetW == font.srcW ? 1 : 0;
    const int yScale = font.targetH == font.srcH ? 1 : 0;
    for (uint8_t sy = 0; sy < font.srcH; ++sy) {
        int dy0 = yScale ? y + sy : y + (sy * font.targetH) / font.srcH;
        int dy1 = yScale ? dy0 + 1 : y + ((sy + 1) * font.targetH) / font.srcH;
        if (dy1 <= dy0) dy1 = dy0 + 1;
        for (uint8_t sx = 0; sx < font.srcW; ++sx) {
            uint8_t row = pgm_read_byte(&font.bitmap[offset + sy * font.rowBytes + sx / 8]);
            if (!(row & (0x80 >> (sx % 8)))) continue;
            int dx0 = xScale ? x + sx : x + (sx * font.targetW) / font.srcW;
            int dx1 = xScale ? dx0 + 1 : x + ((sx + 1) * font.targetW) / font.srcW;
            if (dx1 <= dx0) dx1 = dx0 + 1;
            canvas.fillRect(dx0, dy0, dx1 - dx0, dy1 - dy0, fg);
        }
    }
    return true;
}

} // namespace TerminusBitmap
""")
    OUT_CPP.write_text("".join(parts), encoding="utf-8")


if __name__ == "__main__":
    main()
