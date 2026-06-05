# Cardputer Japanese Dictionary Implementation Plan

## Goals

- Build an offline Japanese dictionary for the M5Stack Cardputer-Adv.
- Boot directly into an interactive lookup UI after one-time SD-card setup.
- Accept romaji input, convert it to kana in firmware, and look up kana reading aliases in the dictionary.
- Keep runtime startup fast: no on-device Yomitan JSON parsing, decompression, or indexing after SD-card preprocessing.

## Key Decisions

- Use host-side preprocessing to convert Yomitan/Jitendex into an SD-friendly dictionary format.
- Generate kana reading aliases during preprocessing.
- Do not generate romaji aliases in the dictionary; romaji-to-kana belongs in firmware input logic.
- Start with a full-query romaji-to-hiragana converter, then add optional IME-style incremental composition later.
- Start implementation with the interactive UI shell so screen and keyboard constraints are visible early.

## Milestones

### 1. Interactive UI Shell

Replace hello-world with a real lookup screen.

Requirements:
- Keyboard input.
- Backspace.
- Clear query.
- Enter/search.
- Result navigation.
- Dummy in-memory results.

Testable outcome:
- Flash the device.
- Type `nihongo`.
- Press Enter.
- See fake dictionary result pages update on screen.

Status: Done.

### 2. Romaji-to-Kana Module

Add a standalone C++ romaji-to-hiragana converter.

Requirements:
- Full-query conversion first.
- Common Hepburn-style inputs.
- Quick on-device Japanese font smoke test using M5GFX bundled fonts.
- Cases such as:
  - `nihongo` -> `にほんご`
  - `gakkou` -> `がっこう`
  - `kyo` -> `きょ`
  - `kanji` -> `かんじ`
- Host tests where practical.

Testable outcome:
- Firmware can show converted kana from romaji input.
- Host tests validate conversion cases.

Status: Done.

### 3. SD Card Boot Probe

Initialize SD card and detect expected data paths.

Requirements:
- Boot status for SD card.
- Boot status for dictionary files.
- UI remains usable if data is missing.

Testable outcome:
- Boot shows one of:
  - `SD OK`
  - `Dictionary missing`
  - `Dictionary OK`

Status: Done.

### 4. Dictionary Converter Import

Adapt the Yomitan/Jitendex converter from `~/crosspoint-reader` branch `japanese-dictionary-fusion`.

Requirements:
- Convert `test/jitendex-yomitan.zip` into SD-card files.
- Produce:
  - `manifest.json`
  - `buckets.bin`
  - `records.bin`
  - `strings.bin`
- Add kana reading aliases.
- Avoid romaji aliases.

Testable outcome:
- Host command creates converted dictionary files.
- Host lookup for `にほんご` returns real entries.

Status: Done.

### 5. Firmware Dictionary Reader

Port the minimal dictionary reader to Arduino/SD APIs.

Requirements:
- Open converted dictionary from SD.
- Exact lookup by kana key first.
- Bounded RAM usage.
- Fixed result cap.

Testable outcome:
- With converted files on SD, typed `nihongo` converts to kana and returns real Jitendex results on device.

Status: Done.

### 6. Result Rendering

Render real dictionary results in a Cardputer-friendly layout.

Requirements:
- Dense small-screen UI.
- Headword.
- Reading.
- Short gloss.
- Result index.
- Wrapped/paginated definitions.
- No overlapping text.

Testable outcome:
- Long definitions page cleanly without freezing input or corrupting layout.

Status: Done.

### 7. Japanese Font Path

Choose and implement the Japanese font rendering path.

Initial approach:
- Use M5GFX available Japanese fonts if readable enough for the first dictionary milestone.

Robust approach:
- Adapt CrossPoint `.cpfont` conversion/runtime loading from the Noto Serif JP TTF.

Testable outcome:
- Headwords, readings, and definitions render readable Japanese on device from real results.

Status: Done. Current path uses M5GFX embedded IPA/IPAex Japanese bitmap fonts.
This is not a full source-font glyph set, so rarer kanji, names, old forms, or
special symbols may still require an SD-loaded converted font later.

### 8. IME-Style Composition Option

Add optional incremental romaji composition.

Requirements:
- Display committed kana plus pending romaji on one compact input line.
- Handle progressive typing:
  - `n` remains pending.
  - `ni` -> `に`
  - `nihon` -> `にほn`
  - `nihong` -> `にほんg`
  - `nihongo` -> `にほんご`
- Search uses the finalized kana string.
- Backspace edits pending romaji first, then committed kana.
- Keep raw romaji mode out of the primary UI unless later debugging needs it.

Testable outcome:
- Typing `nihongo` visibly composes `にほんご` character by character.
- Lookup uses the composed kana.

Status: Done.

### 9A. Partial Search

Improve typed lookup quality beyond exact kana matches.

Requirements:
- Add bounded prefix/partial search, e.g. `にほ` can suggest `日本語`.
- Show exact results first, then partial matches.
- Keep partial lookup bounded so SD reads remain predictable.
- Preserve exact-match priority when both exact and partial results exist.
- Deduplicate exact and partial matches.

Testable outcome:
- Partial queries return useful ranked candidates without scanning the whole dictionary.
- Exact matches still appear first.

Status: Done.

### 9B. De-Inflection

Discuss and design lookup for inflected surface forms.

Requirements:
- Decide whether de-inflection belongs in this typed dictionary workflow.
- Consider common verb/adjective forms, e.g. past, negative, polite, te-form.
- Generate bounded candidate base forms without full morphological analysis.
- Label de-inflected results clearly if implemented.
- Avoid ranking noisy guesses above exact typed matches.

Testable outcome:
- De-inflection rules and UI labeling are documented before implementation.
- If implemented later, inflected forms such as `たべた` can find `食べる`
  without degrading exact and partial lookup behavior.

Status: Done.

### 10. Single-Kanji Input by Reading

Add an optional input helper for inserting individual kanji from kana readings.

Approach:
- Generate an auxiliary SD-side kanji reading index separate from dictionary data.
- User types a kana reading, opens a kanji picker, selects one kanji, and inserts
  it into the query/input buffer.
- Keep this separate from full multi-kanji IME conversion.

Requirements:
- Support kanji with multiple readings.
- Candidate list must be bounded and navigable on the Cardputer screen.
- Insert selected kanji into the current query without breaking kana composition.
- Keep dictionary lookup independent from kanji input data.

Testable outcome:
- Typing `き`, opening the picker, and selecting `貴` inserts `貴`.
- User can combine inserted kanji with typed kana and run a normal lookup.
- Missing/absent kanji index does not block normal kana dictionary lookup.

Status: Done.

### 11. Custom Font and Glyph Coverage

Use a wider built-in M5GFX Japanese font path for rare, old-form, and variant
kanji not covered by the default M5GFX IPA Japanese fonts.

Motivation:
- M5GFX rendered common terms well but showed tofu for `屬` in `貴金屬`.
- Dictionary data can include old forms, names, and rare kanji beyond the bundled
  embedded font subset.

Implemented approach:
- M5GFX includes several Japanese/CJK font families:
  - `lgfxJapanGothic*` / `lgfxJapanMincho*`: IPA/IPAex-derived Japanese fonts.
  - `efontJA_*`: broader Japanese efont set.
  - `efontCN_*`: simplified Chinese-oriented efont set.
  - `efontTW_*`: traditional Chinese/Taiwan-oriented efont set.
- Local coverage probing showed `efontJA_16` includes old-form glyphs that the
  IPA-derived fonts lacked, including `屬`, `體`, and `龜`.
- Switched the app from `lgfxJapanGothicP_12`/`lgfxJapanGothicP_16` to
  `efontJA_12`/`efontJA_16`.
- This keeps rendering inside M5GFX rather than adding a custom mixed glyph
  renderer.

Notes:
- `efontJA` increased firmware flash usage from about 24.2% to about 39.4%.
- RAM usage stayed effectively unchanged.
- `efontTW` has good traditional coverage but lacks some Japanese simplified
  forms in local probing, so `efontJA` is the better default for this app.
- A future SD-loaded `.vlw` or custom converted font remains possible if even
  broader coverage is needed.

Testable outcome:
- Known missing glyphs such as `屬` render correctly.
- Common result pages remain responsive enough on-device.
- Text width/wrapping remains accurate for dictionary headwords and definitions.

Status: Done.

### 12. Final Tweaks

Polish the single-dictionary product after the core lookup path works.

Areas:
- [Done] Search input emphasis:
  - Show the active query/input in a larger headword-sized font before results
    are displayed.
  - Reduce the input line back to the compact header size once results are shown.
  - Keep preview/result mode compact.
  - In full definition view, show the headword/query area at headword size.
- [In progress] UI layout optimizations for long words, long readings, and dense
  definitions.
- [Done] Marquee/ticker behavior for overflowing reading/tag metadata lines.
- [Done] Battery indicator at the right end of the blue search entry line, ideally as a
  compact symbol plus percentage if the Cardputer Adv reports valid values.
- [Done] Optional battery status display using M5.Power battery
  level/voltage/charging APIs, if the Cardputer Adv reports valid values
  on-device.
- Result ranking and filtering tweaks based on real use.
- Lookup latency measurement and performance tuning.
- SD-card read caching if real-device lookup feels uneven.
- [Done] Startup messaging and error states.
- Input ergonomics after extended typing: review key mappings, editing behavior,
  long-query handling, accidental key presses, and whether repeated romaji/kana
  entry feels comfortable after real use.
- Memory and flash-size review.

Testable outcome:
- Common lookups feel responsive on-device.
- Result pages are readable without obvious clipping or confusing controls.
- Startup and missing-data states are clear.
- No large avoidable memory regressions remain.

Status: Pending

### 13. Config Settings

Add a simple configuration surface for user-tunable lookup and UI behavior.

Initial settings:
- Number of result slots shown/kept, if final-tweak testing shows the default
  is not enough.
- Prefix/partial search scan cap, if lookup performance varies by SD card or
  dictionary size.
- Search mode options if needed, e.g. exact-only vs exact+partial.
- Display density options if needed, e.g. larger headword vs more gloss lines.
- Future toggles for battery display, metadata marquee, themes, and dictionaries.

Requirements:
- Settings must persist across reboots.
- Defaults should match the polished single-dictionary workflow.
- Settings UI must fit the Cardputer keyboard/screen constraints.
- Invalid or missing config should fall back safely.

Testable outcome:
- User can change a real, justified setting and see behavior update.
- Device preserves the setting after restart.
- Bad/missing config does not block dictionary startup.

Status: Pending

### 14. Startup Performance

Validate power-on behavior after the core product surface is stable.

Requirements:
- No full dictionary scan at boot.
- Load only minimal metadata.
- Bounded first lookup time.
- Reassess after font, config, lookup, and UI components settle.

Testable outcome:
- Device powers on into usable lookup UI quickly.
- First lookup performs bounded SD reads and returns without noticeable startup indexing.

Status: Pending

### 15. Bonus: Multi-Dictionary Management

Add support for installing and managing multiple dictionaries, such as a Japanese
names dictionary alongside Jitendex.

Deferred until after single-dictionary lookup is working.

Potential layout:
- `/dicts/jitendex/...`
- `/dicts/jmnedict/...`
- `/dicts/<dictionary-id>/manifest.json`
- `/dicts/<dictionary-id>/buckets.bin`
- `/dicts/<dictionary-id>/records.bin`
- `/dicts/<dictionary-id>/strings.bin`

Requirements:
- Discover dictionaries from SD-card manifests.
- Enable or disable dictionaries.
- Define dictionary priority/order.
- Merge results from multiple dictionaries.
- Preserve dictionary title/source in result metadata.
- Support different dictionary categories, e.g. general words vs names.

Testable outcome:
- Device can detect at least two installed dictionaries.
- Lookup can return and label results from both dictionaries.
- User can disable one dictionary without removing files from SD.

Status: Backlog

### 16. Bonus: UI Themes

Add optional UI themes after the core dictionary workflow is stable.

Initial theme candidates:
- Current color theme.
- Monochrome TTY-style theme.

Requirements:
- Theme colors are centralized instead of scattered through drawing code.
- Theme switching does not affect input or lookup behavior.
- Monochrome mode remains legible on the Cardputer display.

Testable outcome:
- User can switch to a monochrome TTY-style UI.
- Result browsing and definition scrolling remain readable.

Status: Backlog
