# cardputer-jpdict

Offline Japanese electronic dictionary firmware for the M5Stack Cardputer-Adv.
The device boots into a keyboard-driven lookup UI, converts romaji input to
kana as you type, and searches a preprocessed Yomitan/Jitendex dictionary from
the microSD card. Results are shown in a compact layout with readable Japanese
fonts, exact and prefix matches, de-inflection hints, full-definition browsing,
and battery/status indicators.

The firmware also includes an optional single-kanji input helper: open Kanji
Search, type a kanji reading, radical name, radical stroke count, or total kanji stroke count in one
combined input, choose radical filters or an individual kanji, and insert the
kanji into the query before running a normal dictionary lookup. Dictionary and
kanji data are prepared once on the host and copied to the microSD card, so the
device does not need to parse or index large source files at startup.

## Usage

Type romaji to compose kana, then press `Enter` to search. Use Up/Down to move
through results and `Enter` to open a full definition. `Esc` exits the current
screen or result list. Press the right arrow from the search screen to open
Kanji Search with an empty kanji lookup input. In Kanji Search, the same input
searches kanji readings, radical names, and numeric
stroke counts for both parts and full kanji. Parts and Kanji results are shown as separate panes when
both exist, or full-width when only one exists. Choose a part to add it to the
shared radical filter list, or choose a kanji to insert it into the query. `Del`
deletes text, and `Ctrl` shows contextual help for the current screen.

## Setup

```sh
uv sync
```

## Build

```sh
uv run pio run
```

## Flash

Connect the Cardputer-Adv by USB, then run:

```sh
uv run pio run -t upload
```

If upload does not start, enter download mode: set the side power switch to
`OFF`, hold `G0`, apply USB power, then release `G0`.

## Serial Monitor

```sh
uv run pio device monitor -b 115200
```

The firmware emits startup and lookup diagnostics that are useful for checking
SD-card, dictionary, and kanji-index state.

## Dictionary Conversion

Inspect the sample Yomitan/Jitendex zip:

```sh
uv run python scripts/build_jpdict.py inspect test/jitendex-yomitan.zip
```

Build an SD-card-ready dictionary directory:

```sh
uv run python scripts/build_jpdict.py convert test/jitendex-yomitan.zip build/sd/jpdict
```

Test host lookup before copying to SD:

```sh
uv run python scripts/build_jpdict.py lookup build/sd/jpdict にほんご
```

The device expects these files under `/jpdict` on the microSD card:

- `manifest.json`
- `buckets.bin`
- `records.bin`
- `strings.bin`
- `key_filter.bin`

`key_filter.bin` is a generated Bloom filter used to skip dictionary lookups
for keys that definitely do not exist, which speeds up misses and fallback
search paths. Lookup remains functional if the file is absent or cannot be
loaded; the firmware simply falls back to probing the main dictionary index.

## Kanji Lookup Index

Build the optional consolidated kanji lookup index from KANJIDIC2 and KRADFILE:

```sh
python3 scripts/build_kanji_index.py convert-all test/kanjidic2.xml.gz test/kradfile build/sd/kanji
```

The device expects these files under `/kanji` on the microSD card:

- `manifest.json`
- `lookup.records.bin`
- `lookup.strings.bin`

Test host lookups before copying to SD:

```sh
python3 scripts/build_kanji_index.py lookup build/sd/kanji き
python3 scripts/build_kanji_index.py lookup-radical build/sd/kanji ごんべん
python3 scripts/build_kanji_index.py lookup-component build/sd/kanji 言
python3 scripts/build_kanji_index.py lookup-strokes build/sd/kanji 3
python3 scripts/build_kanji_index.py lookup-kanji-strokes build/sd/kanji 3
```

Copy the generated `build/sd/kanji` directory to `/kanji` on the microSD card.
Normal dictionary lookup still works if `/kanji` is absent.
