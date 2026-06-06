#!/usr/bin/env python3
import argparse
import collections
import os
import re
import struct
import unicodedata


JPDICT_RECORD = struct.Struct("<96sHBBIHIHIIii")
KANJI_RECORD = struct.Struct("<64sHIIH")


def parse_c_string_bytes(text):
    out = bytearray()
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != "\\":
            out.append(ord(ch) & 0xFF)
            i += 1
            continue

        i += 1
        if i >= len(text):
            break
        esc = text[i]
        i += 1

        if esc in "01234567":
            digits = esc
            while i < len(text) and len(digits) < 3 and text[i] in "01234567":
                digits += text[i]
                i += 1
            out.append(int(digits, 8) & 0xFF)
        elif esc == "x":
            digits = ""
            while i < len(text) and text[i] in "0123456789abcdefABCDEF":
                digits += text[i]
                i += 1
            out.append((int(digits, 16) if digits else 0) & 0xFF)
        elif esc == "n":
            out.append(10)
        elif esc == "r":
            out.append(13)
        elif esc == "t":
            out.append(9)
        elif esc == "b":
            out.append(8)
        elif esc == "f":
            out.append(12)
        elif esc == "v":
            out.append(11)
        elif esc == "a":
            out.append(7)
        else:
            out.append(ord(esc) & 0xFF)
    return bytes(out)


def extract_font_array(path, symbol):
    with open(path, "r", encoding="latin-1") as f:
        source = f.read()

    start_match = re.search(
        rf"PROGMEM\s+const\s+uint8_t\s+{re.escape(symbol)}\[(\d+)\]\s*=",
        source,
    )
    if not start_match:
        raise RuntimeError(f"Could not find font array {symbol} in {path}")

    declared_size = int(start_match.group(1))
    end_match = re.search(r'"\s*;\s*(?:#endif|/\*)', source[start_match.end():])
    if not end_match:
        raise RuntimeError(f"Could not find end of font array {symbol}")
    body = source[start_match.end():start_match.end() + end_match.end()]

    data = bytearray()
    for match in re.finditer(r'"((?:\\.|[^"\\])*)"', body, flags=re.S):
        data.extend(parse_c_string_bytes(match.group(1)))

    if len(data) + 1 == declared_size:
        data.append(0)
    if len(data) != declared_size:
        raise RuntimeError(
            f"{symbol} size mismatch: parsed {len(data)} bytes, "
            f"declaration says {declared_size}"
        )
    return bytes(data)


class U8g2Font:
    def __init__(self, data):
        if len(data) < 23:
            raise ValueError("U8g2 font data is too short")
        self.data = data
        self._coverage_cache = {}

    def _u16be(self, offset):
        if offset + 1 >= len(self.data):
            return 0
        return (self.data[offset] << 8) | self.data[offset + 1]

    @property
    def start_pos_upper_a(self):
        return self._u16be(17)

    @property
    def start_pos_lower_a(self):
        return self._u16be(19)

    @property
    def start_pos_unicode(self):
        return self._u16be(21)

    def has_glyph(self, codepoint):
        cached = self._coverage_cache.get(codepoint)
        if cached is not None:
            return cached
        found = self._has_glyph_uncached(codepoint)
        self._coverage_cache[codepoint] = found
        return found

    def _has_glyph_uncached(self, codepoint):
        if codepoint < 0 or codepoint > 0xFFFF:
            return False

        data = self.data
        font = 23
        if codepoint <= 255:
            if codepoint >= ord("a"):
                font += self.start_pos_lower_a
            elif codepoint >= ord("A"):
                font += self.start_pos_upper_a

            while font + 1 < len(data) and data[font + 1] != 0:
                if data[font] == codepoint:
                    return True
                font += data[font + 1]
            return False

        font += self.start_pos_unicode
        unicode_lut = font
        while unicode_lut + 3 < len(data):
            font += self._u16be(unicode_lut)
            e = self._u16be(unicode_lut + 2)
            unicode_lut += 4
            if e >= codepoint:
                break
        else:
            return False

        while font + 2 < len(data):
            e = self._u16be(font)
            if e == 0:
                return False
            if e == codepoint:
                return True
            advance = data[font + 2]
            if advance == 0:
                return False
            font += advance
        return False


def read_string(strings_data, offset, length):
    return strings_data[offset:offset + length].decode("utf-8", errors="replace")


def scan_jpdict(path):
    records_path = os.path.join(path, "records.bin")
    strings_path = os.path.join(path, "strings.bin")
    with open(records_path, "rb") as f:
        records_data = f.read()
    with open(strings_path, "rb") as f:
        strings_data = f.read()

    count = len(records_data) // JPDICT_RECORD.size
    for i in range(count):
        fields = JPDICT_RECORD.unpack_from(records_data, i * JPDICT_RECORD.size)
        yield "jpdict.term", read_string(strings_data, fields[4], fields[5])
        yield "jpdict.reading", read_string(strings_data, fields[6], fields[7])
        yield "jpdict.definition", read_string(strings_data, fields[8], fields[9])


def scan_kanji(path):
    records_path = os.path.join(path, "lookup.records.bin")
    strings_path = os.path.join(path, "lookup.strings.bin")
    consolidated = os.path.exists(records_path)
    if not consolidated:
        records_path = os.path.join(path, "records.bin")
        strings_path = os.path.join(path, "strings.bin")
    with open(records_path, "rb") as f:
        records_data = f.read()
    with open(strings_path, "rb") as f:
        strings_data = f.read()

    count = len(records_data) // KANJI_RECORD.size
    for i in range(count):
        key, key_len, offset, byte_len, _char_len = KANJI_RECORD.unpack_from(
            records_data, i * KANJI_RECORD.size
        )
        key_text = key[:key_len].decode("utf-8", errors="replace")
        if consolidated and ":" in key_text:
            key_text = key_text.split(":", 1)[1]
        yield "kanji.reading", key_text
        yield "kanji.candidates", read_string(strings_data, offset, byte_len)


def char_label(ch):
    cp = ord(ch)
    name = unicodedata.name(ch, "<no Unicode name>")
    return f"U+{cp:04X} {ch} {name}"


def main():
    parser = argparse.ArgumentParser(
        description="Check generated SD text against M5GFX efontJA_16 coverage."
    )
    parser.add_argument("--jpdict", default="build/sd/jpdict")
    parser.add_argument("--kanji", default="build/sd/kanji")
    parser.add_argument(
        "--font-source",
        default=".pio/libdeps/m5stack-cardputer-adv/M5GFX/src/lgfx/Fonts/efont/lgfx_efont_ja.c",
    )
    parser.add_argument("--font-symbol", default="lgfx_efont_ja_16")
    parser.add_argument("--top", type=int, default=40)
    args = parser.parse_args()

    font = U8g2Font(extract_font_array(args.font_source, args.font_symbol))

    counters = collections.defaultdict(collections.Counter)
    totals = collections.Counter()
    present = collections.Counter()
    missing = collections.Counter()

    scanners = []
    if os.path.isdir(args.jpdict):
        scanners.append(scan_jpdict(args.jpdict))
    if os.path.isdir(args.kanji):
        scanners.append(scan_kanji(args.kanji))

    for scanner in scanners:
        for source, text in scanner:
            for ch in text:
                cp = ord(ch)
                totals[source] += 1
                if font.has_glyph(cp):
                    present[source] += 1
                else:
                    missing[source] += 1
                    counters[source][ch] += 1

    print(f"font={args.font_symbol}")
    print("source,total_chars,present_chars,missing_chars,missing_pct,unique_missing")
    for source in sorted(totals):
        total = totals[source]
        miss = missing[source]
        pct = (miss / total * 100.0) if total else 0.0
        print(
            f"{source},{total},{present[source]},{miss},{pct:.3f},"
            f"{len(counters[source])}"
        )

    combined = collections.Counter()
    for counter in counters.values():
        combined.update(counter)

    print()
    print(f"top_missing_overall,count")
    for ch, count in combined.most_common(args.top):
        print(f"{char_label(ch)},{count}")

    for source in sorted(counters):
        print()
        print(f"top_missing_{source},count")
        for ch, count in counters[source].most_common(args.top):
            print(f"{char_label(ch)},{count}")


if __name__ == "__main__":
    main()
