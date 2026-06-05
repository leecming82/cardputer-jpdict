#!/usr/bin/env python3
import argparse
import gzip
import json
import os
import struct
import xml.etree.ElementTree as ET
from collections import defaultdict

UNICODE_BUCKETS = 0x110000
KEY_BYTES = 64
BUCKET_STRUCT = struct.Struct("<II")
RECORD_STRUCT = struct.Struct("<64sHIIH")


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


def convert(args):
    os.makedirs(args.out_dir, exist_ok=True)
    readings = parse_kanjidic2(args.src)

    entries = []
    for reading, ranked in readings.items():
        candidates = [
            kanji
            for kanji, _rank in sorted(ranked.items(), key=lambda item: item[1])
        ][: args.max_candidates_per_reading]
        if candidates:
            entries.append((reading, "".join(candidates)))
    entries.sort(key=lambda item: item[0])

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

    with open(os.path.join(args.out_dir, "records.bin"), "wb") as records, open(
        os.path.join(args.out_dir, "strings.bin"), "wb"
    ) as strings:
        for reading, candidates in entries:
            key = safe_prefix_bytes(reading, KEY_BYTES)
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

    manifest = {
        "format": "cardputer-kanji-index",
        "version": 1,
        "source": os.path.basename(args.src),
        "reading_count": len(entries),
        "max_candidates_per_reading": args.max_candidates_per_reading,
        "files": ["manifest.json", "buckets.bin", "records.bin", "strings.bin"],
    }
    with open(os.path.join(args.out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(f"wrote {len(entries)} readings to {args.out_dir}")


def lookup(args):
    records_path = os.path.join(args.dir, "records.bin")
    strings_path = os.path.join(args.dir, "strings.bin")
    key = args.reading.encode("utf-8")
    with open(records_path, "rb") as records, open(strings_path, "rb") as strings:
        while True:
            data = records.read(RECORD_STRUCT.size)
            if not data:
                break
            raw_key, key_len, offset, data_len, count = RECORD_STRUCT.unpack(data)
            if raw_key[:key_len] == key:
                strings.seek(offset)
                print(strings.read(data_len).decode("utf-8"))
                print(f"{count} candidates")
                return
    print("no match")


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("convert")
    p.add_argument("src")
    p.add_argument("out_dir")
    p.add_argument("--max-candidates-per-reading", type=int, default=96)
    p.set_defaults(func=convert)

    p = sub.add_parser("lookup")
    p.add_argument("dir")
    p.add_argument("reading")
    p.set_defaults(func=lookup)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
