#!/usr/bin/env python3
import argparse
import gzip
import json
import os
import struct
import xml.etree.ElementTree as ET
from collections import defaultdict
from check_efont_coverage import U8g2Font, extract_font_array
from text_normalization import dedupe_chars, normalize_display_text

UNICODE_BUCKETS = 0x110000
KEY_BYTES = 64
BUCKET_STRUCT = struct.Struct("<II")
RECORD_STRUCT = struct.Struct("<64sHIIH")
DEFAULT_FONT_SOURCE = ".pio/libdeps/m5stack-cardputer-adv/M5GFX/src/lgfx/Fonts/efont/lgfx_efont_ja.c"
DEFAULT_FONT_SYMBOL = "lgfx_efont_ja_16"
LOOKUP_TYPES = {
    "reading": "r",
    "radical_alias": "a",
    "component_kanji": "c",
    "radical_strokes": "s",
    "kanji_strokes": "k",
}
LEGACY_LOOKUP_FILES = [
    "buckets.bin",
    "records.bin",
    "strings.bin",
    "radicals.manifest.json",
    "radical_aliases.buckets.bin",
    "radical_aliases.records.bin",
    "radical_aliases.strings.bin",
    "component_kanji.buckets.bin",
    "component_kanji.records.bin",
    "component_kanji.strings.bin",
    "radical_strokes.buckets.bin",
    "radical_strokes.records.bin",
    "radical_strokes.strings.bin",
    "kanji_strokes.buckets.bin",
    "kanji_strokes.records.bin",
    "kanji_strokes.strings.bin",
]

RADICAL_ALIASES = {
    "一": ["いち", "ひとつ"],
    "丨": ["ぼう", "たてぼう"],
    "丶": ["てん"],
    "丿": ["の"],
    "乙": ["おつ"],
    "亅": ["はねぼう"],
    "二": ["に", "ふた"],
    "亠": ["なべぶた"],
    "人": ["ひと", "にん", "じん"],
    "亻": ["にんべん", "ひと"],
    "儿": ["にんにょう"],
    "入": ["いる", "にゅう"],
    "八": ["はち"],
    "冂": ["けいがまえ"],
    "冖": ["わかんむり"],
    "冫": ["にすい"],
    "几": ["つくえ"],
    "凵": ["うけばこ"],
    "刀": ["かたな", "とう"],
    "刂": ["りっとう", "かたな"],
    "力": ["ちから", "りょく"],
    "勹": ["つつみがまえ"],
    "匕": ["さじ"],
    "匚": ["はこがまえ"],
    "十": ["じゅう", "とお"],
    "卜": ["ぼく", "うらない"],
    "卩": ["ふしづくり"],
    "厂": ["がんだれ"],
    "厶": ["む"],
    "又": ["また"],
    "口": ["くち", "こう"],
    "囗": ["くにがまえ"],
    "土": ["つち"],
    "士": ["さむらい"],
    "夂": ["ふゆがしら"],
    "夕": ["ゆう", "せき"],
    "大": ["だい", "おお"],
    "女": ["おんな"],
    "子": ["こ"],
    "宀": ["うかんむり"],
    "寸": ["すん"],
    "小": ["しょう", "ちいさい"],
    "山": ["やま"],
    "川": ["かわ"],
    "工": ["こう", "たくみ"],
    "己": ["おのれ"],
    "巾": ["はば"],
    "干": ["かん", "ほす"],
    "广": ["まだれ"],
    "廴": ["えんにょう"],
    "廾": ["こまぬき"],
    "弋": ["しきがまえ"],
    "弓": ["ゆみ"],
    "彡": ["さんづくり"],
    "彳": ["ぎょうにんべん"],
    "心": ["こころ", "しん", "りっしんべん"],
    "忄": ["りっしんべん", "こころ"],
    "戈": ["ほこ"],
    "戸": ["と"],
    "手": ["て", "しゅ", "てへん"],
    "扌": ["てへん", "て"],
    "攵": ["のぶん"],
    "文": ["ぶん", "ふみ"],
    "斗": ["ます"],
    "斤": ["おの"],
    "方": ["ほう", "かた"],
    "日": ["ひ", "にち"],
    "曰": ["ひらび"],
    "月": ["つき", "げつ"],
    "木": ["き", "もく"],
    "欠": ["あくび"],
    "止": ["とめる"],
    "歹": ["がつへん"],
    "殳": ["ほこづくり"],
    "毋": ["なかれ"],
    "比": ["くらべる"],
    "毛": ["け"],
    "氏": ["うじ"],
    "气": ["きがまえ"],
    "水": ["みず", "すい", "さんずい"],
    "氵": ["さんずい", "みず"],
    "火": ["ひ", "か"],
    "灬": ["れっか", "れんが", "ひ"],
    "爪": ["つめ"],
    "爫": ["つめかんむり"],
    "父": ["ちち"],
    "爻": ["こう"],
    "爿": ["しょうへん"],
    "片": ["かた"],
    "牛": ["うし"],
    "牜": ["うしへん"],
    "犬": ["いぬ"],
    "犭": ["けものへん", "いぬ"],
    "王": ["おう", "たま"],
    "玉": ["たま"],
    "田": ["た", "でん"],
    "疒": ["やまいだれ"],
    "癶": ["はつがしら"],
    "白": ["しろ"],
    "皮": ["けがわ"],
    "皿": ["さら"],
    "目": ["め", "もく"],
    "矛": ["ほこ"],
    "矢": ["や"],
    "石": ["いし", "せき"],
    "示": ["しめす", "しめすへん"],
    "礻": ["しめすへん"],
    "禸": ["ぐうのあし"],
    "禾": ["のぎ"],
    "穴": ["あな"],
    "立": ["たつ"],
    "竹": ["たけ"],
    "糸": ["いと", "いとへん"],
    "糹": ["いとへん", "いと"],
    "缶": ["ほとぎ"],
    "网": ["あみ"],
    "罒": ["あみがしら"],
    "羊": ["ひつじ"],
    "羽": ["はね"],
    "老": ["おい"],
    "而": ["しかして"],
    "耒": ["らいすき"],
    "耳": ["みみ"],
    "聿": ["ふでづくり"],
    "肉": ["にく"],
    "月": ["つき", "にくづき"],
    "自": ["みずから"],
    "至": ["いたる"],
    "臼": ["うす"],
    "舌": ["した"],
    "舟": ["ふね"],
    "艮": ["こん"],
    "色": ["いろ"],
    "艸": ["くさ", "くさかんむり"],
    "艹": ["くさかんむり", "くさ"],
    "虍": ["とらがしら"],
    "虫": ["むし"],
    "血": ["ち"],
    "行": ["ぎょう"],
    "衣": ["ころも", "ころもへん"],
    "衤": ["ころもへん"],
    "見": ["みる"],
    "角": ["つの"],
    "言": ["いう", "ことば", "げん", "ごん", "ごんべん"],
    "訁": ["ごんべん", "ことば"],
    "谷": ["たに"],
    "豆": ["まめ"],
    "豕": ["いのこ"],
    "豸": ["むじな"],
    "貝": ["かい"],
    "赤": ["あか"],
    "走": ["はしる"],
    "足": ["あし"],
    "辶": ["しんにょう"],
    "邑": ["むら"],
    "阝": ["こざとへん", "おおざと"],
    "金": ["かね", "きん"],
    "釒": ["かねへん"],
    "門": ["もん"],
    "隹": ["ふるとり"],
    "雨": ["あめ"],
    "青": ["あお"],
    "非": ["あらず"],
    "面": ["めん"],
    "革": ["かわへん"],
    "音": ["おと"],
    "頁": ["おおがい"],
    "風": ["かぜ"],
    "食": ["しょく", "たべる"],
    "飠": ["しょくへん"],
    "首": ["くび"],
    "馬": ["うま"],
    "骨": ["ほね"],
    "高": ["たかい"],
    "髟": ["かみがしら"],
    "鬥": ["とうがまえ"],
    "鬯": ["ちょう"],
    "鬲": ["かなえ"],
    "鬼": ["おに"],
    "魚": ["うお", "さかな"],
    "鳥": ["とり"],
    "鹵": ["しお"],
    "鹿": ["しか"],
    "麦": ["むぎ"],
    "麻": ["あさ"],
    "黄": ["き"],
    "黒": ["くろ"],
    "黹": ["ぬいとり"],
    "黽": ["べん"],
    "鼎": ["かなえ"],
    "鼓": ["つづみ"],
    "鼠": ["ねずみ"],
    "鼻": ["はな"],
    "齊": ["せい"],
    "齒": ["は"],
    "龍": ["りゅう"],
    "龜": ["かめ"],
    "龠": ["やく"],
}

RADICAL_STROKES = {
    "一": 1, "丨": 1, "丶": 1, "丿": 1, "乙": 1, "亅": 1,
    "二": 2, "亠": 2, "人": 2, "亻": 2, "儿": 2, "入": 2, "八": 2,
    "冂": 2, "冖": 2, "冫": 2, "几": 2, "凵": 2, "刀": 2, "刂": 2,
    "力": 2, "勹": 2, "匕": 2, "匚": 2, "十": 2, "卜": 2, "卩": 2,
    "厂": 2, "厶": 2, "又": 2,
    "口": 3, "囗": 3, "土": 3, "士": 3, "夂": 3, "夕": 3, "大": 3,
    "女": 3, "子": 3, "宀": 3, "寸": 3, "小": 3, "山": 3, "川": 3,
    "工": 3, "己": 3, "巾": 3, "干": 3, "广": 3, "廴": 3, "廾": 3,
    "弋": 3, "弓": 3, "彡": 3, "彳": 3, "忄": 3, "扌": 3, "氵": 3,
    "犭": 3, "艹": 3, "阝": 3,
    "心": 4, "戈": 4, "戸": 4, "手": 4, "攵": 4, "文": 4, "斗": 4,
    "斤": 4, "方": 4, "日": 4, "曰": 4, "月": 4, "木": 4, "欠": 4,
    "止": 4, "歹": 4, "殳": 4, "毋": 4, "比": 4, "毛": 4, "氏": 4,
    "气": 4, "水": 4, "火": 4, "灬": 4, "爪": 4, "爫": 4, "父": 4,
    "爻": 4, "爿": 4, "片": 4, "牛": 4, "牜": 4, "犬": 4, "王": 4,
    "礻": 4, "罒": 4, "耂": 4, "肀": 4, "辶": 4,
    "玄": 5, "玉": 5, "瓜": 5, "瓦": 5, "甘": 5, "生": 5, "用": 5,
    "田": 5, "疋": 5, "疒": 5, "癶": 5, "白": 5, "皮": 5, "皿": 5,
    "目": 5, "矛": 5, "矢": 5, "石": 5, "示": 5, "禸": 5, "禾": 5,
    "穴": 5, "立": 5, "衤": 5, "钅": 5,
    "竹": 6, "米": 6, "糸": 6, "糹": 6, "缶": 6, "网": 6, "羊": 6,
    "羽": 6, "老": 6, "而": 6, "耒": 6, "耳": 6, "聿": 6, "肉": 6,
    "臣": 6, "自": 6, "至": 6, "臼": 6, "舌": 6, "舛": 6, "舟": 6,
    "艮": 6, "色": 6, "艸": 6, "虍": 6, "虫": 6, "血": 6, "行": 6,
    "衣": 6, "西": 6, "襾": 6,
    "見": 7, "角": 7, "言": 7, "訁": 7, "谷": 7, "豆": 7, "豕": 7,
    "豸": 7, "貝": 7, "赤": 7, "走": 7, "足": 7, "身": 7, "車": 7,
    "辛": 7, "辰": 7, "邑": 7, "酉": 7, "釆": 7, "里": 7,
    "金": 8, "釒": 8, "長": 8, "門": 8, "阜": 8, "隶": 8, "隹": 8,
    "雨": 8, "青": 8, "非": 8,
    "面": 9, "革": 9, "韋": 9, "韭": 9, "音": 9, "頁": 9, "風": 9,
    "飛": 9, "食": 9, "飠": 9, "首": 9, "香": 9,
    "馬": 10, "骨": 10, "高": 10, "髟": 10, "鬥": 10, "鬯": 10,
    "鬲": 10, "鬼": 10, "竜": 10,
    "魚": 11, "鳥": 11, "鹵": 11, "鹿": 11, "麦": 11, "麻": 11,
    "黄": 12, "黍": 12, "黒": 12, "黹": 12,
    "黽": 13, "鼎": 13, "鼓": 13, "鼠": 13,
    "鼻": 14, "齊": 14,
    "齒": 15,
    "龍": 16, "龜": 16,
    "龠": 17,
}

RADICAL_VARIANTS = {
    "亻": "人",
    "刂": "刀",
    "忄": "心",
    "扌": "手",
    "氵": "水",
    "灬": "火",
    "牜": "牛",
    "犭": "犬",
    "礻": "示",
    "糹": "糸",
    "艹": "艸",
    "衤": "衣",
    "訁": "言",
    "釒": "金",
    "飠": "食",
}


def katakana_to_hiragana(text):
    out = []
    for ch in text:
        cp = ord(ch)
        if 0x30A1 <= cp <= 0x30F6:
            out.append(chr(cp - 0x60))
        else:
            out.append(ch)
    return "".join(out)


def reading_variants(text):
    text = katakana_to_hiragana(text.strip())
    if not text or "-" in text:
        return []

    compact = text.replace(".", "")
    values = {compact}
    if "." in text:
        before_dot = text.split(".", 1)[0]
        if before_dot:
            values.add(before_dot)
    return sorted(values)


def safe_prefix_bytes(text, max_bytes):
    data = text.encode("utf-8")
    if len(data) <= max_bytes:
        return data
    cut = data[:max_bytes]
    while cut and (cut[-1] & 0xC0) == 0x80:
        cut = cut[:-1]
    return cut


def open_xml(path):
    if path.endswith(".gz"):
        return gzip.open(path, "rb")
    return open(path, "rb")


def parse_kanjidic2(path):
    readings = defaultdict(dict)
    with open_xml(path) as f:
        for event, elem in ET.iterparse(f, events=("end",)):
            if elem.tag != "character":
                continue

            literal = elem.findtext("literal")
            if not literal:
                elem.clear()
                continue

            grade = elem.findtext("misc/grade")
            freq = elem.findtext("misc/freq")
            stroke = elem.findtext("misc/stroke_count")
            rank = (
                int(grade) if grade and grade.isdigit() else 99,
                int(freq) if freq and freq.isdigit() else 99999,
                int(stroke) if stroke and stroke.isdigit() else 999,
                literal,
            )

            for reading in elem.findall("reading_meaning/rmgroup/reading"):
                if reading.get("r_type") not in {"ja_on", "ja_kun"}:
                    continue
                for value in reading_variants(reading.text or ""):
                    current = readings[value].get(literal)
                    if current is None or rank < current:
                        readings[value][literal] = rank

            elem.clear()
    return readings


def parse_kanjidic2_metadata(path):
    metadata = {}
    radical_kanji = defaultdict(dict)
    with open_xml(path) as f:
        for event, elem in ET.iterparse(f, events=("end",)):
            if elem.tag != "character":
                continue

            literal = elem.findtext("literal")
            if not literal:
                elem.clear()
                continue

            grade = elem.findtext("misc/grade")
            freq = elem.findtext("misc/freq")
            stroke = elem.findtext("misc/stroke_count")
            rank = (
                int(grade) if grade and grade.isdigit() else 99,
                int(freq) if freq and freq.isdigit() else 99999,
                int(stroke) if stroke and stroke.isdigit() else 999,
                literal,
            )
            metadata[literal] = {
                "stroke": int(stroke) if stroke and stroke.isdigit() else 0,
                "rank": rank,
            }

            for radical in elem.findall("radical/rad_value"):
                if radical.get("rad_type") != "classical":
                    continue
                value = radical.text or ""
                if value.isdigit():
                    radical_kanji[f"r:{value}"][literal] = rank

            elem.clear()
    return metadata, radical_kanji


def parse_kradfile(path):
    components = defaultdict(set)
    with open(path, "rb") as f:
        data = f.read()
    try:
        text = data.decode("euc-jp")
    except UnicodeDecodeError:
        text = data.decode("utf-8")
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or " : " not in line:
            continue
        kanji, raw_components = line.split(" : ", 1)
        kanji = kanji.strip()
        if not kanji:
            continue
        for component in raw_components.split():
            if component:
                components[kanji].add(component)
    return components


def build_buckets(entries):
    buckets = [(0, 0)] * UNICODE_BUCKETS
    bucket_start = {}
    bucket_count = {}
    for idx, (key, _candidates) in enumerate(entries):
        cp = ord(key[0])
        bucket_start.setdefault(cp, idx)
        bucket_count[cp] = bucket_count.get(cp, 0) + 1
    for cp, start in bucket_start.items():
        buckets[cp] = (start, bucket_count[cp])
    return buckets


def write_lookup_files(out_dir, prefix, entries):
    buckets_path = os.path.join(out_dir, f"{prefix}.buckets.bin")
    records_path = os.path.join(out_dir, f"{prefix}.records.bin")
    strings_path = os.path.join(out_dir, f"{prefix}.strings.bin")

    with open(buckets_path, "wb") as f:
        for start, count in build_buckets(entries):
            f.write(BUCKET_STRUCT.pack(start, count))

    with open(records_path, "wb") as records, open(strings_path, "wb") as strings:
        for key_text, candidates in entries:
            key = safe_prefix_bytes(key_text, KEY_BYTES)
            data = candidates.encode("utf-8")
            offset = strings.tell()
            strings.write(data)
            records.write(
                RECORD_STRUCT.pack(
                    key.ljust(KEY_BYTES, b"\0"),
                    len(key),
                    offset,
                    len(data),
                    len(candidates),
                )
            )

    return [
        f"{prefix}.buckets.bin",
        f"{prefix}.records.bin",
        f"{prefix}.strings.bin",
    ]


def write_records_strings(records_path, strings_path, entries):
    with open(records_path, "wb") as records, open(strings_path, "wb") as strings:
        for key_text, candidates in entries:
            key = safe_prefix_bytes(key_text, KEY_BYTES)
            data = candidates.encode("utf-8")
            offset = strings.tell()
            strings.write(data)
            records.write(
                RECORD_STRUCT.pack(
                    key.ljust(KEY_BYTES, b"\0"),
                    len(key),
                    offset,
                    len(data),
                    len(candidates),
                )
            )


def typed_key(lookup_type, key):
    return f"{LOOKUP_TYPES[lookup_type]}:{key}"


def write_consolidated_lookup(out_dir, typed_entries):
    records_path = os.path.join(out_dir, "lookup.records.bin")
    strings_path = os.path.join(out_dir, "lookup.strings.bin")
    typed_entries.sort(key=lambda item: item[0])
    write_records_strings(records_path, strings_path, typed_entries)
    return ["lookup.records.bin", "lookup.strings.bin"]


def remove_legacy_lookup_files(out_dir):
    for name in LEGACY_LOOKUP_FILES:
        path = os.path.join(out_dir, name)
        if os.path.exists(path):
            os.remove(path)


def build_reading_entries(src, max_candidates, font_source, font_symbol,
                          filter_unsupported_candidates):
    readings = parse_kanjidic2(src)
    font = None
    if filter_unsupported_candidates:
        font = U8g2Font(extract_font_array(font_source, font_symbol))

    entries = []
    normalized_candidates = 0
    deduplicated_candidates = 0
    dropped_unsupported_candidates = 0
    for reading, ranked in readings.items():
        raw_candidates = [
            kanji
            for kanji, _rank in sorted(ranked.items(), key=lambda item: item[1])
        ]
        seen = set()
        candidates = []
        for raw in raw_candidates:
            normalized = normalize_display_text(raw)
            if normalized != raw:
                normalized_candidates += 1
            for ch in normalized:
                if ch in seen:
                    deduplicated_candidates += 1
                    continue
                seen.add(ch)
                if font is not None and not font.has_glyph(ord(ch)):
                    dropped_unsupported_candidates += 1
                    continue
                candidates.append(ch)
                if len(candidates) >= max_candidates:
                    break
            if len(candidates) >= max_candidates:
                break
        candidates = dedupe_chars("".join(candidates))
        if candidates:
            entries.append((reading, candidates))
    entries.sort(key=lambda item: item[0])
    stats = {
        "normalized_candidates": normalized_candidates,
        "deduplicated_candidates": deduplicated_candidates,
        "dropped_unsupported_candidates": dropped_unsupported_candidates,
    }
    return entries, stats


def convert(args):
    os.makedirs(args.out_dir, exist_ok=True)
    entries, stats = build_reading_entries(
        args.src,
        args.max_candidates_per_reading,
        args.font_source,
        args.font_symbol,
        args.filter_unsupported_candidates,
    )
    buckets = [(0, 0)] * UNICODE_BUCKETS
    bucket_start = {}
    bucket_count = {}
    for idx, (reading, _candidates) in enumerate(entries):
        cp = ord(reading[0])
        bucket_start.setdefault(cp, idx)
        bucket_count[cp] = bucket_count.get(cp, 0) + 1
    for cp, start in bucket_start.items():
        buckets[cp] = (start, bucket_count[cp])

    with open(os.path.join(args.out_dir, "buckets.bin"), "wb") as f:
        for start, count in buckets:
            f.write(BUCKET_STRUCT.pack(start, count))

    write_records_strings(
        os.path.join(args.out_dir, "records.bin"),
        os.path.join(args.out_dir, "strings.bin"),
        entries,
    )

    manifest = {
        "format": "cardputer-kanji-index",
        "version": 1,
        "source": os.path.basename(args.src),
        "reading_count": len(entries),
        "max_candidates_per_reading": args.max_candidates_per_reading,
        "normalization": {
            "normalized_candidates": stats["normalized_candidates"],
            "deduplicated_candidates": stats["deduplicated_candidates"],
            "filter_unsupported_candidates": args.filter_unsupported_candidates,
            "font_symbol": args.font_symbol if args.filter_unsupported_candidates else "",
            "dropped_unsupported_candidates": stats["dropped_unsupported_candidates"],
        },
        "files": ["manifest.json", "buckets.bin", "records.bin", "strings.bin"],
    }
    with open(os.path.join(args.out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(f"wrote {len(entries)} readings to {args.out_dir}")


def lookup(args):
    if os.path.exists(os.path.join(args.dir, "lookup.records.bin")):
        lookup_consolidated(args.dir, "reading", args.reading)
    else:
        lookup_record_files(args.dir, "records.bin", "strings.bin", args.reading)


def sorted_chars(ranked, metadata=None, max_count=None):
    def rank_for(ch):
        if ch in ranked and not isinstance(ranked[ch], set):
            return ranked[ch]
        if metadata and ch in metadata:
            return metadata[ch]["rank"]
        return (99, 99999, 999, ch)

    chars = [ch for ch in ranked.keys()]
    chars.sort(key=rank_for)
    if max_count is not None:
        chars = chars[:max_count]
    return dedupe_chars("".join(chars))


def build_radical_entries(kanjidic2, kradfile, max_components_per_alias,
                          max_components_per_stroke,
                          max_candidates_per_component,
                          max_kanji_per_stroke, font_source, font_symbol,
                          filter_unsupported_candidates):
    metadata, _radical_kanji = parse_kanjidic2_metadata(kanjidic2)
    krad_components = parse_kradfile(kradfile)

    font = None
    if filter_unsupported_candidates:
        font = U8g2Font(extract_font_array(font_source, font_symbol))

    component_kanji = defaultdict(dict)
    component_strokes = {}
    for kanji, components in krad_components.items():
        rank = metadata.get(kanji, {}).get("rank", (99, 99999, 999, kanji))
        for component in components:
            normalized = normalize_display_text(component)
            for ch in normalized:
                if font is not None and not font.has_glyph(ord(ch)):
                    continue
                component_kanji[ch][kanji] = rank
                stroke = RADICAL_STROKES.get(ch, metadata.get(ch, {}).get("stroke", 0))
                if stroke:
                    component_strokes[ch] = stroke

    for variant, canonical in RADICAL_VARIANTS.items():
        if variant in component_kanji or canonical not in component_kanji:
            continue
        if font is not None and not font.has_glyph(ord(variant)):
            continue
        component_kanji[variant] = dict(component_kanji[canonical])
        stroke = RADICAL_STROKES.get(variant, 0)
        if stroke:
            component_strokes[variant] = stroke

    component_entries = []
    for component, ranked in component_kanji.items():
        kanji = sorted_chars(ranked, metadata, max_candidates_per_component)
        if kanji:
            component_entries.append((component, kanji))
    component_entries.sort(key=lambda item: item[0])

    alias_components = defaultdict(dict)
    for component, aliases in RADICAL_ALIASES.items():
        normalized = normalize_display_text(component)
        for ch in normalized:
            if ch not in component_kanji:
                continue
            rank = metadata.get(ch, {}).get("rank", (99, 99999, 999, ch))
            alias_components[ch][ch] = rank
            for alias in aliases:
                for value in reading_variants(alias):
                    alias_components[value][ch] = rank

    alias_entries = []
    for alias, ranked in alias_components.items():
        candidates = sorted_chars(ranked, metadata, max_components_per_alias)
        if candidates:
            alias_entries.append((alias, candidates))
    alias_entries.sort(key=lambda item: item[0])

    stroke_components = defaultdict(dict)
    for component, stroke in component_strokes.items():
        rank = metadata.get(component, {}).get("rank", (99, 99999, stroke, component))
        stroke_components[str(stroke)][component] = rank

    stroke_entries = []
    for stroke, ranked in stroke_components.items():
        candidates = sorted_chars(ranked, metadata, max_components_per_stroke)
        if candidates:
            stroke_entries.append((stroke, candidates))
    stroke_entries.sort(key=lambda item: int(item[0]))

    kanji_strokes = defaultdict(dict)
    for kanji, data in metadata.items():
        stroke = data.get("stroke", 0)
        if not stroke:
            continue
        if font is not None and not font.has_glyph(ord(kanji)):
            continue
        kanji_strokes[str(stroke)][kanji] = data["rank"]

    kanji_stroke_entries = []
    for stroke, ranked in kanji_strokes.items():
        candidates = sorted_chars(ranked, metadata, max_kanji_per_stroke)
        if candidates:
            kanji_stroke_entries.append((stroke, candidates))
    kanji_stroke_entries.sort(key=lambda item: int(item[0]))

    return {
        "radical_alias": alias_entries,
        "component_kanji": component_entries,
        "radical_strokes": stroke_entries,
        "kanji_strokes": kanji_stroke_entries,
    }


def convert_radicals(args):
    os.makedirs(args.out_dir, exist_ok=True)
    entry_groups = build_radical_entries(
        args.kanjidic2,
        args.kradfile,
        args.max_components_per_alias,
        args.max_components_per_stroke,
        args.max_candidates_per_component,
        args.max_kanji_per_stroke,
        args.font_source,
        args.font_symbol,
        args.filter_unsupported_candidates,
    )
    alias_entries = entry_groups["radical_alias"]
    component_entries = entry_groups["component_kanji"]
    stroke_entries = entry_groups["radical_strokes"]
    kanji_stroke_entries = entry_groups["kanji_strokes"]

    written_files = []
    written_files.extend(write_lookup_files(args.out_dir, "radical_aliases", alias_entries))
    written_files.extend(write_lookup_files(args.out_dir, "component_kanji", component_entries))
    written_files.extend(write_lookup_files(args.out_dir, "radical_strokes", stroke_entries))
    written_files.extend(write_lookup_files(args.out_dir, "kanji_strokes", kanji_stroke_entries))

    manifest_path = os.path.join(args.out_dir, "radicals.manifest.json")
    manifest = {
        "format": "cardputer-kanji-radical-index",
        "version": 1,
        "kanjidic2_source": os.path.basename(args.kanjidic2),
        "kradfile_source": os.path.basename(args.kradfile),
        "alias_count": len(alias_entries),
        "component_count": len(component_entries),
        "stroke_count": len(stroke_entries),
        "kanji_stroke_count": len(kanji_stroke_entries),
        "max_components_per_alias": args.max_components_per_alias,
        "max_components_per_stroke": args.max_components_per_stroke,
        "max_candidates_per_component": args.max_candidates_per_component,
        "max_kanji_per_stroke": args.max_kanji_per_stroke,
        "normalization": {
            "filter_unsupported_candidates": args.filter_unsupported_candidates,
            "font_symbol": args.font_symbol if args.filter_unsupported_candidates else "",
        },
        "files": ["radicals.manifest.json"] + written_files,
    }
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(
        f"wrote {len(alias_entries)} radical aliases and "
        f"{len(component_entries)} components and "
        f"{len(stroke_entries)} radical stroke buckets and "
        f"{len(kanji_stroke_entries)} kanji stroke buckets to {args.out_dir}"
    )


def convert_all(args):
    os.makedirs(args.out_dir, exist_ok=True)
    remove_legacy_lookup_files(args.out_dir)
    reading_entries, reading_stats = build_reading_entries(
        args.kanjidic2,
        args.max_candidates_per_reading,
        args.font_source,
        args.font_symbol,
        args.filter_unsupported_candidates,
    )
    radical_groups = build_radical_entries(
        args.kanjidic2,
        args.kradfile,
        args.max_components_per_alias,
        args.max_components_per_stroke,
        args.max_candidates_per_component,
        args.max_kanji_per_stroke,
        args.font_source,
        args.font_symbol,
        args.filter_unsupported_candidates,
    )

    typed_entries = []
    typed_entries.extend((typed_key("reading", key), candidates)
                         for key, candidates in reading_entries)
    for lookup_type, entries in radical_groups.items():
        typed_entries.extend((typed_key(lookup_type, key), candidates)
                             for key, candidates in entries)
    written_files = write_consolidated_lookup(args.out_dir, typed_entries)

    manifest = {
        "format": "cardputer-kanji-lookup",
        "version": 2,
        "kanjidic2_source": os.path.basename(args.kanjidic2),
        "kradfile_source": os.path.basename(args.kradfile),
        "record_count": len(typed_entries),
        "reading_count": len(reading_entries),
        "alias_count": len(radical_groups["radical_alias"]),
        "component_count": len(radical_groups["component_kanji"]),
        "radical_stroke_count": len(radical_groups["radical_strokes"]),
        "kanji_stroke_count": len(radical_groups["kanji_strokes"]),
        "max_candidates_per_reading": args.max_candidates_per_reading,
        "max_components_per_alias": args.max_components_per_alias,
        "max_components_per_stroke": args.max_components_per_stroke,
        "max_candidates_per_component": args.max_candidates_per_component,
        "max_kanji_per_stroke": args.max_kanji_per_stroke,
        "lookup_types": LOOKUP_TYPES,
        "normalization": {
            "normalized_candidates": reading_stats["normalized_candidates"],
            "deduplicated_candidates": reading_stats["deduplicated_candidates"],
            "filter_unsupported_candidates": args.filter_unsupported_candidates,
            "font_symbol": args.font_symbol if args.filter_unsupported_candidates else "",
            "dropped_unsupported_candidates": reading_stats["dropped_unsupported_candidates"],
        },
        "files": ["manifest.json"] + written_files,
    }
    with open(os.path.join(args.out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(
        f"wrote {len(typed_entries)} consolidated kanji lookup records "
        f"to {args.out_dir}"
    )


def lookup_record_files(dir_path, records_name, strings_name, key):
    records_path = os.path.join(dir_path, records_name)
    strings_path = os.path.join(dir_path, strings_name)
    encoded_key = key.encode("utf-8")
    with open(records_path, "rb") as records, open(strings_path, "rb") as strings:
        while True:
            data = records.read(RECORD_STRUCT.size)
            if not data:
                break
            raw_key, key_len, offset, data_len, count = RECORD_STRUCT.unpack(data)
            if raw_key[:key_len] == encoded_key:
                strings.seek(offset)
                text = strings.read(data_len).decode("utf-8")
                print(text)
                print(f"{count} candidates")
                return
    print("no match")


def lookup_consolidated(dir_path, lookup_type, key):
    lookup_record_files(
        dir_path,
        "lookup.records.bin",
        "lookup.strings.bin",
        typed_key(lookup_type, key),
    )


def lookup_prefixed(dir_path, prefix, key):
    if os.path.exists(os.path.join(dir_path, "lookup.records.bin")):
        lookup_type = {
            "radical_aliases": "radical_alias",
            "component_kanji": "component_kanji",
            "radical_strokes": "radical_strokes",
            "kanji_strokes": "kanji_strokes",
        }[prefix]
        lookup_consolidated(dir_path, lookup_type, key)
        return
    lookup_record_files(dir_path, f"{prefix}.records.bin",
                        f"{prefix}.strings.bin", key)


def lookup_radical_alias(args):
    lookup_prefixed(args.dir, "radical_aliases", args.alias)


def lookup_component(args):
    lookup_prefixed(args.dir, "component_kanji", args.component)


def lookup_radical_strokes(args):
    lookup_prefixed(args.dir, "radical_strokes", args.strokes)


def lookup_kanji_strokes(args):
    lookup_prefixed(args.dir, "kanji_strokes", args.strokes)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("convert")
    p.add_argument("src")
    p.add_argument("out_dir")
    p.add_argument("--max-candidates-per-reading", type=int, default=96)
    p.add_argument("--font-source", default=DEFAULT_FONT_SOURCE)
    p.add_argument("--font-symbol", default=DEFAULT_FONT_SYMBOL)
    p.add_argument(
        "--no-filter-unsupported-candidates",
        dest="filter_unsupported_candidates",
        action="store_false",
    )
    p.set_defaults(filter_unsupported_candidates=True)
    p.set_defaults(func=convert)

    p = sub.add_parser("lookup")
    p.add_argument("dir")
    p.add_argument("reading")
    p.set_defaults(func=lookup)

    p = sub.add_parser("convert-radicals")
    p.add_argument("kanjidic2")
    p.add_argument("kradfile")
    p.add_argument("out_dir")
    p.add_argument("--max-components-per-alias", type=int, default=24)
    p.add_argument("--max-components-per-stroke", type=int, default=48)
    p.add_argument("--max-candidates-per-component", type=int, default=160)
    p.add_argument("--max-kanji-per-stroke", type=int, default=160)
    p.add_argument("--font-source", default=DEFAULT_FONT_SOURCE)
    p.add_argument("--font-symbol", default=DEFAULT_FONT_SYMBOL)
    p.add_argument(
        "--no-filter-unsupported-candidates",
        dest="filter_unsupported_candidates",
        action="store_false",
    )
    p.set_defaults(filter_unsupported_candidates=True)
    p.set_defaults(func=convert_radicals)

    p = sub.add_parser("convert-all")
    p.add_argument("kanjidic2")
    p.add_argument("kradfile")
    p.add_argument("out_dir")
    p.add_argument("--max-candidates-per-reading", type=int, default=96)
    p.add_argument("--max-components-per-alias", type=int, default=24)
    p.add_argument("--max-components-per-stroke", type=int, default=48)
    p.add_argument("--max-candidates-per-component", type=int, default=160)
    p.add_argument("--max-kanji-per-stroke", type=int, default=160)
    p.add_argument("--font-source", default=DEFAULT_FONT_SOURCE)
    p.add_argument("--font-symbol", default=DEFAULT_FONT_SYMBOL)
    p.add_argument(
        "--no-filter-unsupported-candidates",
        dest="filter_unsupported_candidates",
        action="store_false",
    )
    p.set_defaults(filter_unsupported_candidates=True)
    p.set_defaults(func=convert_all)

    p = sub.add_parser("lookup-radical")
    p.add_argument("dir")
    p.add_argument("alias")
    p.set_defaults(func=lookup_radical_alias)

    p = sub.add_parser("lookup-component")
    p.add_argument("dir")
    p.add_argument("component")
    p.set_defaults(func=lookup_component)

    p = sub.add_parser("lookup-strokes")
    p.add_argument("dir")
    p.add_argument("strokes")
    p.set_defaults(func=lookup_radical_strokes)

    p = sub.add_parser("lookup-kanji-strokes")
    p.add_argument("dir")
    p.add_argument("strokes")
    p.set_defaults(func=lookup_kanji_strokes)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
