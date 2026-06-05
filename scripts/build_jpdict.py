#!/usr/bin/env python3
import argparse
import io
import json
import os
import random
import struct
import time
import zipfile

UNICODE_BUCKETS = 0x110000
KEY_BYTES = 96
BUCKET_STRUCT = struct.Struct("<II")
RECORD_STRUCT = struct.Struct("<96sHBBIHIHIIii")
BLOOM_FILTER_BYTES = 128 * 1024
BLOOM_HASHES = 2

TIER_COMMON = 0
TIER_MODERN = 1
TIER_RARE = 2
TIER_ARCHAIC = 3

SKIP_CONTENT_KINDS = {
    "attribution",
    "forms",
    "xref",
    "xref-content",
    "xref-glossary",
    "antonym",
    "antonym-content",
    "antonym-glossary",
    "graphic",
    "graphic-attribution",
    "reference-label",
    "registered-trademark",
}

EXTRA_CONTENT_LABELS = {
    "info-gloss": "Explanation",
    "lang-source": "Language of Origin",
    "sense-note": "Note",
}


def first_char_scope(text):
    cp = ord(text[0])
    if (
        0x3400 <= cp <= 0x4DBF
        or 0x4E00 <= cp <= 0x9FFF
        or 0xF900 <= cp <= 0xFAFF
        or 0x20000 <= cp <= 0x2A6DF
        or 0x2A700 <= cp <= 0x2B73F
        or 0x2B740 <= cp <= 0x2B81F
        or 0x2B820 <= cp <= 0x2CEAF
        or 0x2CEB0 <= cp <= 0x2EBEF
        or 0x2F800 <= cp <= 0x2FA1F
    ):
        return "kanji"
    if 0x3040 <= cp <= 0x309F:
        return "hiragana"
    if 0x30A0 <= cp <= 0x30FF or 0xFF65 <= cp <= 0xFF9F:
        return "katakana"
    return "other"


def include_first_char(text, scope):
    if not text:
        return False
    if scope == "all":
        return True
    char_scope = first_char_scope(text)
    if scope == "kanji":
        return char_scope == "kanji"
    if scope == "japanese":
        return char_scope in ("kanji", "hiragana", "katakana")
    raise ValueError(f"unknown first character scope: {scope}")


def safe_prefix_bytes(text, limit):
    data = text.encode("utf-8")
    if len(data) <= limit:
        return data
    cut = limit
    while cut > 0 and (data[cut] & 0xC0) == 0x80:
        cut -= 1
    return data[:cut]


def fnv1a32(data, seed=0x811C9DC5):
    value = seed
    for byte in data:
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def bloom_positions(data, bit_count):
    h1 = fnv1a32(data)
    h2 = fnv1a32(data, 0xCBF29CE4) | 1
    for i in range(BLOOM_HASHES):
        yield (h1 + i * h2) % bit_count


def write_key_filter(entries, path):
    bit_count = BLOOM_FILTER_BYTES * 8
    bits = bytearray(BLOOM_FILTER_BYTES)
    unique_keys = set()
    for entry in entries:
        key = safe_prefix_bytes(entry["key"], KEY_BYTES)
        if not key:
            continue
        unique_keys.add(key)
        for pos in bloom_positions(key, bit_count):
            bits[pos >> 3] |= 1 << (pos & 7)
    with open(path, "wb") as f:
        f.write(bits)
    return len(unique_keys)


def text_content_from_content_kind(node, wanted_content_kind, out):
    if node is None:
        return
    if isinstance(node, list):
        for item in node:
            text_content_from_content_kind(item, wanted_content_kind, out)
        return
    if not isinstance(node, dict):
        return

    data = node.get("data")
    content_kind = data.get("content") if isinstance(data, dict) else None
    if content_kind == wanted_content_kind:
        text_content_without_examples(node.get("content"), out)
        return
    text_content_from_content_kind(node.get("content"), wanted_content_kind, out)


def text_content_without_examples(node, out):
    if node is None:
        return
    if isinstance(node, str):
        if node.strip():
            out.append(node.strip())
        return
    if isinstance(node, list):
        for item in node:
            text_content_without_examples(item, out)
        return
    if not isinstance(node, dict):
        return

    data = node.get("data")
    if isinstance(data, dict) and data.get("class") == "tag":
        return
    content_kind = data.get("content") if isinstance(data, dict) else None
    if content_kind in EXTRA_CONTENT_LABELS:
        content = []
        text_content_from_content_kind(node.get("content"), content_kind + "-content", content)
        if content:
            out.append(EXTRA_CONTENT_LABELS[content_kind] + ": " + " ".join(content))
        return
    if content_kind in ("example-sentence", "example-sentence-a", "example-sentence-b", "example-keyword"):
        return
    if content_kind in SKIP_CONTENT_KINDS:
        return
    text_content_without_examples(node.get("content"), out)


def tag_codes(node, out):
    if isinstance(node, dict):
        data = node.get("data")
        if isinstance(data, dict) and data.get("class") == "tag":
            code = data.get("code")
            if code:
                out.add(code)
        for value in node.values():
            tag_codes(value, out)
    elif isinstance(node, list):
        for value in node:
            tag_codes(value, out)


def tag_label(code, content):
    if content == "forms":
        return ""
    if code == "vs" or content == "suru":
        return "suru verb"
    if code == "vt":
        return "transitive"
    if code == "vi":
        return "intransitive"
    if code == "uk":
        return "usually kana"
    return content.strip() if isinstance(content, str) else ""


def tag_labels(node, out):
    if isinstance(node, dict):
        data = node.get("data")
        if isinstance(data, dict) and data.get("class") == "tag":
            label = tag_label(data.get("code"), node.get("content"))
            if label and label not in out:
                out.append(label)
            return
        for value in node.values():
            tag_labels(value, out)
    elif isinstance(node, list):
        for value in node:
            tag_labels(value, out)


def flatten_glossary(glossary):
    tags = []
    tag_labels(glossary, tags)
    pieces = []
    for item in glossary:
        if isinstance(item, dict):
            text_content_without_examples(item.get("content"), pieces)
        else:
            text_content_without_examples(item, pieces)

    compact = []
    for piece in tags + pieces:
        if piece and (not compact or compact[-1] != piece):
            compact.append(piece)
    return "; ".join(compact)


def redirect_target(glossary):
    for item in glossary or []:
        if not isinstance(item, list) or not item:
            continue
        if not isinstance(item[0], str) or len(item) < 2:
            continue
        notes = item[1]
        if isinstance(notes, list) and any(isinstance(note, str) and note.startswith("redirected from ") for note in notes):
            return item[0]
    return ""


def classify_tier(score, sequence, tags):
    if sequence < 0 or tags.intersection({"arch", "obs"}):
        return TIER_ARCHAIC
    if tags.intersection({"rare", "dated"}):
        return TIER_RARE
    if score >= 0:
        return TIER_COMMON
    return TIER_MODERN


class YomitanSource:
    def __init__(self, path):
        self.path = path
        self.zip_file = zipfile.ZipFile(path) if zipfile.is_zipfile(path) else None

    def close(self):
        if self.zip_file:
            self.zip_file.close()

    def term_bank_paths(self):
        if self.zip_file:
            names = [name for name in self.zip_file.namelist() if os.path.basename(name).startswith("term_bank_")]
        else:
            names = [
                os.path.join(self.path, name)
                for name in os.listdir(self.path)
                if name.startswith("term_bank_") and name.endswith(".json")
            ]
        return sorted(names, key=lambda p: int(os.path.basename(p).split("_")[-1].split(".")[0]))

    def index(self):
        if self.zip_file:
            for name in self.zip_file.namelist():
                if os.path.basename(name) == "index.json":
                    with self.zip_file.open(name) as f:
                        return json.load(io.TextIOWrapper(f, encoding="utf-8"))
            return {}
        index_path = os.path.join(self.path, "index.json")
        if not os.path.exists(index_path):
            return {}
        with open(index_path, "r", encoding="utf-8") as f:
            return json.load(f)

    def read_json(self, path):
        if self.zip_file:
            with self.zip_file.open(path) as f:
                return json.load(io.TextIOWrapper(f, encoding="utf-8"))
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)


def load_yomitan_entries(src_path):
    source = YomitanSource(src_path)
    entries = []
    try:
        for path in source.term_bank_paths():
            for raw in source.read_json(path):
                term = raw[0] or ""
                if not term:
                    continue
                glossary = raw[5] or []
                tags = set()
                tag_codes(glossary, tags)
                score = int(raw[4] or 0)
                sequence = int(raw[6] or 0)
                entries.append(
                    {
                        "term": term,
                        "reading": raw[1] or "",
                        "score": score,
                        "definition": flatten_glossary(glossary),
                        "sequence": sequence,
                        "tags": tags,
                        "tier": classify_tier(score, sequence, tags),
                        "redirect_target": redirect_target(glossary),
                    }
                )
        return source.index(), entries
    finally:
        source.close()


def resolve_redirects(entries):
    by_term = {}
    by_sequence = {}
    for entry in entries:
        if entry["redirect_target"]:
            continue
        current = by_term.get(entry["term"])
        if current is None or (entry["tier"], -entry["score"], entry["reading"]) < (
            current["tier"],
            -current["score"],
            current["reading"],
        ):
            by_term[entry["term"]] = entry
        if entry["sequence"] > 0:
            current = by_sequence.get(entry["sequence"])
            if current is None or (entry["tier"], -entry["score"], entry["term"]) < (
                current["tier"],
                -current["score"],
                current["term"],
            ):
                by_sequence[entry["sequence"]] = entry

    resolved = 0
    unresolved = 0
    for entry in entries:
        target = entry["redirect_target"]
        if not target:
            continue
        canonical = by_term.get(target)
        if canonical is None and entry["sequence"] < 0:
            canonical = by_sequence.get(-entry["sequence"])
        if canonical is None:
            unresolved += 1
            continue

        entry["reading"] = entry["reading"] or canonical["reading"]
        entry["definition"] = canonical["definition"]
        entry["tags"] = set(entry["tags"]) | set(canonical["tags"])
        resolved += 1
    return resolved, unresolved


def make_lookup_entry(entry, key=None, alias_kind="term"):
    copied = dict(entry)
    copied["key"] = key if key is not None else entry["term"]
    copied["alias_kind"] = alias_kind
    return copied


def convert(args):
    os.makedirs(args.out_dir, exist_ok=True)
    index, raw_entries = load_yomitan_entries(args.src)
    resolved_redirects, unresolved_redirects = resolve_redirects(raw_entries)

    entries = []
    skipped = {"first_char_scope": 0, "min_score": 0, "tags": 0, "negative_sequence": 0}
    added_reading_aliases = 0
    drop_tags = {tag.strip() for tag in args.drop_tags.split(",") if tag.strip()}

    for entry in raw_entries:
        if args.min_score is not None and entry["score"] < args.min_score:
            skipped["min_score"] += 1
            continue
        if args.drop_negative_sequence and entry["sequence"] < 0:
            skipped["negative_sequence"] += 1
            continue
        if drop_tags and entry["tags"].intersection(drop_tags):
            skipped["tags"] += 1
            continue

        if include_first_char(entry["term"], args.first_char_scope):
            entries.append(make_lookup_entry(entry))
        else:
            skipped["first_char_scope"] += 1

        if args.reading_aliases and entry["reading"] and entry["reading"] != entry["term"]:
            if include_first_char(entry["reading"], args.first_char_scope):
                entries.append(make_lookup_entry(entry, entry["reading"], "reading"))
                added_reading_aliases += 1

    entries.sort(key=lambda e: (e["key"], e["tier"], -e["score"], e["reading"], e["term"], e["sequence"]))

    deduplicated_lookup_records = 0
    deduped = []
    seen = set()
    for entry in entries:
        payload_key = (entry["key"], entry["term"], entry["reading"], entry["definition"])
        if payload_key in seen:
            deduplicated_lookup_records += 1
            continue
        seen.add(payload_key)
        deduped.append(entry)
    entries = deduped

    buckets = [(0, 0)] * UNICODE_BUCKETS
    bucket_start = {}
    bucket_count = {}
    for idx, entry in enumerate(entries):
        cp = ord(entry["key"][0])
        bucket_start.setdefault(cp, idx)
        bucket_count[cp] = bucket_count.get(cp, 0) + 1
    for cp, start in bucket_start.items():
        buckets[cp] = (start, bucket_count[cp])

    key_filter_keys = write_key_filter(entries, os.path.join(args.out_dir, "key_filter.bin"))

    with open(os.path.join(args.out_dir, "buckets.bin"), "wb") as f:
        for start, count in buckets:
            f.write(BUCKET_STRUCT.pack(start, count))

    with open(os.path.join(args.out_dir, "records.bin"), "wb") as records, open(
        os.path.join(args.out_dir, "strings.bin"), "wb"
    ) as strings:
        string_offsets = {}

        def write_string(text):
            data = text.encode("utf-8")
            existing = string_offsets.get(data)
            if existing is not None:
                return existing
            offset = strings.tell()
            strings.write(data)
            value = (offset, len(data))
            string_offsets[data] = value
            return value

        for entry in entries:
            key = safe_prefix_bytes(entry["key"], KEY_BYTES)
            term_off, term_len = write_string(entry["term"])
            reading_off, reading_len = write_string(entry["reading"])
            def_off, def_len = write_string(entry["definition"])
            records.write(
                RECORD_STRUCT.pack(
                    key.ljust(KEY_BYTES, b"\0"),
                    len(key),
                    entry["tier"],
                    1 if entry["alias_kind"] == "reading" else 0,
                    term_off,
                    term_len,
                    reading_off,
                    reading_len,
                    def_off,
                    def_len,
                    entry["score"],
                    entry["sequence"],
                )
            )

    meta = {
        "format": "cardputer-jpdict",
        "version": 1,
        "title": index.get("title", ""),
        "revision": index.get("revision", ""),
        "entry_count": len(entries),
        "record_size": RECORD_STRUCT.size,
        "key_bytes": KEY_BYTES,
        "unicode_buckets": UNICODE_BUCKETS,
        "key_filter": {
            "file": "key_filter.bin",
            "bytes": BLOOM_FILTER_BYTES,
            "hashes": BLOOM_HASHES,
            "keys": key_filter_keys,
            "hash": "fnv1a32-double",
        },
        "default_install_path": "/jpdict",
        "record_fields": [
            "key_prefix",
            "key_len",
            "tier",
            "flags",
            "term_offset",
            "term_len",
            "reading_offset",
            "reading_len",
            "definition_offset",
            "definition_len",
            "score",
            "sequence",
        ],
        "tiers": {
            "0": "common/high-priority modern",
            "1": "modern",
            "2": "rare_or_dated",
            "3": "archaic_obsolete_or_old_variant",
        },
        "filters": {
            "first_char_scope": args.first_char_scope,
            "reading_aliases": args.reading_aliases,
            "added_reading_aliases": added_reading_aliases,
            "deduplicated_lookup_records": deduplicated_lookup_records,
            "min_score": args.min_score,
            "drop_tags": sorted(drop_tags),
            "drop_negative_sequence": args.drop_negative_sequence,
            "skipped": skipped,
            "redirects": {"resolved": resolved_redirects, "unresolved": unresolved_redirects},
        },
    }
    with open(os.path.join(args.out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)

    print(f"wrote {len(entries)} lookup records to {args.out_dir}")
    for name in ("manifest.json", "buckets.bin", "records.bin", "strings.bin", "key_filter.bin"):
        path = os.path.join(args.out_dir, name)
        print(f"{name}: {os.path.getsize(path):,} bytes")


class JapaneseDictFile:
    def __init__(self, path):
        self.path = path
        self.buckets = open(os.path.join(path, "buckets.bin"), "rb")
        self.records = open(os.path.join(path, "records.bin"), "rb")
        self.strings = open(os.path.join(path, "strings.bin"), "rb")

    def close(self):
        self.buckets.close()
        self.records.close()
        self.strings.close()

    def bucket(self, cp):
        self.buckets.seek(cp * BUCKET_STRUCT.size)
        return BUCKET_STRUCT.unpack(self.buckets.read(BUCKET_STRUCT.size))

    def record_key(self, index):
        self.records.seek(index * RECORD_STRUCT.size)
        rec = self.records.read(RECORD_STRUCT.size)
        if len(rec) != RECORD_STRUCT.size:
            return "", None
        fields = RECORD_STRUCT.unpack(rec)
        key = fields[0][: fields[1]].decode("utf-8")
        return key, fields

    def read_string(self, offset, length):
        self.strings.seek(offset)
        return self.strings.read(length).decode("utf-8", errors="replace")

    def find_exact(self, key):
        if not key:
            return []
        start, count = self.bucket(ord(key[0]))
        lo, hi = start, start + count
        while lo < hi:
            mid = (lo + hi) // 2
            rec_key, _ = self.record_key(mid)
            if rec_key < key:
                lo = mid + 1
            else:
                hi = mid

        results = []
        pos = lo
        while pos < start + count:
            rec_key, fields = self.record_key(pos)
            if rec_key != key:
                break
            results.append(
                {
                    "key": rec_key,
                    "tier": fields[2],
                    "flags": fields[3],
                    "term": self.read_string(fields[4], fields[5]),
                    "reading": self.read_string(fields[6], fields[7]),
                    "definition": self.read_string(fields[8], fields[9]),
                    "score": fields[10],
                    "sequence": fields[11],
                }
            )
            pos += 1
        results.sort(key=lambda r: (r["tier"], -r["score"], r["flags"], r["term"], r["reading"]))
        return results


def lookup(args):
    dictionary = JapaneseDictFile(args.dict_dir)
    try:
        start = time.perf_counter_ns()
        matches = dictionary.find_exact(args.query)
        elapsed_us = (time.perf_counter_ns() - start) / 1000
        print(f"{len(matches)} matches in {elapsed_us:.1f} us")
        for match in matches[: args.limit]:
            alias = "reading" if match["flags"] & 1 else "term"
            print(
                f"{match['term']} [{match['reading']}] key={match['key']} alias={alias} "
                f"tier={match['tier']} score={match['score']} seq={match['sequence']}"
            )
            print(f"  {match['definition'][:240]}")
    finally:
        dictionary.close()


def bench(args):
    dictionary = JapaneseDictFile(args.dict_dir)
    try:
        samples = []
        record_count = os.path.getsize(os.path.join(args.dict_dir, "records.bin")) // RECORD_STRUCT.size
        for _ in range(args.samples):
            key, _ = dictionary.record_key(random.randrange(record_count))
            samples.append(key)

        timings = []
        for sample in samples:
            start = time.perf_counter_ns()
            dictionary.find_exact(sample)
            timings.append((time.perf_counter_ns() - start) / 1000)
        timings.sort()
        print(f"samples={len(timings)}")
        print(f"p50={timings[len(timings)//2]:.1f} us")
        print(f"p95={timings[int(len(timings)*0.95)]:.1f} us")
        print(f"max={timings[-1]:.1f} us")
    finally:
        dictionary.close()


def inspect(args):
    source = YomitanSource(args.src)
    try:
        index = source.index()
        paths = source.term_bank_paths()
        print(f"title: {index.get('title', '')}")
        print(f"revision: {index.get('revision', '')}")
        print(f"term banks: {len(paths)}")
        total = 0
        for path in paths:
            total += len(source.read_json(path))
        print(f"term entries: {total}")
    finally:
        source.close()


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(required=True)

    p = sub.add_parser("inspect")
    p.add_argument("src")
    p.set_defaults(func=inspect)

    p = sub.add_parser("convert")
    p.add_argument("src")
    p.add_argument("out_dir")
    p.add_argument("--first-char-scope", choices=("all", "kanji", "japanese"), default="japanese")
    p.add_argument("--no-reading-aliases", dest="reading_aliases", action="store_false")
    p.set_defaults(reading_aliases=True)
    p.add_argument("--min-score", type=int)
    p.add_argument("--drop-tags", default="")
    p.add_argument("--drop-negative-sequence", action="store_true")
    p.set_defaults(func=convert)

    p = sub.add_parser("lookup")
    p.add_argument("dict_dir")
    p.add_argument("query")
    p.add_argument("--limit", type=int, default=5)
    p.set_defaults(func=lookup)

    p = sub.add_parser("bench")
    p.add_argument("dict_dir")
    p.add_argument("--samples", type=int, default=1000)
    p.set_defaults(func=bench)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
