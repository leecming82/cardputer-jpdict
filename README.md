# cardputer-jpdict

Offline Japanese electronic dictionary firmware for the M5Stack Cardputer-Adv.
The device boots into a keyboard-driven lookup UI, converts romaji input to
kana as you type, and searches a preprocessed Yomitan/Jitendex dictionary from
the microSD card. Results are shown in a compact layout with readable Japanese
fonts, exact and prefix matches, de-inflection hints, full-definition browsing,
and battery/status indicators.

The firmware also includes an optional single-kanji input helper: type a kana
reading, open the kanji picker, choose an individual kanji, and insert it into
the query before running a normal dictionary lookup. Dictionary and kanji data
are prepared once on the host and copied to the microSD card, so the device does
not need to parse or index large source files at startup.

## Usage

Type romaji to compose kana, then press `Enter` to search. Use the arrow keys
to move through results, `Enter` or the right arrow to open a full definition,
and the left arrow or `Del` to go back. Press the right arrow from the search
screen to open the kanji picker for the trailing kana reading. `Tab` clears the
query, and `Ctrl` shows contextual help for the current screen.

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

## Kanji Reading Index

Build the optional kanji picker index from KANJIDIC2:

```sh
python3 scripts/build_kanji_index.py convert test/kanjidic2.xml.gz build/sd/kanji
```

Test a host lookup:

```sh
python3 scripts/build_kanji_index.py lookup build/sd/kanji き
```

Copy the generated `build/sd/kanji` directory to `/kanji` on the microSD card.
Normal dictionary lookup still works if `/kanji` is absent.
