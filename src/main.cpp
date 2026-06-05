#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "JapaneseDictionary.h"
#include "KanjiIndex.h"
#include "M5Cardputer.h"
#include "RomajiKana.h"

namespace {
constexpr size_t kMaxQueryChars = 32;
constexpr int kSdSckPin = 40;
constexpr int kSdMisoPin = 39;
constexpr int kSdMosiPin = 14;
constexpr int kSdCsPin = 12;

constexpr const char* kDictionaryPaths[] = {
    "/jpdict",
    "/dict/jitendex-cpdict-ranked-japanese",
    "/dict/jitendex-cpdict-ranked",
    "/dict/jitendex-cpdict-modern",
};

constexpr size_t kMaxResults = 6;
constexpr size_t kMaxKanjiCandidates = 64;
constexpr size_t kVisibleKanjiCandidates = 24;
constexpr size_t kKanjiGridColumns = 6;
constexpr uint32_t kMarqueeFrameMs = 180;
constexpr uint32_t kMarqueePauseMs = 1200;
constexpr uint32_t kMarqueeMsPerPixel = 70;

String committedKana;
String pendingRomaji;
String lastSearched;
String lastSearchedKana;
size_t selectedResult = 0;
size_t resultCount = 0;
JapaneseDictionary dictionary;
JapaneseDictionaryMatch results[kMaxResults];
KanjiIndex kanjiIndex;
String kanjiCandidates[kMaxKanjiCandidates];
size_t selectedKanjiCandidate = 0;
size_t kanjiCandidateCount = 0;
String kanjiReading;
bool searched = false;
bool dirty = true;
bool marqueeActive = false;
uint32_t lastMarqueeFrame = 0;
int marqueeX = 0;
int marqueeY = 0;
int marqueeWidth = 0;
int marqueeHeight = 0;
String marqueeText;
uint16_t marqueeColor = TFT_WHITE;
uint16_t marqueeBackground = TFT_BLACK;
String storageDetail;
String dictionaryBasePath;
String kanjiIndexStatus = "not checked";

enum class StorageState {
  SdChecking,
  SdMissing,
  SdOk,
  DictionaryOk,
  DictionaryMissing,
  DictionaryOpenFailed,
};

enum class ViewMode {
  Results,
  Definition,
  KanjiPicker,
};

StorageState storageState = StorageState::SdChecking;
ViewMode viewMode = ViewMode::Results;
int definitionScrollLine = 0;

constexpr int kCompactContentTop = 26;
constexpr int kLargeContentTop = 43;
constexpr int kFooterTop = 117;
constexpr int kSmallBodyLineHeight = 15;
constexpr int kLargeBodyLineHeight = 20;

String currentInputText();

bool usesLargeSearchHeader() {
  return viewMode == ViewMode::Results && !searched &&
         currentInputText().length() > 0;
}

int contentTop() {
  return usesLargeSearchHeader() ? kLargeContentTop : kCompactContentTop;
}

String joinPath(const char* base, const char* leaf) {
  String path = base;
  if (!path.endsWith("/")) {
    path += "/";
  }
  path += leaf;
  return path;
}

bool hasDictionaryFiles(const char* basePath) {
  return SD.exists(joinPath(basePath, "manifest.json")) &&
         SD.exists(joinPath(basePath, "buckets.bin")) &&
         SD.exists(joinPath(basePath, "records.bin")) &&
         SD.exists(joinPath(basePath, "strings.bin"));
}

String startupStatusLine() {
  switch (storageState) {
    case StorageState::DictionaryOk:
      return "Dict OK  SD OK";
    case StorageState::SdMissing:
      return "SD card missing";
    case StorageState::DictionaryMissing:
      return "Dictionary missing";
    case StorageState::DictionaryOpenFailed:
      return "Dictionary open failed";
    case StorageState::SdOk:
      return "SD OK";
    case StorageState::SdChecking:
      return "SD checking";
  }
  return "SD checking";
}

String startupDetailLine() {
  switch (storageState) {
    case StorageState::DictionaryOk:
      return dictionaryBasePath;
    case StorageState::SdMissing:
      return "Insert card and restart";
    case StorageState::DictionaryMissing:
      return "Expected /jpdict";
    case StorageState::DictionaryOpenFailed:
      return "Check /jpdict files";
    case StorageState::SdOk:
    case StorageState::SdChecking:
      return storageDetail;
  }
  return storageDetail;
}

uint16_t startupStatusColor() {
  return storageState == StorageState::DictionaryOk ? TFT_GREEN : TFT_ORANGE;
}

bool hasKanjiIndexFiles(const char* basePath) {
  return SD.exists(joinPath(basePath, "manifest.json")) &&
         SD.exists(joinPath(basePath, "buckets.bin")) &&
         SD.exists(joinPath(basePath, "records.bin")) &&
         SD.exists(joinPath(basePath, "strings.bin"));
}

bool logKanjiIndexFiles(const char* basePath) {
  const bool hasManifest = SD.exists(joinPath(basePath, "manifest.json"));
  const bool hasBuckets = SD.exists(joinPath(basePath, "buckets.bin"));
  const bool hasRecords = SD.exists(joinPath(basePath, "records.bin"));
  const bool hasStrings = SD.exists(joinPath(basePath, "strings.bin"));
  Serial.printf("Kanji index files at %s: manifest=%u buckets=%u records=%u strings=%u\n",
                basePath, hasManifest, hasBuckets, hasRecords, hasStrings);
  return hasManifest && hasBuckets && hasRecords && hasStrings;
}

bool openKanjiIndex(const char* reason) {
  if (kanjiIndex.isOpen()) {
    kanjiIndexStatus = "OK /kanji";
    return true;
  }

  Serial.printf("Kanji index check (%s)\n", reason);
  if (!logKanjiIndexFiles("/kanji")) {
    kanjiIndexStatus = "missing /kanji files";
    Serial.println("Kanji index missing/open skipped: /kanji");
    return false;
  }

  if (kanjiIndex.open("/kanji")) {
    kanjiIndexStatus = "OK /kanji";
    Serial.println("Kanji index opened: /kanji");
    return true;
  }

  kanjiIndexStatus = "open failed /kanji";
  Serial.println("Kanji index open failed: /kanji");
  return false;
}

void probeStorage() {
  storageState = StorageState::SdMissing;
  storageDetail = "Insert card and restart";
  dictionaryBasePath = "";

  SPI.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  if (!SD.begin(kSdCsPin, SPI, 25000000, "/sd", 10)) {
    Serial.println("SD init failed");
    return;
  }

  const uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  const uint64_t cardSizeMb = SD.cardSize() / (1024 * 1024);
  storageState = StorageState::SdOk;
  storageDetail = String(cardSizeMb) + "MB";

  for (const char* path : kDictionaryPaths) {
    if (hasDictionaryFiles(path)) {
      dictionaryBasePath = path;
      storageState = StorageState::DictionaryOk;
      storageDetail = dictionaryBasePath;
      Serial.printf("Dictionary found: %s\n", dictionaryBasePath.c_str());
      if (dictionary.open(dictionaryBasePath.c_str())) {
        Serial.printf("Dictionary opened: %s\n", dictionary.path().c_str());
      } else {
        storageState = StorageState::DictionaryOpenFailed;
        storageDetail = dictionaryBasePath;
        Serial.printf("Dictionary open failed: %s\n", dictionaryBasePath.c_str());
      }
      openKanjiIndex("startup");
      return;
    }
  }

  storageState = StorageState::DictionaryMissing;
  storageDetail = "Expected /jpdict";
  Serial.printf("SD OK (%lluMB), dictionary missing\n", cardSizeMb);
}

int textWidth(const String &text) {
  return M5Cardputer.Display.textWidth(text);
}

String ellipsize(const String &text, int maxWidth) {
  if (textWidth(text) <= maxWidth) {
    return text;
  }

  String out = text;
  while (out.length() > 0 && textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

void drawTextLine(int x, int y, int width, const String &text, uint16_t color,
                  uint16_t background) {
  auto &display = M5Cardputer.Display;
  display.fillRect(x, y, width, 16, background);
  display.setTextDatum(top_left);
  display.setTextColor(color, background);
  display.drawString(ellipsize(text, width), x, y);
}

int marqueeOffsetForText(const String& text, int width) {
  const int renderedWidth = textWidth(text);
  if (renderedWidth <= width) {
    return 0;
  }

  const int travel = renderedWidth - width + 18;
  const uint32_t scrollMs = travel * kMarqueeMsPerPixel;
  const uint32_t cycleMs = kMarqueePauseMs + scrollMs + kMarqueePauseMs;
  const uint32_t pos = millis() % cycleMs;
  int offset = 0;
  if (pos >= kMarqueePauseMs) {
    const uint32_t moving = pos - kMarqueePauseMs;
    offset = moving >= scrollMs ? travel : moving / kMarqueeMsPerPixel;
  }
  return offset;
}

void drawMarqueeFrame() {
  auto& display = M5Cardputer.Display;
  if (!marqueeActive) {
    return;
  }

  display.setFont(&fonts::efontJA_12);
  display.fillRect(marqueeX, marqueeY, marqueeWidth, marqueeHeight,
                   marqueeBackground);
  display.setTextDatum(top_left);
  display.setTextColor(marqueeColor, marqueeBackground);
  display.setClipRect(marqueeX, marqueeY, marqueeWidth, marqueeHeight);
  display.drawString(marqueeText,
                     marqueeX - marqueeOffsetForText(marqueeText, marqueeWidth),
                     marqueeY);
  display.clearClipRect();
}

void drawMarqueeText(int x, int y, int width, int height, const String& text,
                     uint16_t color, uint16_t background) {
  auto& display = M5Cardputer.Display;
  display.setFont(&fonts::efontJA_12);
  display.fillRect(x, y, width, height, background);
  display.setTextDatum(top_left);
  display.setTextColor(color, background);

  if (textWidth(text) <= width) {
    display.drawString(text, x, y);
    return;
  }

  marqueeActive = true;
  marqueeX = x;
  marqueeY = y;
  marqueeWidth = width;
  marqueeHeight = height;
  marqueeText = text;
  marqueeColor = color;
  marqueeBackground = background;
  drawMarqueeFrame();
}

void drawBatteryIndicator(int x, int y, int width, int height,
                          uint16_t foreground, uint16_t background) {
  auto& display = M5Cardputer.Display;
  display.fillRect(x, y, width, height, background);

  const int32_t level = M5Cardputer.Power.getBatteryLevel();
  const bool hasLevel = level >= 0 && level <= 100;
  const int iconX = x;
  const int iconY = y + 4;
  const int iconW = 15;
  const int iconH = 9;
  display.drawRect(iconX, iconY, iconW, iconH, foreground);
  display.fillRect(iconX + iconW, iconY + 3, 2, 3, foreground);
  if (hasLevel) {
    const int fillW = (iconW - 4) * level / 100;
    if (fillW > 0) {
      display.fillRect(iconX + 2, iconY + 2, fillW, iconH - 4, foreground);
    }
  }

  int labelX = iconX + iconW + 5;

  display.setFont(&fonts::efontJA_12);
  display.setTextDatum(top_left);
  display.setTextColor(foreground, background);
  const String label = hasLevel ? String(level) + "%" : "--%";
  display.drawString(label, labelX, y + 1);
}

String currentInputText() {
  if (committedKana.length() == 0 && pendingRomaji.length() == 0) {
    return "";
  }
  return committedKana + pendingRomaji;
}

void clearSearchResults() {
  searched = false;
  resultCount = 0;
  selectedResult = 0;
  definitionScrollLine = 0;
  viewMode = ViewMode::Results;
}

void composePending(bool final = false) {
  if (pendingRomaji.length() == 0) {
    return;
  }

  const jpdict::RomajiComposition composed =
      jpdict::composeRomaji(pendingRomaji.c_str(), final);
  if (!composed.committed.empty()) {
    committedKana += composed.committed.c_str();
  }
  pendingRomaji = composed.pending.c_str();
}

void removeLastUtf8Char(String& text) {
  if (text.length() == 0) {
    return;
  }
  int pos = text.length() - 1;
  while (pos > 0 && (static_cast<uint8_t>(text[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  text.remove(pos);
}

uint32_t utf8CodepointAt(const String& text, int pos) {
  if (pos >= static_cast<int>(text.length())) {
    return 0;
  }
  const auto* data = reinterpret_cast<const uint8_t*>(text.c_str() + pos);
  const uint8_t first = data[0];
  if (first < 0x80) {
    return first;
  }
  if ((first & 0xE0) == 0xC0) {
    return ((first & 0x1F) << 6) | (data[1] & 0x3F);
  }
  if ((first & 0xF0) == 0xE0) {
    return ((first & 0x0F) << 12) | ((data[1] & 0x3F) << 6) |
           (data[2] & 0x3F);
  }
  if ((first & 0xF8) == 0xF0) {
    return ((first & 0x07) << 18) | ((data[1] & 0x3F) << 12) |
           ((data[2] & 0x3F) << 6) | (data[3] & 0x3F);
  }
  return 0;
}

bool isKanaCodepoint(uint32_t cp) {
  return (cp >= 0x3041 && cp <= 0x3096) || cp == 0x30FC;
}

String trailingKanaSegment() {
  int start = committedKana.length();
  while (start > 0) {
    int charStart = start - 1;
    while (charStart > 0 &&
           (static_cast<uint8_t>(committedKana[charStart]) & 0xC0) == 0x80) {
      --charStart;
    }
    if (!isKanaCodepoint(utf8CodepointAt(committedKana, charStart))) {
      break;
    }
    start = charStart;
  }
  return committedKana.substring(start);
}

void replaceTrailingKanaSegment(const String& replacement) {
  int start = committedKana.length();
  while (start > 0) {
    int charStart = start - 1;
    while (charStart > 0 &&
           (static_cast<uint8_t>(committedKana[charStart]) & 0xC0) == 0x80) {
      --charStart;
    }
    if (!isKanaCodepoint(utf8CodepointAt(committedKana, charStart))) {
      break;
    }
    start = charStart;
  }
  committedKana.remove(start);
  committedKana += replacement;
}

int bodyLineHeight(bool largeText) {
  return largeText ? kLargeBodyLineHeight : kSmallBodyLineHeight;
}

int drawWrappedText(int x, int y, int width, int maxHeight, const String &text,
                    uint16_t color, uint16_t background, int skipLines = 0,
                    bool largeText = false) {
  auto &display = M5Cardputer.Display;
  display.setFont(largeText ? &fonts::efontJA_16 : &fonts::efontJA_12);
  display.setTextDatum(top_left);
  display.setTextColor(color, background);

  const int lineHeight = bodyLineHeight(largeText);
  int lineY = y;
  int pos = 0;
  int wrappedLine = 0;

  while (pos < static_cast<int>(text.length()) && lineY + lineHeight <= y + maxHeight) {
    while (pos < static_cast<int>(text.length()) && text[pos] == ' ') {
      ++pos;
    }

    String line;
    String word;
    while (pos < static_cast<int>(text.length())) {
      const char ch = text[pos++];
      if (ch == ' ') {
        if (word.length() > 0) {
          const String candidate = line.length() > 0 ? line + " " + word : word;
          if (textWidth(candidate) > width && line.length() > 0) {
            --pos;
            break;
          }
          line = candidate;
          word = "";
        }
        continue;
      }
      word += ch;

      if (textWidth(word) > width && line.length() == 0) {
        while (word.length() > 0 && textWidth(word) > width) {
          word.remove(word.length() - 1);
          --pos;
        }
        line = word;
        word = "";
        break;
      }
    }

    if (word.length() > 0) {
      const String candidate = line.length() > 0 ? line + " " + word : word;
      if (textWidth(candidate) <= width || line.length() == 0) {
        line = candidate;
      } else {
        pos -= word.length();
      }
    }

    if (line.length() == 0) {
      break;
    }

    if (wrappedLine >= skipLines) {
      display.fillRect(x, lineY, width, lineHeight, background);
      display.drawString(line, x, lineY);
      lineY += lineHeight;
    }
    ++wrappedLine;
  }

  return lineY;
}

int countWrappedLines(int width, const String &text, bool largeText = false) {
  auto &display = M5Cardputer.Display;
  display.setFont(largeText ? &fonts::efontJA_16 : &fonts::efontJA_12);

  int pos = 0;
  int wrappedLine = 0;

  while (pos < static_cast<int>(text.length())) {
    while (pos < static_cast<int>(text.length()) && text[pos] == ' ') {
      ++pos;
    }

    String line;
    String word;
    while (pos < static_cast<int>(text.length())) {
      const char ch = text[pos++];
      if (ch == ' ') {
        if (word.length() > 0) {
          const String candidate = line.length() > 0 ? line + " " + word : word;
          if (textWidth(candidate) > width && line.length() > 0) {
            --pos;
            break;
          }
          line = candidate;
          word = "";
        }
        continue;
      }
      word += ch;

      if (textWidth(word) > width && line.length() == 0) {
        while (word.length() > 0 && textWidth(word) > width) {
          word.remove(word.length() - 1);
          --pos;
        }
        line = word;
        word = "";
        break;
      }
    }

    if (word.length() > 0) {
      const String candidate = line.length() > 0 ? line + " " + word : word;
      if (textWidth(candidate) <= width || line.length() == 0) {
        line = candidate;
      } else {
        pos -= word.length();
      }
    }

    if (line.length() == 0) {
      break;
    }
    ++wrappedLine;
  }

  return wrappedLine;
}

String cleanDefinitionForDisplay(String text) {
  text.replace("forms; ", "");
  text.replace("; forms; ", "; ");
  text.replace("; forms", "");
  return text;
}

String trimDefinitionItem(String item) {
  item.trim();
  return item;
}

bool hasVisibleDefinitionText(const String& text) {
  for (size_t i = 0; i < text.length(); ++i) {
    const char ch = text[i];
    if (ch != ' ' && ch != '-' && ch != '(' && ch != ')' && ch != '[' &&
        ch != ']' && ch != '/' && ch != '\\' && ch != ',' && ch != ';' &&
        ch != ':') {
      return true;
    }
  }
  return false;
}

bool isDefinitionAttribute(const String& item) {
  static constexpr const char* kAttributes[] = {
      "1-dan",       "1-dan (spec.)", "2-dan",       "4-dan",
      "5-dan",       "5-dan (irreg.)", "5-dan (spec.)", "abbr.",
      "adjectival",  "adjective",     "adverb",      "anatomy",
      "archaic",     "architecture",  "art",         "astronomy",
      "aux-adj",     "aux-verb",      "auxiliary",   "biology",
      "botany",      "business",      "chemistry",   "childish",
      "colloquial",  "computing",     "conjunction", "copula",
      "counter",     "dated",         "derogatory",  "economics",
      "engineering", "euphemism",     "exp",         "familiar",
      "feminine",    "finance",       "food",        "formal",
      "geography",   "grammar",       "historical",  "honorific",
      "humble",      "i-adjective",   "idiom",       "interjection",
      "intransitive", "jocular",      "linguistics", "manga",
      "masculine",   "math",          "medical",     "military",
      "mimetic",     "na-adj",        "net slang",   "no-adj",
      "noun",        "numeric",       "obsolete",    "particle",
      "person",      "place",         "poetical",    "polite",
      "prefix",      "pronoun",       "proverb",     "rare",
      "sensitive",   "slang",         "sports",      "suffix",
      "suru verb",   "to-adverb",     "trademark",   "transitive",
      "unclass",     "usually kana",  "verb",        "vulgar",
      "wasei",       "work",
  };

  for (const char* attribute : kAttributes) {
    if (item == attribute) {
      return true;
    }
  }
  return false;
}

bool attributesContain(const String& attributes, const String& item) {
  int start = 0;
  while (start < static_cast<int>(attributes.length())) {
    int end = attributes.indexOf(", ", start);
    if (end < 0) {
      end = attributes.length();
    }
    if (attributes.substring(start, end) == item) {
      return true;
    }
    start = end + 2;
  }
  return false;
}

void appendAttribute(String& attributes, const String& item) {
  if (item.length() == 0 || attributesContain(attributes, item)) {
    return;
  }
  if (attributes.length() > 0) {
    attributes += ", ";
  }
  attributes += item;
}

bool isNoiseDefinitionItem(const String& item) {
  return item.length() == 0 || item == "forms" || !hasVisibleDefinitionText(item);
}

struct ParsedDefinition {
  String attributes;
  String glosses;
};

ParsedDefinition parseDefinition(const String& rawDefinition) {
  ParsedDefinition parsed;
  const String definition = cleanDefinitionForDisplay(rawDefinition);
  bool seenGloss = false;
  int start = 0;

  while (start < static_cast<int>(definition.length())) {
    int end = definition.indexOf("; ", start);
    if (end < 0) {
      end = definition.length();
    }

    const String item = trimDefinitionItem(definition.substring(start, end));
    if (!isNoiseDefinitionItem(item)) {
      if (isDefinitionAttribute(item)) {
        appendAttribute(parsed.attributes, item);
      } else {
        seenGloss = true;
        if (parsed.glosses.length() > 0) {
          parsed.glosses += " ";
        }
        parsed.glosses += "- ";
        parsed.glosses += item;
      }
    }

    if (end >= static_cast<int>(definition.length())) {
      break;
    }
    start = end + 2;
  }

  if (!seenGloss && parsed.attributes.length() > 0) {
    parsed.glosses = parsed.attributes;
    parsed.attributes = "";
  }
  return parsed;
}

int maxDefinitionScrollLine() {
  if (!searched || resultCount == 0 || selectedResult >= resultCount) {
    return 0;
  }

  auto &display = M5Cardputer.Display;
  const int top = kCompactContentTop;
  constexpr int footerTop = kFooterTop;
  constexpr int pad = 5;
  constexpr bool largeDefinitionText = true;
  const int lineHeight = bodyLineHeight(largeDefinitionText);

  const ParsedDefinition definition =
      parseDefinition(results[selectedResult].definition);
  const int dividerY = top + 36;
  const int bodyY = dividerY + 5;
  const int bodyHeight = footerTop - bodyY - 2;
  const int visibleLines = bodyHeight > 0 ? bodyHeight / lineHeight : 0;
  const int wrappedLines =
      countWrappedLines(display.width() - pad * 2, definition.glosses,
                        largeDefinitionText);
  const int maxScroll = wrappedLines - visibleLines;
  return maxScroll > 0 ? maxScroll : 0;
}

void runSearch() {
  composePending(true);
  lastSearched = committedKana;
  lastSearchedKana = committedKana;
  selectedResult = 0;
  definitionScrollLine = 0;
  viewMode = ViewMode::Results;
  searched = true;

  if (committedKana.length() == 0 || !dictionary.isOpen()) {
    resultCount = 0;
  } else {
    resultCount =
        dictionary.lookupExactThenPrefix(lastSearchedKana, results, kMaxResults);
  }

  Serial.printf("search kana='%s' results=%u\n", lastSearchedKana.c_str(),
                static_cast<unsigned>(resultCount));
  dirty = true;
}

void selectPreviousResult() {
  if (viewMode == ViewMode::KanjiPicker) {
    if (selectedKanjiCandidate > 0 &&
        selectedKanjiCandidate % kKanjiGridColumns != 0) {
      --selectedKanjiCandidate;
      dirty = true;
    }
    return;
  }

  if (viewMode == ViewMode::Definition) {
    if (definitionScrollLine > 0) {
      --definitionScrollLine;
      dirty = true;
    }
    return;
  }

  if (resultCount == 0 || selectedResult == 0) {
    return;
  }
  --selectedResult;
  definitionScrollLine = 0;
  dirty = true;
}

void selectNextResult() {
  if (viewMode == ViewMode::KanjiPicker) {
    if (selectedKanjiCandidate + 1 < kanjiCandidateCount &&
        selectedKanjiCandidate % kKanjiGridColumns != kKanjiGridColumns - 1) {
      ++selectedKanjiCandidate;
      dirty = true;
    }
    return;
  }

  if (viewMode == ViewMode::Definition) {
    const int maxScroll = maxDefinitionScrollLine();
    if (definitionScrollLine < maxScroll) {
      ++definitionScrollLine;
      dirty = true;
    }
    return;
  }

  if (resultCount == 0 || selectedResult + 1 >= resultCount) {
    return;
  }
  ++selectedResult;
  definitionScrollLine = 0;
  dirty = true;
}

void selectUp() {
  if (viewMode == ViewMode::KanjiPicker) {
    if (selectedKanjiCandidate >= kKanjiGridColumns) {
      selectedKanjiCandidate -= kKanjiGridColumns;
      dirty = true;
    }
    return;
  }
  selectPreviousResult();
}

void selectDown() {
  if (viewMode == ViewMode::KanjiPicker) {
    if (selectedKanjiCandidate + kKanjiGridColumns < kanjiCandidateCount) {
      selectedKanjiCandidate += kKanjiGridColumns;
      dirty = true;
    }
    return;
  }
  selectNextResult();
}

void clearQuery() {
  committedKana = "";
  pendingRomaji = "";
  clearSearchResults();
  dirty = true;
}

void openKanjiPicker() {
  composePending(true);
  kanjiReading = trailingKanaSegment();
  selectedKanjiCandidate = 0;
  kanjiCandidateCount = 0;
  openKanjiIndex("picker");
  Serial.printf("kanji picker reading='%s' open=%u\n", kanjiReading.c_str(),
                kanjiIndex.isOpen());
  if (kanjiReading.length() > 0 && kanjiIndex.isOpen()) {
    kanjiCandidateCount =
        kanjiIndex.lookup(kanjiReading, kanjiCandidates, kMaxKanjiCandidates);
  }
  Serial.printf("kanji picker candidates=%u\n",
                static_cast<unsigned>(kanjiCandidateCount));
  viewMode = ViewMode::KanjiPicker;
  dirty = true;
}

void closeKanjiPicker() {
  if (viewMode != ViewMode::KanjiPicker) {
    return;
  }
  viewMode = ViewMode::Results;
  dirty = true;
}

void insertSelectedKanji() {
  if (viewMode != ViewMode::KanjiPicker || kanjiCandidateCount == 0 ||
      selectedKanjiCandidate >= kanjiCandidateCount) {
    closeKanjiPicker();
    return;
  }
  replaceTrailingKanaSegment(kanjiCandidates[selectedKanjiCandidate]);
  clearSearchResults();
  viewMode = ViewMode::Results;
  dirty = true;
}

void appendChar(char ch) {
  if (committedKana.length() + pendingRomaji.length() >= kMaxQueryChars) {
    return;
  }
  if ((ch >= 'A' && ch <= 'Z')) {
    ch = static_cast<char>(ch - 'A' + 'a');
  }
  if ((ch >= 'a' && ch <= 'z') || ch == '-' || ch == '\'') {
    pendingRomaji += ch;
    composePending(false);
    clearSearchResults();
    dirty = true;
  }
}

void deleteChar() {
  if (pendingRomaji.length() > 0) {
    pendingRomaji.remove(pendingRomaji.length() - 1);
  } else if (committedKana.length() > 0) {
    removeLastUtf8Char(committedKana);
  } else {
    return;
  }
  clearSearchResults();
  dirty = true;
}

void drawHeader() {
  auto &display = M5Cardputer.Display;
  const int headerBottom = contentTop() - 2;
  display.fillRect(0, 0, display.width(), headerBottom, TFT_NAVY);
  display.setTextDatum(top_left);

  const String inputText =
      currentInputText().length() > 0 ? currentInputText() : String("type romaji...");
  const uint16_t inputColor =
      currentInputText().length() > 0 ? TFT_CYAN : TFT_LIGHTGREY;
  constexpr int batteryWidth = 42;
  const int inputWidth = display.width() - 15 - batteryWidth;
  drawBatteryIndicator(display.width() - batteryWidth - 2, 5, batteryWidth, 16,
                       TFT_WHITE, TFT_NAVY);
  display.setTextDatum(top_left);
  if (usesLargeSearchHeader()) {
    display.setFont(&fonts::efontJA_16);
    display.setTextColor(inputColor, TFT_NAVY);
    display.drawString(ellipsize(inputText, inputWidth), 5, 7);
  } else {
    display.setFont(&fonts::efontJA_12);
    drawTextLine(5, 5, inputWidth, inputText, inputColor, TFT_NAVY);
  }
}

void drawResults() {
  auto &display = M5Cardputer.Display;
  const int top = contentTop();
  constexpr int footerTop = kFooterTop;
  constexpr int pad = 5;

  display.fillRect(0, top, display.width(), footerTop - top, TFT_BLACK);

  if (!searched) {
    display.setTextDatum(top_left);
    display.setTextColor(startupStatusColor(), TFT_BLACK);
    display.drawString(ellipsize(startupStatusLine(), display.width() - pad * 2),
                       pad, top + 2);
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    if (storageState == StorageState::DictionaryOk) {
      display.drawString("Type romaji; kana commits live", pad, top + 18);
      display.drawString("Enter search; right kanji", pad, top + 33);
    } else {
      display.drawString(ellipsize(startupDetailLine(), display.width() - pad * 2),
                         pad, top + 18);
      display.drawString("Lookup disabled until fixed", pad, top + 33);
    }
    return;
  }

  if (resultCount == 0) {
    display.setTextDatum(middle_center);
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    if (committedKana.length() == 0 && pendingRomaji.length() == 0) {
      display.drawString("No query entered", display.width() / 2, 62);
    } else if (!dictionary.isOpen()) {
      display.drawString(startupStatusLine(), display.width() / 2, 62);
      display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      display.drawString(startupDetailLine(), display.width() / 2, 80);
    } else {
      display.drawString("No match", display.width() / 2, 62);
      display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      display.drawString(lastSearchedKana, display.width() / 2, 80);
    }
    return;
  }

  const JapaneseDictionaryMatch &result = results[selectedResult];
  const ParsedDefinition definition = parseDefinition(result.definition);
  display.setTextDatum(top_left);
  display.setFont(&fonts::efontJA_16);
  display.setTextColor(TFT_GREEN, TFT_BLACK);
  display.drawString(ellipsize(result.term, display.width() - pad * 2), pad,
                     top);
  display.setFont(&fonts::efontJA_12);

  String reading = String("[") + result.reading + "]";
  if (result.deinflectionDepth > 0 && result.sourceText.length() > 0) {
    reading += " < ";
    reading += result.sourceText;
  }
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  String metadata = reading;
  if (definition.attributes.length() > 0) {
    metadata += " (";
    metadata += definition.attributes;
    metadata += ")";
  }
  drawMarqueeText(pad, top + 21, display.width() - pad * 2, 15, metadata,
                  TFT_CYAN, TFT_BLACK);

  int dividerY = top + 36;
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.drawFastHLine(pad, dividerY, display.width() - pad * 2,
                        TFT_DARKGREY);

  const int bodyY = dividerY + 5;
  drawWrappedText(pad, bodyY, display.width() - pad * 2,
                  footerTop - bodyY - 2, definition.glosses, TFT_WHITE,
                  TFT_BLACK);
}

void drawDefinition() {
  auto &display = M5Cardputer.Display;
  const int top = contentTop();
  constexpr int footerTop = kFooterTop;
  constexpr int pad = 5;

  display.fillRect(0, top, display.width(), footerTop - top, TFT_BLACK);

  if (!searched || resultCount == 0) {
    viewMode = ViewMode::Results;
    drawResults();
    return;
  }

  const JapaneseDictionaryMatch &result = results[selectedResult];
  const ParsedDefinition definition = parseDefinition(result.definition);
  display.setTextDatum(top_left);
  display.setFont(&fonts::efontJA_16);
  display.setTextColor(TFT_GREEN, TFT_BLACK);
  display.drawString(ellipsize(result.term, display.width() - pad * 2), pad,
                     top);
  display.setFont(&fonts::efontJA_12);
  String metadata = String("[") + result.reading + "]";
  if (definition.attributes.length() > 0) {
    metadata += " (";
    metadata += definition.attributes;
    metadata += ")";
  }
  drawMarqueeText(pad, top + 21, display.width() - pad * 2, 15, metadata,
                  TFT_CYAN, TFT_BLACK);
  int dividerY = top + 36;
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.drawFastHLine(pad, dividerY, display.width() - pad * 2,
                        TFT_DARKGREY);

  const int bodyY = dividerY + 5;
  drawWrappedText(pad, bodyY, display.width() - pad * 2,
                  footerTop - bodyY - 2, definition.glosses, TFT_WHITE,
                  TFT_BLACK, definitionScrollLine, true);
}

void drawKanjiPicker() {
  auto &display = M5Cardputer.Display;
  const int top = contentTop();
  constexpr int footerTop = kFooterTop;
  constexpr int pad = 5;
  display.fillRect(0, top, display.width(), footerTop - top, TFT_BLACK);
  display.setTextDatum(top_left);

  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawString(ellipsize(String("Kanji: ") + kanjiReading,
                               display.width() - pad * 2),
                     pad, top + 2);

  if (!kanjiIndex.isOpen()) {
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    if (kanjiIndexStatus == "missing /kanji files") {
      display.drawString("Kanji index missing", pad, top + 22);
      display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      display.drawString("Expected /kanji", pad, top + 38);
    } else if (kanjiIndexStatus == "open failed /kanji") {
      display.drawString("Kanji index open failed", pad, top + 22);
      display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      display.drawString("Check /kanji files", pad, top + 38);
    } else {
      display.drawString(ellipsize(kanjiIndexStatus, display.width() - pad * 2),
                         pad, top + 22);
    }
    return;
  }
  if (kanjiReading.length() == 0) {
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.drawString("No trailing kana", pad, top + 22);
    return;
  }
  if (kanjiCandidateCount == 0) {
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.drawString("No kanji candidates", pad, top + 22);
    return;
  }

  display.setFont(&fonts::efontJA_16);
  const size_t pageStart =
      (selectedKanjiCandidate / kVisibleKanjiCandidates) * kVisibleKanjiCandidates;
  const size_t requestedPageEnd = pageStart + kVisibleKanjiCandidates;
  const size_t pageEnd = requestedPageEnd < kanjiCandidateCount
                             ? requestedPageEnd
                             : kanjiCandidateCount;
  int x = pad;
  int y = top + 24;
  for (size_t i = pageStart; i < pageEnd; ++i) {
    const bool selected = i == selectedKanjiCandidate;
    const uint16_t bg = selected ? TFT_DARKGREY : TFT_BLACK;
    const uint16_t fg = selected ? TFT_YELLOW : TFT_WHITE;
    const int cellW = (display.width() - pad * 2) / kKanjiGridColumns;
    display.fillRect(x - 1, y - 1, cellW, 22, bg);
    display.setTextColor(fg, bg);
    display.drawString(kanjiCandidates[i], x + 3, y);
    x += cellW;
    if ((i - pageStart + 1) % kKanjiGridColumns == 0) {
      x = pad;
      y += 24;
    }
  }
  display.setFont(&fonts::efontJA_12);
}

void drawFooter() {
  auto &display = M5Cardputer.Display;
  constexpr int footerTop = kFooterTop;
  display.fillRect(0, footerTop, display.width(), display.height() - footerTop, TFT_DARKGREY);
  display.setTextDatum(top_left);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY);

  String left = "Enter search";
  String right = "/ kanji";
  if (viewMode == ViewMode::KanjiPicker) {
    left = kanjiCandidateCount > 0 ? String(selectedKanjiCandidate + 1) + "/" +
                                         kanjiCandidateCount + "  arrows nav"
                                   : "Kanji picker";
    right = "Enter insert";
  } else if (viewMode == ViewMode::Definition) {
    left = String("Scroll ") + (definitionScrollLine + 1) + "  arrows";
    right = "left back";
  } else if (searched && resultCount > 0) {
    left = String(selectedResult + 1) + "/" + resultCount + "  arrows nav";
    right = "right open";
  } else {
    right = "right kanji";
  }
  display.drawString(left, 4, footerTop + 3);
  display.setTextDatum(top_right);
  display.drawString(right, display.width() - 4, footerTop + 3);
}

void drawApp() {
  auto &display = M5Cardputer.Display;
  marqueeActive = false;
  display.setFont(&fonts::efontJA_12);
  display.setTextSize(1);
  display.setTextWrap(false);
  drawHeader();
  if (viewMode == ViewMode::Definition) {
    drawDefinition();
  } else if (viewMode == ViewMode::KanjiPicker) {
    drawKanjiPicker();
  } else {
    drawResults();
  }
  drawFooter();
  dirty = false;
}

void openDefinition() {
  if (!searched || resultCount == 0) {
    runSearch();
    return;
  }
  viewMode = ViewMode::Definition;
  definitionScrollLine = 0;
  dirty = true;
}

void leaveDefinition() {
  if (viewMode != ViewMode::Definition) {
    return;
  }
  viewMode = ViewMode::Results;
  dirty = true;
}

void handleKeyboard() {
  auto &keyboard = M5Cardputer.Keyboard;
  if (!keyboard.isChange() || !keyboard.isPressed()) {
    return;
  }

  auto &keys = keyboard.keysState();
  if (keys.del) {
    if (viewMode == ViewMode::Definition) {
      leaveDefinition();
      return;
    }
    if (viewMode == ViewMode::KanjiPicker) {
      closeKanjiPicker();
      return;
    }
    deleteChar();
    return;
  }
  if (keys.enter) {
    if (viewMode == ViewMode::KanjiPicker) {
      insertSelectedKanji();
      return;
    }
    openDefinition();
    return;
  }
  if (keys.tab) {
    if (viewMode == ViewMode::Definition) {
      leaveDefinition();
      return;
    }
    if (viewMode == ViewMode::KanjiPicker) {
      closeKanjiPicker();
      return;
    }
    clearQuery();
    return;
  }

  if (viewMode == ViewMode::Definition) {
    for (const char ch : keys.word) {
      if (ch == ';') {
        selectUp();
      } else if (ch == '.' || ch == ' ') {
        selectDown();
      } else if (ch == ',') {
        leaveDefinition();
      }
    }
    return;
  }

  if (viewMode == ViewMode::KanjiPicker) {
    for (const char ch : keys.word) {
      if (ch == ';') {
        selectUp();
      } else if (ch == '.' || ch == ' ') {
        selectDown();
      } else if (ch == ',') {
        selectPreviousResult();
      } else if (ch == '/') {
        selectNextResult();
      }
    }
    return;
  }

  for (const char ch : keys.word) {
    if (ch == ';') {
      selectUp();
    } else if (ch == '.' || ch == ' ') {
      selectDown();
    } else if (ch == '/') {
      if (searched && resultCount > 0) {
        openDefinition();
      } else {
        openKanjiPicker();
      }
    } else if (ch == ',') {
      // Left arrow is currently only meaningful in detail/picker views.
    } else {
      appendChar(ch);
    }
  }
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  Serial.begin(115200);
  delay(100);

  auto &display = M5Cardputer.Display;
  display.setRotation(1);
  display.setBrightness(128);
  display.clear(TFT_BLACK);

  Serial.println("cardputer-jpdict interactive UI shell");
  probeStorage();
  drawApp();
}

void loop() {
  M5Cardputer.update();
  handleKeyboard();

  const uint32_t now = millis();
  if (dirty) {
    drawApp();
  }

  if (marqueeActive && now - lastMarqueeFrame >= kMarqueeFrameMs) {
    lastMarqueeFrame = now;
    drawMarqueeFrame();
  }
}
