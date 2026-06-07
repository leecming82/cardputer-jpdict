#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include "JapaneseDictionary.h"
#include "KanjiIndex.h"
#include "M5Cardputer.h"
#include "PopupHeadwordFont.h"
#include "RomajiKana.h"

namespace {
constexpr size_t kMaxQueryChars = 32;
constexpr int kSdSckPin = 40;
constexpr int kSdMisoPin = 39;
constexpr int kSdMosiPin = 14;
constexpr int kSdCsPin = 12;
constexpr int kSdMaxOpenFiles = 12;

constexpr const char* kDictionaryPaths[] = {
    "/jpdict",
    "/dict/jitendex-cpdict-ranked-japanese",
    "/dict/jitendex-cpdict-ranked",
    "/dict/jitendex-cpdict-modern",
};

constexpr size_t kMaxResults = 12;
constexpr size_t kMaxNormalResults = 6;
constexpr size_t kSearchHistorySize = 8;
constexpr size_t kMaxKanjiCandidates = 160;
constexpr size_t kMaxRadicalComponents = 24;
constexpr size_t kMaxSelectedRadicals = 6;
constexpr size_t kVisibleKanjiCandidates = 24;
constexpr size_t kKanjiGridColumns = 6;
constexpr uint32_t kMarqueeFrameMs = 240;
constexpr uint32_t kMarqueePageHoldMs = 1800;
constexpr uint32_t kMarqueeLoopPauseMs = 2400;
constexpr uint32_t kMarqueeAnimateAfterActivityMs = 10000;
constexpr uint32_t kIdleDelayMs = 40;
constexpr uint32_t kBacklightDimAfterMs = 15000;
constexpr uint32_t kDisplaySleepAfterMs = 60000;
constexpr uint32_t kBatteryRefreshMs = 60000;
constexpr uint8_t kBacklightNormal = 96;
constexpr uint8_t kBacklightDim = 8;
constexpr uint8_t kBacklightOff = 0;
constexpr uint32_t kIdleCpuFrequencyMhz = 80;
constexpr uint32_t kFallbackNormalCpuFrequencyMhz = 240;
constexpr int kBatteryAdcPin = 10;
constexpr float kBatteryAdcMultiplier = 2.0f;
constexpr float kBatteryMinMillivolts = 3300.0f;
constexpr float kBatteryMaxMillivolts = 4150.0f;

String committedKana;
String pendingRomaji;
String lastSearched;
String lastSearchedKana;
size_t selectedResult = 0;
size_t resultCount = 0;
JapaneseDictionary dictionary;
JapaneseDictionaryMatch results[kMaxResults];
String resultPreviewLines[kMaxResults];
String searchHistory[kSearchHistorySize];
size_t searchHistoryCount = 0;
int searchHistoryIndex = -1;
KanjiIndex kanjiIndex;
String kanjiCandidates[kMaxKanjiCandidates];
String kanjiCandidateScratch[kMaxKanjiCandidates];
String radicalComponents[kMaxRadicalComponents];
size_t selectedKanjiCandidate = 0;
size_t selectedRadicalCandidate = 0;
size_t kanjiCandidateCount = 0;
size_t radicalComponentCount = 0;
String kanjiReading;
String kanjiSearchCommitted;
String kanjiSearchPending;
String kanjiSearchDigits;
String selectedRadicalComponents[kMaxSelectedRadicals];
size_t selectedRadicalCount = 0;
String kanjiSourceSegment;
int kanjiSourceStart = 0;
int kanjiSpanOffset = 0;
int kanjiReplaceStart = 0;
int kanjiReplaceLength = 0;
bool searched = false;
bool dirty = true;
bool marqueeActive = false;
bool helpVisible = false;
bool headwordPopupVisible = false;
bool segmentedSearch = false;
uint32_t lastMarqueeFrame = 0;
uint32_t lastInputAt = 0;
bool backlightDimmed = false;
bool backlightOff = false;
bool displaySleeping = false;
bool idleCpuReduced = false;
uint32_t normalCpuFrequencyMhz = 0;
bool batteryCacheValid = false;
uint32_t lastBatteryReadAt = 0;
int cachedBatteryLevel = -1;
int marqueeX = 0;
int marqueeY = 0;
int marqueeWidth = 0;
int marqueeHeight = 0;
String marqueeText;
uint16_t marqueeColor = TFT_WHITE;
uint16_t marqueeBackground = TFT_BLACK;
bool marqueeLargeText = false;
uint32_t marqueeStartedAt = 0;
int marqueeLastPage = -1;
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
  KanjiSearch,
  KanjiSpanPicker,
  KanjiPicker,
};

enum class KanjiSearchPane {
  Parts,
  Kanji,
};

StorageState storageState = StorageState::SdChecking;
ViewMode viewMode = ViewMode::Results;
KanjiSearchPane kanjiSearchPane = KanjiSearchPane::Parts;
int definitionScrollLine = 0;

constexpr int kCompactContentTop = 26;
constexpr int kLargeContentTop = 43;
constexpr int kSmallBodyLineHeight = 15;
constexpr int kLargeBodyLineHeight = 20;
constexpr int kPopupHeadwordLineHeight = popup_headword_font::kLineHeight + 2;

String currentInputText();
void clearSearchResults();
void removeLastUtf8Char(String& text);
int previousUtf8CharStart(const String& text, int pos);
int nextUtf8CharEnd(const String& text, int pos);
void drawResults();
void leaveDefinition();

bool usesLargeSearchHeader() {
  return viewMode == ViewMode::Results && !searched &&
         currentInputText().length() > 0;
}

int contentTop() {
  return usesLargeSearchHeader() ? kLargeContentTop : kCompactContentTop;
}

int contentBottom() {
  return M5Cardputer.Display.height();
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
      if (kanjiIndexStatus == "OK /kanji") {
        return "SD OK  Dict OK  Kanji OK";
      }
      return String("SD OK  Dict OK  ") + kanjiIndexStatus;
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
  return storageState == StorageState::DictionaryOk &&
                 kanjiIndexStatus == "OK /kanji"
             ? TFT_GREEN
             : TFT_ORANGE;
}

bool hasKanjiIndexFiles(const char* basePath) {
  return SD.exists(joinPath(basePath, "manifest.json")) &&
         SD.exists(joinPath(basePath, "lookup.records.bin")) &&
         SD.exists(joinPath(basePath, "lookup.strings.bin"));
}

bool hasRadicalIndexFiles(const char* basePath) {
  return hasKanjiIndexFiles(basePath);
}

bool logKanjiIndexFiles(const char* basePath) {
  const bool hasManifest = SD.exists(joinPath(basePath, "manifest.json"));
  const bool hasRecords = SD.exists(joinPath(basePath, "lookup.records.bin"));
  const bool hasStrings = SD.exists(joinPath(basePath, "lookup.strings.bin"));
  const bool hasRadicals = hasRadicalIndexFiles(basePath);
  Serial.printf(
      "Kanji index files at %s: manifest=%u lookup_records=%u lookup_strings=%u radicals=%u\n",
      basePath, hasManifest, hasRecords, hasStrings, hasRadicals);
  return hasManifest && hasRecords && hasStrings;
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
    Serial.printf("Kanji index opened: /kanji radicals=%u\n",
                  kanjiIndex.hasRadicalIndex());
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
  if (!SD.begin(kSdCsPin, SPI, 25000000, "/sd", kSdMaxOpenFiles)) {
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
    removeLastUtf8Char(out);
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

int marqueePageEnd(const String& text, int start, int width) {
  int end = start;
  int lastFit = start;
  while (end < static_cast<int>(text.length())) {
    const int next = nextUtf8CharEnd(text, end);
    const String candidate = text.substring(start, next);
    if (textWidth(candidate) > width) {
      return lastFit > start ? lastFit : next;
    }
    lastFit = next;
    end = next;
  }
  return text.length();
}

int marqueePageCount(const String& text, int width) {
  int count = 0;
  int start = 0;
  while (start < static_cast<int>(text.length())) {
    ++count;
    const int next = marqueePageEnd(text, start, width);
    if (next <= start) {
      break;
    }
    start = next;
  }
  return count > 0 ? count : 1;
}

int marqueePageIndexForText(const String& text, int width) {
  const int pageCount = marqueePageCount(text, width);
  const uint32_t cycleMs =
      pageCount * kMarqueePageHoldMs + kMarqueeLoopPauseMs;
  const uint32_t elapsed = millis() - marqueeStartedAt;
  uint32_t page = (elapsed % cycleMs) / kMarqueePageHoldMs;
  if (page >= static_cast<uint32_t>(pageCount)) {
    page = pageCount - 1;
  }
  return static_cast<int>(page);
}

String marqueePageForText(const String& text, int width, int page) {
  int start = 0;
  for (int i = 0; i < page; ++i) {
    start = marqueePageEnd(text, start, width);
  }
  const int end = marqueePageEnd(text, start, width);
  return text.substring(start, end);
}

bool shouldAnimateMarquee(uint32_t now) {
  return !backlightDimmed && !backlightOff &&
         now - lastInputAt <= kMarqueeAnimateAfterActivityMs;
}

void drawMarqueeFrame(bool force = false) {
  auto& display = M5Cardputer.Display;
  if (!marqueeActive) {
    return;
  }

  const int page = marqueePageIndexForText(marqueeText, marqueeWidth);
  if (!force && page == marqueeLastPage) {
    return;
  }
  marqueeLastPage = page;

  display.setFont(marqueeLargeText ? &fonts::efontJA_16 : &fonts::efontJA_12);
  display.fillRect(marqueeX, marqueeY, marqueeWidth, marqueeHeight,
                   marqueeBackground);
  display.setTextDatum(top_left);
  display.setTextColor(marqueeColor, marqueeBackground);
  display.setClipRect(marqueeX, marqueeY, marqueeWidth, marqueeHeight);
  display.drawString(marqueePageForText(marqueeText, marqueeWidth, page),
                     marqueeX, marqueeY);
  display.clearClipRect();
}

void drawMarqueeText(int x, int y, int width, int height, const String& text,
                     uint16_t color, uint16_t background,
                     bool largeText = false, bool allowMarquee = true) {
  auto& display = M5Cardputer.Display;
  display.setFont(largeText ? &fonts::efontJA_16 : &fonts::efontJA_12);
  display.fillRect(x, y, width, height, background);
  display.setTextDatum(top_left);
  display.setTextColor(color, background);

  if (!allowMarquee || textWidth(text) <= width ||
      !shouldAnimateMarquee(millis())) {
    display.drawString(ellipsize(text, width), x, y);
    return;
  }

  const bool marqueeChanged =
      !marqueeActive || marqueeX != x || marqueeY != y ||
      marqueeWidth != width || marqueeHeight != height ||
      marqueeText != text || marqueeLargeText != largeText;
  if (marqueeChanged) {
    marqueeStartedAt = millis();
    marqueeLastPage = -1;
  }
  marqueeActive = true;
  marqueeX = x;
  marqueeY = y;
  marqueeWidth = width;
  marqueeHeight = height;
  marqueeText = text;
  marqueeColor = color;
  marqueeBackground = background;
  marqueeLargeText = largeText;
  drawMarqueeFrame(true);
}

void drawMetadataLine(int x, int y, int width, const String& readingLabel,
                      const String& tagText, bool allowMarquee) {
  auto& display = M5Cardputer.Display;
  display.setFont(&fonts::efontJA_12);
  display.fillRect(x, y, width, 15, TFT_BLACK);
  display.setTextDatum(top_left);
  display.setTextColor(TFT_CYAN, TFT_BLACK);

  if (tagText.length() == 0) {
    display.drawString(ellipsize(readingLabel, width), x, y);
    return;
  }

  constexpr int gap = 5;
  constexpr int minTagWidth = 44;
  int readingWidth = textWidth(readingLabel);
  const int maxReadingWidth = width - gap - minTagWidth;
  if (maxReadingWidth > 0 && readingWidth > maxReadingWidth) {
    const String visibleReading = ellipsize(readingLabel, maxReadingWidth);
    display.drawString(visibleReading, x, y);
    readingWidth = textWidth(visibleReading);
  } else {
    display.drawString(readingLabel, x, y);
  }

  const int tagX = x + readingWidth + gap;
  const int tagWidth = width - readingWidth - gap;
  if (tagWidth <= 0) {
    return;
  }
  drawMarqueeText(tagX, y, tagWidth, 15, tagText, TFT_CYAN, TFT_BLACK, false,
                  allowMarquee);
}

String headwordTail(const JapaneseDictionaryMatch& result) {
  if (result.terms.length() <= result.term.length() ||
      !result.terms.startsWith(result.term)) {
    return "";
  }
  String tail = result.terms.substring(result.term.length());
  if (tail.startsWith("・")) {
    tail = tail.substring(strlen("・"));
  }
  return tail;
}

void drawHeadwordLine(int x, int y, int width,
                      const JapaneseDictionaryMatch& result) {
  auto& display = M5Cardputer.Display;
  display.setFont(&fonts::efontJA_16);
  display.fillRect(x, y, width, 20, TFT_BLACK);
  display.setTextDatum(top_left);
  display.setTextColor(TFT_GREEN, TFT_BLACK);

  const String tail = headwordTail(result);
  if (tail.length() == 0) {
    display.drawString(ellipsize(result.term, width), x, y);
    return;
  }

  constexpr int gap = 4;
  constexpr int minTailWidth = 48;
  const String separator = "・";
  const int primaryWidth = textWidth(result.term);
  const int separatorWidth = textWidth(separator);
  const int fixedWidth = primaryWidth + gap + separatorWidth + gap;
  if (fixedWidth > width - minTailWidth) {
    display.drawString(ellipsize(result.term, width), x, y);
    return;
  }

  display.drawString(result.term, x, y);
  display.drawString(separator, x + primaryWidth + gap, y);
  const int tailX = x + fixedWidth;
  const int tailWidth = width - fixedWidth;
  if (tailWidth <= 0) {
    return;
  }
  drawMarqueeText(tailX, y, tailWidth, 20, tail, TFT_GREEN, TFT_BLACK, true);
}

int readBatteryLevelFromAdc() {
  static bool adcInitialized = false;
  if (!adcInitialized) {
    pinMode(kBatteryAdcPin, INPUT);
    adcInitialized = true;
  }

  constexpr int samples = 4;
  uint32_t totalMillivolts = 0;
  for (int i = 0; i < samples; ++i) {
    totalMillivolts += analogReadMilliVolts(kBatteryAdcPin);
  }

  const float adcMillivolts = static_cast<float>(totalMillivolts) / samples;
  const float batteryMillivolts = adcMillivolts * kBatteryAdcMultiplier;
  if (batteryMillivolts < 2500.0f || batteryMillivolts > 5000.0f) {
    return -1;
  }

  const float percent =
      ((batteryMillivolts - kBatteryMinMillivolts) /
       (kBatteryMaxMillivolts - (kBatteryMinMillivolts + 50.0f))) *
      100.0f;
  if (percent <= 0.0f) {
    return 1;
  }
  if (percent >= 100.0f) {
    return 100;
  }
  return static_cast<int>(percent);
}

int readBatteryLevel() {
  const int adcLevel = readBatteryLevelFromAdc();
  if (adcLevel > 0 && adcLevel <= 100) {
    return adcLevel;
  }

  const int32_t apiLevel = M5Cardputer.Power.getBatteryLevel();
  if (apiLevel > 0 && apiLevel <= 100) {
    return static_cast<int>(apiLevel);
  }
  return -1;
}

int readCachedBatteryLevel() {
  const uint32_t now = millis();
  if (!batteryCacheValid || now - lastBatteryReadAt >= kBatteryRefreshMs) {
    cachedBatteryLevel = readBatteryLevel();
    lastBatteryReadAt = now;
    batteryCacheValid = true;
  }
  return cachedBatteryLevel;
}

int batterySegmentCount(int level) {
  if (level >= 85) {
    return 4;
  }
  if (level >= 60) {
    return 3;
  }
  if (level >= 35) {
    return 2;
  }
  if (level >= 15) {
    return 1;
  }
  return 0;
}

void drawBatteryIndicator(int x, int y, int width, int height,
                          uint16_t foreground, uint16_t background) {
  auto& display = M5Cardputer.Display;
  display.fillRect(x, y, width, height, background);

  const int32_t level = readCachedBatteryLevel();
  const bool hasLevel = level > 0 && level <= 100;
  const int iconW = 25;
  const int iconH = 11;
  const int iconX = x + (width - iconW - 2) / 2;
  const int iconY = y + (height - iconH) / 2;
  const uint16_t batteryColor =
      hasLevel && level < 15 ? TFT_RED
                             : hasLevel && level < 35 ? TFT_ORANGE
                                                       : foreground;

  display.drawRect(iconX, iconY, iconW, iconH, batteryColor);
  display.fillRect(iconX + iconW, iconY + 3, 2, 5, batteryColor);
  if (hasLevel) {
    constexpr int segmentCount = 4;
    constexpr int segmentGap = 1;
    const int segmentW =
        (iconW - 4 - (segmentCount - 1) * segmentGap) / segmentCount;
    const int bars = batterySegmentCount(level);
    for (int i = 0; i < bars; ++i) {
      const int segmentX = iconX + 2 + i * (segmentW + segmentGap);
      display.fillRect(segmentX, iconY + 2, segmentW, iconH - 4,
                       batteryColor);
    }
  } else {
    display.drawLine(iconX + 4, iconY + iconH - 3, iconX + iconW - 4,
                     iconY + 2, batteryColor);
  }
}

String currentInputText() {
  if (viewMode == ViewMode::KanjiSearch) {
    String selected = "Selected:";
    if (selectedRadicalCount == 0) {
      selected += " none";
    } else {
      for (size_t i = 0; i < selectedRadicalCount; ++i) {
        selected += " ";
        selected += selectedRadicalComponents[i];
      }
    }
    return selected;
  }
  if (committedKana.length() == 0 && pendingRomaji.length() == 0) {
    return "";
  }
  return committedKana + pendingRomaji;
}

void resetSearchHistoryRecall() {
  searchHistoryIndex = -1;
}

void rememberSearch(const String& query) {
  if (query.length() == 0) {
    return;
  }

  size_t existing = searchHistoryCount;
  for (size_t i = 0; i < searchHistoryCount; ++i) {
    if (searchHistory[i] == query) {
      existing = i;
      break;
    }
  }

  if (existing < searchHistoryCount) {
    for (size_t i = existing; i + 1 < searchHistoryCount; ++i) {
      searchHistory[i] = searchHistory[i + 1];
    }
    --searchHistoryCount;
  }

  const size_t limit = searchHistoryCount < kSearchHistorySize - 1
                           ? searchHistoryCount
                           : kSearchHistorySize - 1;
  for (size_t i = limit; i > 0; --i) {
    searchHistory[i] = searchHistory[i - 1];
  }
  searchHistory[0] = query;
  searchHistoryCount = searchHistoryCount < kSearchHistorySize
                           ? searchHistoryCount + 1
                           : kSearchHistorySize;
}

bool canRecallSearchHistory() {
  return viewMode == ViewMode::Results && !searched && searchHistoryCount > 0 &&
         pendingRomaji.length() == 0 &&
         (committedKana.length() == 0 || searchHistoryIndex >= 0);
}

bool recallSearchHistory(int direction) {
  if (!canRecallSearchHistory()) {
    return false;
  }

  if (searchHistoryIndex < 0) {
    searchHistoryIndex = direction < 0 ? 0 : searchHistoryCount - 1;
  } else if (direction < 0) {
    searchHistoryIndex =
        (searchHistoryIndex + 1) % static_cast<int>(searchHistoryCount);
  } else {
    searchHistoryIndex =
        (searchHistoryIndex + static_cast<int>(searchHistoryCount) - 1) %
        static_cast<int>(searchHistoryCount);
  }

  committedKana = searchHistory[searchHistoryIndex];
  pendingRomaji = "";
  clearSearchResults();
  dirty = true;
  return true;
}

void clearSearchResults() {
  searched = false;
  segmentedSearch = false;
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

void composeKanjiSearchText(String& committed, String& pending,
                            bool final = false) {
  if (pending.length() == 0) {
    return;
  }

  const jpdict::RomajiComposition composed =
      jpdict::composeRomaji(pending.c_str(), final);
  if (!composed.committed.empty()) {
    committed += composed.committed.c_str();
  }
  pending = composed.pending.c_str();
}

void composeKanjiSearchPending(bool final = false) {
  composeKanjiSearchText(kanjiSearchCommitted, kanjiSearchPending, final);
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

void appendUtf8(String& out, uint32_t cp) {
  if (cp <= 0x7F) {
    out += static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

String hiraganaToKatakana(const String& text) {
  String out;
  out.reserve(text.length());
  int pos = 0;
  while (pos < static_cast<int>(text.length())) {
    uint32_t cp = utf8CodepointAt(text, pos);
    if (cp >= 0x3041 && cp <= 0x3096) {
      cp += 0x60;
    } else if (cp == '-') {
      cp = 0x30FC;
    }
    appendUtf8(out, cp);
    pos = nextUtf8CharEnd(text, pos);
  }
  return out;
}

int32_t sequenceGroupId(int32_t sequence) {
  return sequence < 0 ? -sequence : sequence;
}

bool hasTermVariant(const String& terms, const String& term) {
  int start = 0;
  while (start <= static_cast<int>(terms.length())) {
    int end = terms.indexOf("・", start);
    if (end < 0) {
      end = terms.length();
    }
    if (terms.substring(start, end) == term) {
      return true;
    }
    if (end >= static_cast<int>(terms.length())) {
      break;
    }
    start = end + strlen("・");
  }
  return false;
}

void appendTermVariant(JapaneseDictionaryMatch& match, const String& term) {
  if (term.length() == 0 || hasTermVariant(match.terms, term)) {
    return;
  }
  if (match.terms.length() > 0) {
    match.terms += "・";
  }
  match.terms += term;
  if (match.termCount < UINT8_MAX) {
    ++match.termCount;
  }
}

bool mergeSearchResult(JapaneseDictionaryMatch* matches, size_t count,
                       const JapaneseDictionaryMatch& candidate) {
  const int32_t candidateGroup = sequenceGroupId(candidate.sequence);
  for (size_t i = 0; i < count; ++i) {
    if (sequenceGroupId(matches[i].sequence) == candidateGroup &&
        matches[i].reading == candidate.reading &&
        matches[i].definition == candidate.definition) {
      appendTermVariant(matches[i], candidate.term);
      return true;
    }
  }
  return false;
}

size_t appendSearchResults(JapaneseDictionaryMatch* outMatches, size_t found,
                           size_t maxMatches,
                           const JapaneseDictionaryMatch* candidates,
                           size_t candidateCount) {
  if (outMatches == nullptr || candidates == nullptr) {
    return found;
  }
  for (size_t i = 0; i < candidateCount && found < maxMatches; ++i) {
    if (!mergeSearchResult(outMatches, found, candidates[i])) {
      outMatches[found++] = candidates[i];
    }
  }
  return found;
}

bool rankedBefore(const JapaneseDictionaryMatch& a,
                  const JapaneseDictionaryMatch& b) {
  if (a.tier != b.tier) {
    return a.tier < b.tier;
  }
  if (a.score != b.score) {
    return a.score > b.score;
  }
  if (a.deinflectionDepth != b.deinflectionDepth) {
    return a.deinflectionDepth < b.deinflectionDepth;
  }
  if (a.flags != b.flags) {
    return a.flags < b.flags;
  }
  return a.term < b.term;
}

void sortSearchResults(JapaneseDictionaryMatch* matches, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    JapaneseDictionaryMatch value = matches[i];
    size_t j = i;
    while (j > 0 && rankedBefore(value, matches[j - 1])) {
      matches[j] = matches[j - 1];
      --j;
    }
    matches[j] = value;
  }
}

bool hasLikelyInflectionEnding(const String& query) {
  static constexpr const char* kEndings[] = {
      "ました", "ません", "ましたら", "ませんでした", "なかった", "かった",
      "くない", "くて",   "ければ",   "れば",         "られる",   "れる",
      "せる",   "させる", "ない",     "ます",         "たい",     "た",
      "て",     "だ",     "で",       "ば",           "ぬ",
  };
  for (const char* ending : kEndings) {
    if (query.endsWith(ending) && query.length() > strlen(ending)) {
      return true;
    }
  }
  return false;
}

size_t remainingSearchCapacity(size_t found) {
  return found < kMaxNormalResults ? kMaxNormalResults - found : 0;
}

size_t stagedSearchLimit(size_t found) {
  constexpr size_t kMergeSlack = 2;
  const size_t remaining = remainingSearchCapacity(found);
  if (remaining == 0) {
    return 0;
  }
  const size_t limit = remaining + kMergeSlack;
  return limit < kMaxResults ? limit : kMaxResults;
}

bool isKanaCodepoint(uint32_t cp) {
  return (cp >= 0x3041 && cp <= 0x3096) || cp == 0x30FC;
}

bool isPreviewKanaCodepoint(uint32_t cp) {
  return (cp >= 0x3041 && cp <= 0x3096) ||
         (cp >= 0x309D && cp <= 0x309F) ||
         (cp >= 0x30A1 && cp <= 0x30FA) ||
         (cp >= 0x30FD && cp <= 0x30FF) || cp == 0x30FC;
}

bool isAllPreviewKana(const String& text) {
  if (text.length() == 0) {
    return false;
  }

  int pos = 0;
  while (pos < static_cast<int>(text.length())) {
    if (!isPreviewKanaCodepoint(utf8CodepointAt(text, pos))) {
      return false;
    }
    pos = nextUtf8CharEnd(text, pos);
  }
  return true;
}

bool shouldShowPreviewReading(const JapaneseDictionaryMatch& result) {
  return !(result.term == result.reading && isAllPreviewKana(result.term));
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

int previousUtf8CharStart(const String& text, int pos) {
  if (pos <= 0) {
    return 0;
  }
  int charStart = pos - 1;
  while (charStart > 0 &&
         (static_cast<uint8_t>(text[charStart]) & 0xC0) == 0x80) {
    --charStart;
  }
  return charStart;
}

int nextUtf8CharEnd(const String& text, int pos) {
  if (pos >= text.length()) {
    return text.length();
  }
  int next = pos + 1;
  while (next < text.length() &&
         (static_cast<uint8_t>(text[next]) & 0xC0) == 0x80) {
    ++next;
  }
  return next;
}

size_t utf8CharStarts(const String& text, int* starts, size_t maxStarts) {
  if (starts == nullptr || maxStarts == 0) {
    return 0;
  }

  size_t count = 0;
  int pos = 0;
  while (pos < static_cast<int>(text.length()) && count + 1 < maxStarts) {
    starts[count++] = pos;
    pos = nextUtf8CharEnd(text, pos);
  }
  starts[count] = text.length();
  return count;
}

size_t lookupSegmentedExact(const String& query,
                            JapaneseDictionaryMatch* outMatches,
                            size_t maxMatches) {
  if (!dictionary.isOpen() || query.length() == 0 || outMatches == nullptr ||
      maxMatches < 2) {
    return 0;
  }

  int starts[kMaxQueryChars + 1] = {};
  const size_t charCount = utf8CharStarts(query, starts, kMaxQueryChars + 1);
  if (charCount < 4) {
    return 0;
  }

  String segments[kMaxResults];
  size_t segmentCount = 0;
  size_t charPos = 0;
  while (charPos < charCount && segmentCount < maxMatches) {
    bool matched = false;
    const size_t remaining = charCount - charPos;
    if (remaining < 2) {
      return 0;
    }

    for (size_t segmentChars = remaining; segmentChars >= 2; --segmentChars) {
      const int start = starts[charPos];
      const int end = starts[charPos + segmentChars];
      const String segment = query.substring(start, end);

      if (dictionary.hasExact(segment)) {
        segments[segmentCount++] = segment;
        charPos += segmentChars;
        matched = true;
        break;
      }
      if (segmentChars == 2) {
        break;
      }
    }

    if (!matched) {
      return 0;
    }
  }

  if (segmentCount < 2 || charPos != charCount) {
    return 0;
  }

  size_t found = 0;
  for (size_t segmentIndex = 0;
       segmentIndex < segmentCount && found < maxMatches; ++segmentIndex) {
    const size_t remainingSegments = segmentCount - segmentIndex - 1;
    const size_t available = maxMatches - found;
    if (available <= remainingSegments) {
      return 0;
    }

    const size_t segmentLimit = available - remainingSegments;
    const size_t segmentFound =
        dictionary.lookupExact(segments[segmentIndex], outMatches + found,
                               segmentLimit);
    if (segmentFound == 0) {
      return 0;
    }
    found += segmentFound;
  }

  return found >= segmentCount ? found : 0;
}

int trailingKanaSegmentStart() {
  return committedKana.length() - trailingKanaSegment().length();
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
      const int charStart = pos;
      pos = nextUtf8CharEnd(text, pos);
      const String ch = text.substring(charStart, pos);
      if (ch == " ") {
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
          removeLastUtf8Char(word);
          pos = previousUtf8CharStart(text, pos);
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
      const int charStart = pos;
      pos = nextUtf8CharEnd(text, pos);
      const String ch = text.substring(charStart, pos);
      if (ch == " ") {
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
          removeLastUtf8Char(word);
          pos = previousUtf8CharStart(text, pos);
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

int drawWrappedPopupHeadwords(int x, int y, int width, int maxHeight,
                              const String& text, uint16_t color,
                              uint16_t background) {
  auto& display = M5Cardputer.Display;
  display.setFont(popup_headword_font::font());
  display.setTextDatum(top_left);
  display.setTextColor(color, background);

  int lineY = y;
  int pos = 0;
  while (pos < static_cast<int>(text.length()) &&
         lineY + kPopupHeadwordLineHeight <= y + maxHeight) {
    while (pos < static_cast<int>(text.length()) && text[pos] == ' ') {
      ++pos;
    }

    String line;
    String word;
    while (pos < static_cast<int>(text.length())) {
      const int charStart = pos;
      pos = nextUtf8CharEnd(text, pos);
      const String ch = text.substring(charStart, pos);
      if (ch == " ") {
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
          removeLastUtf8Char(word);
          pos = previousUtf8CharStart(text, pos);
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

    display.fillRect(x, lineY, width, kPopupHeadwordLineHeight, background);
    display.drawString(line, x, lineY);
    lineY += kPopupHeadwordLineHeight;
  }

  return lineY;
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
      "adjectival",  "adjective",     "adverb",      "agriculture",
      "anatomy",     "archaic",       "archeology",  "architecture",
      "art",         "astronomy",     "audiovisual", "aux-adj",
      "aux-verb",    "auxiliary",     "aviation",    "baseball",
      "biochemistry", "biology",      "botany",      "boxing",
      "Brazil",      "Buddhism",      "business",    "card games",
      "chemistry",   "childish",      "Chinese myth.", "Christian",
      "civil eng.",  "clothing",      "colloquial",  "computing",
      "conjunction", "copula",        "counter",     "crystal",
      "dated",       "dentistry",     "derogatory",  "ecology",
      "economics",   "electricity",   "electronics", "embryology",
      "engineering", "entomology",    "euphemism",   "exp",
      "familiar",    "feminine",      "film",        "finance",
      "fishing",     "food",          "formal",      "gardening",
      "genetics",    "geography",     "geology",     "geometry",
      "go (game)",   "golf",          "grammar",     "Greek myth.",
      "hanafuda",    "herpetology",   "historical",  "Hokkaidō",
      "honorific",   "horse racing",  "humble",      "i-adjective",
      "idiom",       "internet",      "interjection", "intransitive",
      "Japanese myth.", "jocular",    "kabuki",      "Kansai",
      "Kantō",       "ku-adj",        "kuru",        "Kyōto",
      "Kyūshū",      "law",           "legend",      "linguistics",
      "logic",       "mahjong",       "manga",       "manga slang",
      "martial arts", "masculine",    "math",        "mech. eng.",
      "medical",     "meteorology",   "military",    "mimetic",
      "mineralogy",  "mining",        "motorsport",  "music",
      "na-adj",      "Nagano",        "nari",        "net slang",
      "no-adj",      "noh",           "noun",        "nu-verb",
      "numeric",     "obsolete",      "ornithology", "Ōsaka",
      "paleontology", "particle",     "pathology",   "person",
      "pharmacology", "philosophy",   "photography", "physics",
      "physiology",  "place",         "poetical",    "polite",
      "politics",    "prefix",        "printing",    "pronoun",
      "proverb",     "psychiatry",    "psychoanalysis", "psychology",
      "quote",       "railway",       "rare",        "religion",
      "ri-verb",     "Roman myth.",   "Ryūkyū",      "sensitive",
      "shiku",       "Shintō",        "shōgi",       "skating",
      "skiing",      "slang",         "sports",      "statistics",
      "stock market", "suffix",       "sumō",        "surgery",
      "su-verb",     "suru verb",     "taru",        "telecom",
      "television",  "to-adverb",     "Tōhoku",      "Tosa",
      "trademark",   "transitive",    "Tsugaru",     "unclass",
      "usually kana", "verb",         "veterinary medicine", "video games",
      "vulgar",      "wasei",         "work",        "wrestling",
      "yoji",        "zoology",       "zuru",
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
  String numberedGlosses;
};

ParsedDefinition parseDefinition(const String& rawDefinition) {
  ParsedDefinition parsed;
  const String definition = cleanDefinitionForDisplay(rawDefinition);
  bool seenGloss = false;
  int glossCount = 0;
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
        ++glossCount;
        if (parsed.glosses.length() > 0) {
          parsed.glosses += " · ";
        }
        parsed.glosses += item;

        if (parsed.numberedGlosses.length() > 0) {
          parsed.numberedGlosses += "  ";
        }
        parsed.numberedGlosses += glossCount;
        parsed.numberedGlosses += ". ";
        parsed.numberedGlosses += item;
      }
    }

    if (end >= static_cast<int>(definition.length())) {
      break;
    }
    start = end + 2;
  }

  if (!seenGloss && parsed.attributes.length() > 0) {
    parsed.glosses = parsed.attributes;
    parsed.numberedGlosses = parsed.attributes;
    parsed.attributes = "";
  } else if (glossCount == 1) {
    parsed.numberedGlosses = parsed.glosses;
  }
  return parsed;
}

String compactResultLine(const JapaneseDictionaryMatch& result) {
  String line = result.term;
  if (shouldShowPreviewReading(result)) {
    line += " ";
    line += result.reading;
  }
  line += " ";
  line += parseDefinition(result.definition).glosses;
  return line;
}

String resultTermReadingLine(const JapaneseDictionaryMatch& result) {
  String line = result.term;
  if (shouldShowPreviewReading(result)) {
    line += " ";
    line += result.reading;
  }
  return line;
}

constexpr int kResultRowHeight = 20;
constexpr int kSelectedResultRowHeight = kResultRowHeight * 2;

void rebuildResultPreviewLines() {
  for (size_t i = 0; i < resultCount; ++i) {
    resultPreviewLines[i] = compactResultLine(results[i]);
  }
  for (size_t i = resultCount; i < kMaxResults; ++i) {
    resultPreviewLines[i] = "";
  }
}

int resultListHeight() {
  const int top = contentTop();
  const int height = contentBottom() - top;
  return height > 0 ? height : 0;
}

int resultListRowHeight(size_t resultIndex) {
  return resultIndex == selectedResult ? kSelectedResultRowHeight
                                       : kResultRowHeight;
}

size_t firstVisibleResultFor(size_t selected) {
  const int height = resultListHeight();
  if (height <= kSelectedResultRowHeight) {
    return selected;
  }

  const size_t rowsBeforeSelected =
      (height - kSelectedResultRowHeight) / kResultRowHeight;
  if (selected > rowsBeforeSelected) {
    return selected - rowsBeforeSelected;
  }
  return 0;
}

void drawResultListRow(size_t resultIndex, int y) {
  if (resultIndex >= resultCount) {
    return;
  }

  const int rowHeight = resultListRowHeight(resultIndex);
  if (y + rowHeight > contentBottom()) {
    return;
  }

  auto& display = M5Cardputer.Display;
  constexpr int pad = 5;
  const bool selected = resultIndex == selectedResult;
  const uint16_t background = selected ? TFT_DARKGREY : TFT_BLACK;
  const uint16_t foreground = selected ? TFT_YELLOW : TFT_GREEN;
  const String& line = resultPreviewLines[resultIndex];

  static LGFX_Sprite rowSprite(&display);
  if (rowSprite.getBuffer() == nullptr || rowSprite.width() != display.width() ||
      rowSprite.height() != rowHeight) {
    rowSprite.deleteSprite();
    rowSprite.setColorDepth(16);
    rowSprite.createSprite(display.width(), rowHeight);
  }
  if (rowSprite.getBuffer() != nullptr) {
    rowSprite.setFont(&fonts::efontJA_16);
    rowSprite.setTextDatum(top_left);
    rowSprite.fillSprite(background);
    rowSprite.setTextColor(foreground, background);
    if (selected) {
      rowSprite.drawString(resultTermReadingLine(results[resultIndex]), pad, 1);
      rowSprite.drawString(parseDefinition(results[resultIndex].definition).glosses,
                           pad, 20);
    } else {
      rowSprite.drawString(line, pad, 1);
    }
    rowSprite.pushSprite(0, y);
    return;
  }

  display.setFont(&fonts::efontJA_16);
  display.setTextDatum(top_left);
  display.fillRect(0, y, display.width(), rowHeight, background);
  display.setTextColor(foreground, background);
  if (selected) {
    display.drawString(resultTermReadingLine(results[resultIndex]), pad, y + 1);
    display.drawString(parseDefinition(results[resultIndex].definition).glosses,
                       pad, y + 20);
  } else {
    display.drawString(line, pad, y + 1);
  }
}

void redrawResultSelection(size_t oldSelected, size_t newSelected) {
  (void)oldSelected;
  (void)newSelected;
  drawResults();
}

int maxDefinitionScrollLine() {
  if (!searched || resultCount == 0 || selectedResult >= resultCount) {
    return 0;
  }

  auto &display = M5Cardputer.Display;
  const int top = kCompactContentTop;
  const int bottom = contentBottom();
  constexpr int pad = 5;
  constexpr bool largeDefinitionText = true;
  const int lineHeight = bodyLineHeight(largeDefinitionText);

  const ParsedDefinition definition =
      parseDefinition(results[selectedResult].definition);
  const int dividerY = top + 36;
  const int bodyY = dividerY + 5;
  const int bodyHeight = bottom - bodyY - 2;
  const int visibleLines = bodyHeight > 0 ? bodyHeight / lineHeight : 0;
  const int wrappedLines =
      countWrappedLines(display.width() - pad * 2, definition.numberedGlosses,
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
  headwordPopupVisible = false;
  viewMode = ViewMode::Results;
  searched = true;
  segmentedSearch = false;

  if (committedKana.length() == 0 || !dictionary.isOpen()) {
    resultCount = 0;
  } else {
    JapaneseDictionaryMatch staged[kMaxResults];
    const String katakanaQuery = hiraganaToKatakana(lastSearchedKana);
    const bool likelyInflected =
        hasLikelyInflectionEnding(lastSearchedKana);
    const bool allowKatakanaFallback = !likelyInflected;

    resultCount = 0;
    size_t stagedCount =
        dictionary.lookupExact(lastSearchedKana, staged, kMaxNormalResults);
    resultCount = appendSearchResults(results, resultCount, kMaxResults,
                                      staged, stagedCount);
    if (allowKatakanaFallback && resultCount < kMaxNormalResults &&
        katakanaQuery != lastSearchedKana) {
      stagedCount =
          dictionary.lookupExact(katakanaQuery, staged,
                                 stagedSearchLimit(resultCount));
      resultCount = appendSearchResults(results, resultCount, kMaxResults,
                                        staged, stagedCount);
    }
    sortSearchResults(results, resultCount);
    if (resultCount > kMaxNormalResults) {
      resultCount = kMaxNormalResults;
    }

    if (resultCount < kMaxNormalResults) {
      stagedCount =
          dictionary.lookupPrefix(lastSearchedKana, staged,
                                  stagedSearchLimit(resultCount));
      resultCount = appendSearchResults(results, resultCount,
                                        kMaxNormalResults, staged,
                                        stagedCount);
    }
    if (allowKatakanaFallback && resultCount < kMaxNormalResults &&
        katakanaQuery != lastSearchedKana) {
      stagedCount =
          dictionary.lookupPrefix(katakanaQuery, staged,
                                  stagedSearchLimit(resultCount));
      resultCount = appendSearchResults(results, resultCount,
                                        kMaxNormalResults, staged,
                                        stagedCount);
    }
    if (resultCount < kMaxNormalResults && likelyInflected) {
      stagedCount =
          dictionary.lookupDeinflected(lastSearchedKana, staged,
                                       stagedSearchLimit(resultCount));
      resultCount = appendSearchResults(results, resultCount,
                                        kMaxNormalResults, staged,
                                        stagedCount);
    }
    if (resultCount == 0) {
      resultCount =
          lookupSegmentedExact(lastSearchedKana, results, kMaxResults);
      segmentedSearch = resultCount > 0;
    }
  }
  if (resultCount > 0) {
    rememberSearch(lastSearchedKana);
  }
  resetSearchHistoryRecall();
  rebuildResultPreviewLines();

  Serial.printf("search kana='%s' results=%u segmented=%u\n",
                lastSearchedKana.c_str(), static_cast<unsigned>(resultCount),
                segmentedSearch);
  dirty = true;
}

bool kanjiSearchHasBothPanes() {
  return radicalComponentCount > 0 && kanjiCandidateCount > 0;
}

size_t kanjiSearchActiveCount() {
  return kanjiSearchPane == KanjiSearchPane::Parts ? radicalComponentCount
                                                   : kanjiCandidateCount;
}

size_t& kanjiSearchActiveSelection() {
  return kanjiSearchPane == KanjiSearchPane::Parts ? selectedRadicalCandidate
                                                   : selectedKanjiCandidate;
}

size_t kanjiSearchNavigationColumns() {
  return kanjiSearchHasBothPanes() ? 3 : kKanjiGridColumns;
}

void selectPreviousResult() {
  if (viewMode == ViewMode::KanjiSearch) {
    size_t& selected = kanjiSearchActiveSelection();
    const size_t columns = kanjiSearchNavigationColumns();
    if (selected > 0 && selected % columns != 0) {
      --selected;
      dirty = true;
    } else if (kanjiSearchHasBothPanes() &&
               kanjiSearchPane == KanjiSearchPane::Kanji) {
      kanjiSearchPane = KanjiSearchPane::Parts;
      selectedRadicalCandidate =
          selectedRadicalCandidate < radicalComponentCount
              ? selectedRadicalCandidate
              : radicalComponentCount > 0 ? radicalComponentCount - 1 : 0;
      dirty = true;
    }
    return;
  }

  if (viewMode == ViewMode::KanjiSpanPicker) {
    if (kanjiSpanOffset > 0) {
      kanjiSpanOffset = previousUtf8CharStart(kanjiSourceSegment, kanjiSpanOffset);
      dirty = true;
    }
    return;
  }

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
  const size_t oldSelected = selectedResult;
  const size_t oldFirstRow = firstVisibleResultFor(oldSelected);
  --selectedResult;
  definitionScrollLine = 0;
  if (!dirty && oldFirstRow == firstVisibleResultFor(selectedResult)) {
    redrawResultSelection(oldSelected, selectedResult);
  } else {
    dirty = true;
  }
}

void selectNextResult() {
  if (viewMode == ViewMode::KanjiSearch) {
    size_t& selected = kanjiSearchActiveSelection();
    const size_t count = kanjiSearchActiveCount();
    const size_t columns = kanjiSearchNavigationColumns();
    if (selected + 1 < count && selected % columns != columns - 1) {
      ++selected;
      dirty = true;
    } else if (kanjiSearchHasBothPanes() &&
               kanjiSearchPane == KanjiSearchPane::Parts) {
      kanjiSearchPane = KanjiSearchPane::Kanji;
      selectedKanjiCandidate =
          selectedKanjiCandidate < kanjiCandidateCount
              ? selectedKanjiCandidate
              : kanjiCandidateCount > 0 ? kanjiCandidateCount - 1 : 0;
      dirty = true;
    }
    return;
  }

  if (viewMode == ViewMode::KanjiSpanPicker) {
    const int nextOffset = nextUtf8CharEnd(kanjiSourceSegment, kanjiSpanOffset);
    if (nextOffset < kanjiSourceSegment.length()) {
      kanjiSpanOffset = nextOffset;
      dirty = true;
    }
    return;
  }

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
  const size_t oldSelected = selectedResult;
  const size_t oldFirstRow = firstVisibleResultFor(oldSelected);
  ++selectedResult;
  definitionScrollLine = 0;
  if (!dirty && oldFirstRow == firstVisibleResultFor(selectedResult)) {
    redrawResultSelection(oldSelected, selectedResult);
  } else {
    dirty = true;
  }
}

void selectUp() {
  if (viewMode == ViewMode::KanjiSearch) {
    size_t& selected = kanjiSearchActiveSelection();
    const size_t columns = kanjiSearchNavigationColumns();
    if (selected >= columns) {
      selected -= columns;
      dirty = true;
    }
    return;
  }

  if (recallSearchHistory(-1)) {
    return;
  }

  if (viewMode == ViewMode::KanjiSpanPicker) {
    selectPreviousResult();
    return;
  }

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
  if (viewMode == ViewMode::KanjiSearch) {
    size_t& selected = kanjiSearchActiveSelection();
    const size_t count = kanjiSearchActiveCount();
    const size_t columns = kanjiSearchNavigationColumns();
    if (selected + columns < count) {
      selected += columns;
      dirty = true;
    }
    return;
  }

  if (recallSearchHistory(1)) {
    return;
  }

  if (viewMode == ViewMode::KanjiSpanPicker) {
    selectNextResult();
    return;
  }

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
  resetSearchHistoryRecall();
  clearSearchResults();
  dirty = true;
}

bool kanjiListContains(String* items, size_t count, const String& value) {
  for (size_t i = 0; i < count; ++i) {
    if (items[i] == value) {
      return true;
    }
  }
  return false;
}

void filterKanjiCandidatesByList(String* allowed, size_t allowedCount) {
  size_t kept = 0;
  for (size_t i = 0; i < kanjiCandidateCount; ++i) {
    if (kanjiListContains(allowed, allowedCount, kanjiCandidates[i])) {
      kanjiCandidates[kept++] = kanjiCandidates[i];
    }
  }
  kanjiCandidateCount = kept;
}

void filterKanjiCandidatesByComponent(const String& component) {
  size_t kept = 0;
  for (size_t i = 0; i < kanjiCandidateCount; ++i) {
    if (kanjiIndex.containsComponentKanji(component, kanjiCandidates[i])) {
      kanjiCandidates[kept++] = kanjiCandidates[i];
    }
  }
  kanjiCandidateCount = kept;
}

void refreshSelectedRadicalKanjiCandidates() {
  kanjiCandidateCount = 0;
  if (!kanjiIndex.hasRadicalIndex() || selectedRadicalCount == 0) {
    return;
  }

  kanjiCandidateCount = kanjiIndex.lookupComponentKanji(
      selectedRadicalComponents[0], kanjiCandidates, kMaxKanjiCandidates);
  for (size_t i = 1; i < selectedRadicalCount && kanjiCandidateCount > 0; ++i) {
    const size_t scratchCount = kanjiIndex.lookupComponentKanji(
        selectedRadicalComponents[i], kanjiCandidateScratch,
        kMaxKanjiCandidates);
    filterKanjiCandidatesByList(kanjiCandidateScratch, scratchCount);
  }
}

String kanjiSearchKey() {
  return kanjiSearchDigits.length() > 0 ? kanjiSearchDigits
                                        : kanjiSearchCommitted + kanjiSearchPending;
}

bool kanjiSearchIsStrokeCount() {
  return kanjiSearchDigits.length() > 0;
}

bool kanjiSearchHasInput() {
  return kanjiSearchKey().length() > 0;
}

void normalizeKanjiSearchPane() {
  if (radicalComponentCount == 0 && kanjiCandidateCount > 0) {
    kanjiSearchPane = KanjiSearchPane::Kanji;
  } else if (kanjiCandidateCount == 0 && radicalComponentCount > 0) {
    kanjiSearchPane = KanjiSearchPane::Parts;
  }
  if (selectedRadicalCandidate >= radicalComponentCount) {
    selectedRadicalCandidate = radicalComponentCount > 0 ? radicalComponentCount - 1 : 0;
  }
  if (selectedKanjiCandidate >= kanjiCandidateCount) {
    selectedKanjiCandidate = kanjiCandidateCount > 0 ? kanjiCandidateCount - 1 : 0;
  }
}

void refreshKanjiSearchCandidates() {
  radicalComponentCount = 0;
  kanjiCandidateCount = 0;

  openKanjiIndex("search");
  if (!kanjiIndex.isOpen()) {
    return;
  }

  const String key = kanjiSearchKey();
  if (kanjiIndex.hasRadicalIndex()) {
    if (kanjiSearchIsStrokeCount()) {
      radicalComponentCount = kanjiIndex.lookupRadicalsByStroke(
          key, radicalComponents, kMaxRadicalComponents);
    } else if (key.length() > 0) {
      radicalComponentCount = kanjiIndex.lookupRadicalAliases(
          key, radicalComponents, kMaxRadicalComponents);
    }
  }

  refreshSelectedRadicalKanjiCandidates();
  if (kanjiSearchIsStrokeCount() && key.length() > 0 &&
      kanjiIndex.hasRadicalIndex()) {
    if (selectedRadicalCount > 0) {
      kanjiCandidateCount = kanjiIndex.lookupComponentKanjiByStroke(
          selectedRadicalComponents[0], key, kanjiCandidates,
          kMaxKanjiCandidates);
      for (size_t i = 1; i < selectedRadicalCount && kanjiCandidateCount > 0;
           ++i) {
        filterKanjiCandidatesByComponent(selectedRadicalComponents[i]);
      }
    } else {
      const size_t strokeKanjiCount = kanjiIndex.lookupKanjiByStroke(
          key, kanjiCandidateScratch, kMaxKanjiCandidates);
      kanjiCandidateCount = strokeKanjiCount;
      for (size_t i = 0; i < kanjiCandidateCount; ++i) {
        kanjiCandidates[i] = kanjiCandidateScratch[i];
      }
    }
  } else if (!kanjiSearchIsStrokeCount() && key.length() > 0) {
    const size_t readingCount =
        kanjiIndex.lookup(key, kanjiCandidateScratch, kMaxKanjiCandidates);
    if (selectedRadicalCount > 0) {
      filterKanjiCandidatesByList(kanjiCandidateScratch, readingCount);
    } else {
      kanjiCandidateCount = readingCount;
      for (size_t i = 0; i < kanjiCandidateCount; ++i) {
        kanjiCandidates[i] = kanjiCandidateScratch[i];
      }
    }
  }
  normalizeKanjiSearchPane();
}

void clearKanjiSearchInput() {
  kanjiSearchCommitted = "";
  kanjiSearchPending = "";
  kanjiSearchDigits = "";
}

void openKanjiSearch() {
  composePending(true);
  kanjiSearchCommitted = "";
  kanjiSearchPending = "";
  kanjiSearchDigits = "";
  selectedRadicalCount = 0;
  selectedRadicalCandidate = 0;
  selectedKanjiCandidate = 0;
  kanjiSearchPane = KanjiSearchPane::Parts;
  viewMode = ViewMode::KanjiSearch;
  refreshKanjiSearchCandidates();
  dirty = true;
}

void closeKanjiSearch() {
  if (viewMode != ViewMode::KanjiSearch) {
    return;
  }
  viewMode = ViewMode::Results;
  dirty = true;
}

void appendKanjiSearchChar(char ch) {
  if ((ch >= 'A' && ch <= 'Z')) {
    ch = static_cast<char>(ch - 'A' + 'a');
  }

  if (ch >= '0' && ch <= '9') {
    if (kanjiSearchCommitted.length() > 0 || kanjiSearchPending.length() > 0 ||
        kanjiSearchDigits.length() >= 2) {
      return;
    }
    kanjiSearchDigits += ch;
  } else {
    if (kanjiSearchDigits.length() > 0 ||
        kanjiSearchCommitted.length() + kanjiSearchPending.length() >=
            kMaxQueryChars) {
      return;
    }
    if (!((ch >= 'a' && ch <= 'z') || ch == '-' || ch == '\'')) {
      return;
    }
    kanjiSearchPending += ch;
    composeKanjiSearchPending(false);
  }
  refreshKanjiSearchCandidates();
  dirty = true;
}

void deleteKanjiSearchChar() {
  bool changed = false;
  if (kanjiSearchDigits.length() > 0) {
    kanjiSearchDigits.remove(kanjiSearchDigits.length() - 1);
    changed = true;
  } else if (kanjiSearchPending.length() > 0) {
    kanjiSearchPending.remove(kanjiSearchPending.length() - 1);
    changed = true;
  } else if (kanjiSearchCommitted.length() > 0) {
    removeLastUtf8Char(kanjiSearchCommitted);
    changed = true;
  }

  if (!changed) {
    return;
  }
  refreshKanjiSearchCandidates();
  dirty = true;
}

void addSelectedRadical(const String& radical) {
  if (selectedRadicalCount >= kMaxSelectedRadicals ||
      kanjiListContains(selectedRadicalComponents, selectedRadicalCount,
                        radical)) {
    return;
  }
  selectedRadicalComponents[selectedRadicalCount++] = radical;
  clearKanjiSearchInput();
  refreshKanjiSearchCandidates();
  dirty = true;
}

void removeLastSelectedRadical() {
  if (selectedRadicalCount == 0) {
    return;
  }
  --selectedRadicalCount;
  selectedRadicalComponents[selectedRadicalCount] = "";
  refreshKanjiSearchCandidates();
  dirty = true;
}

void insertKanjiSearchCandidate(size_t index) {
  if (index >= kanjiCandidateCount) {
    return;
  }
  committedKana += kanjiCandidates[index];
  pendingRomaji = "";
  resetSearchHistoryRecall();
  clearSearchResults();
  viewMode = ViewMode::Results;
  dirty = true;
}

void chooseKanjiSearchItem() {
  if (viewMode != ViewMode::KanjiSearch) {
    return;
  }

  if (kanjiSearchPane == KanjiSearchPane::Parts && radicalComponentCount > 0) {
    addSelectedRadical(radicalComponents[selectedRadicalCandidate]);
    return;
  }

  if (kanjiSearchPane == KanjiSearchPane::Kanji && kanjiCandidateCount > 0) {
    insertKanjiSearchCandidate(selectedKanjiCandidate);
  }
}

void openKanjiPicker() {
  composePending(true);
  kanjiSourceSegment = trailingKanaSegment();
  kanjiSourceStart = trailingKanaSegmentStart();
  kanjiSpanOffset = 0;
  kanjiReading = "";
  kanjiReplaceStart = kanjiSourceStart;
  kanjiReplaceLength = kanjiSourceSegment.length();
  selectedKanjiCandidate = 0;
  kanjiCandidateCount = 0;
  viewMode = ViewMode::KanjiSpanPicker;
  dirty = true;
}

void confirmKanjiSpan() {
  if (viewMode != ViewMode::KanjiSpanPicker) {
    return;
  }

  kanjiReading = kanjiSourceSegment.substring(kanjiSpanOffset);
  kanjiReplaceStart = kanjiSourceStart + kanjiSpanOffset;
  kanjiReplaceLength = kanjiSourceSegment.length() - kanjiSpanOffset;
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
  if (viewMode != ViewMode::KanjiPicker &&
      viewMode != ViewMode::KanjiSpanPicker) {
    return;
  }
  viewMode = ViewMode::Results;
  dirty = true;
}

bool isBackKey(char ch) {
  return ch == '`' || ch == '~';
}

void handleEscCommand() {
  if (viewMode == ViewMode::Definition) {
    if (headwordPopupVisible) {
      headwordPopupVisible = false;
      dirty = true;
      return;
    }
    leaveDefinition();
    return;
  }
  if (viewMode == ViewMode::KanjiSearch) {
    if (kanjiSearchHasInput()) {
      clearKanjiSearchInput();
      refreshKanjiSearchCandidates();
      dirty = true;
      return;
    }
    if (selectedRadicalCount > 0) {
      removeLastSelectedRadical();
      return;
    }
    closeKanjiSearch();
    return;
  }
  if (viewMode == ViewMode::KanjiSpanPicker ||
      viewMode == ViewMode::KanjiPicker) {
    closeKanjiPicker();
    return;
  }
  if (viewMode == ViewMode::Results && searched) {
    clearSearchResults();
    dirty = true;
    return;
  }
  if (viewMode == ViewMode::Results &&
      (committedKana.length() > 0 || pendingRomaji.length() > 0)) {
    clearQuery();
    return;
  }
}

void insertSelectedKanji() {
  if (viewMode != ViewMode::KanjiPicker || kanjiCandidateCount == 0 ||
      selectedKanjiCandidate >= kanjiCandidateCount) {
    closeKanjiPicker();
    return;
  }
  String updated = committedKana.substring(0, kanjiReplaceStart);
  updated += kanjiCandidates[selectedKanjiCandidate];
  updated += committedKana.substring(kanjiReplaceStart + kanjiReplaceLength);
  committedKana = updated;
  resetSearchHistoryRecall();
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
    resetSearchHistoryRecall();
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
  resetSearchHistoryRecall();
  clearSearchResults();
  dirty = true;
}

void drawHeader() {
  auto &display = M5Cardputer.Display;
  const int headerBottom = contentTop();
  display.fillRect(0, 0, display.width(), headerBottom, TFT_NAVY);
  display.setTextDatum(top_left);

  const String inputText =
      currentInputText().length() > 0 ? currentInputText() : String("Type: Romaji");
  const uint16_t inputColor =
      currentInputText().length() > 0 ? TFT_CYAN : TFT_LIGHTGREY;
  constexpr int batteryWidth = 30;
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
  const int bottom = contentBottom();
  constexpr int pad = 5;

  display.fillRect(0, top, display.width(), bottom - top, TFT_BLACK);

  if (!searched) {
    display.setTextDatum(top_left);
    display.setTextColor(startupStatusColor(), TFT_BLACK);
    display.drawString(ellipsize(startupStatusLine(), display.width() - pad * 2),
                       pad, top + 2);
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    if (storageState == StorageState::DictionaryOk) {
      display.drawString("Enter: Search", pad, top + 18);
      display.drawString("Right: Kanji", pad, top + 33);
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

  const size_t firstRow = firstVisibleResultFor(selectedResult);
  int rowY = top;
  for (size_t resultIndex = firstRow; resultIndex < resultCount; ++resultIndex) {
    const int rowHeight = resultListRowHeight(resultIndex);
    if (rowY + rowHeight > bottom) {
      break;
    }
    drawResultListRow(resultIndex, rowY);
    rowY += rowHeight;
  }
}

void drawDefinition() {
  auto &display = M5Cardputer.Display;
  const int top = contentTop();
  const int bottom = contentBottom();
  constexpr int pad = 5;

  display.fillRect(0, top, display.width(), bottom - top, TFT_BLACK);

  if (!searched || resultCount == 0) {
    viewMode = ViewMode::Results;
    drawResults();
    return;
  }

  const JapaneseDictionaryMatch &result = results[selectedResult];
  const ParsedDefinition definition = parseDefinition(result.definition);
  display.setTextDatum(top_left);
  drawHeadwordLine(pad, top, display.width() - pad * 2, result);
  display.setFont(&fonts::efontJA_12);
  String reading = String("[") + result.reading + "]";
  String metadata;
  if (result.termCount > 1) {
    metadata += result.termCount;
    metadata += " forms";
  }
  if (definition.attributes.length() > 0) {
    if (metadata.length() > 0) {
      metadata += " ";
    }
    metadata += " (";
    metadata += definition.attributes;
    metadata += ")";
  }
  drawMetadataLine(pad, top + 21, display.width() - pad * 2, reading, metadata,
                   !marqueeActive);
  int dividerY = top + 36;
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.drawFastHLine(pad, dividerY, display.width() - pad * 2,
                        TFT_DARKGREY);

  const int bodyY = dividerY + 5;
  drawWrappedText(pad, bodyY, display.width() - pad * 2,
                  bottom - bodyY - 2, definition.numberedGlosses, TFT_WHITE,
                  TFT_BLACK, definitionScrollLine, true);
}

void drawHeadwordPopup() {
  if (!headwordPopupVisible || viewMode != ViewMode::Definition || !searched ||
      resultCount == 0 || selectedResult >= resultCount) {
    return;
  }

  auto& display = M5Cardputer.Display;
  constexpr int pad = 8;
  const int panelX = 5;
  const int panelY = contentTop() + 4;
  const int panelW = display.width() - panelX * 2;
  const int panelH = display.height() - panelY - 5;
  const int textX = panelX + pad;
  const int textY = panelY + 10;
  const int textW = panelW - pad * 2;

  display.fillRect(panelX - 3, panelY - 3, panelW + 6, panelH + 6, TFT_BLACK);
  display.fillRect(panelX, panelY, panelW, panelH, TFT_DARKGREY);
  display.drawRect(panelX, panelY, panelW, panelH, TFT_LIGHTGREY);

  const int textH = panelY + panelH - textY - 8;
  drawWrappedPopupHeadwords(textX, textY, textW, textH,
                            results[selectedResult].terms, TFT_GREEN,
                            TFT_DARKGREY);
}

void drawKanjiSearchGridItem(size_t index, const String& text, int x, int y,
                             int cellW) {
  auto &display = M5Cardputer.Display;
  const bool selected = index == selectedKanjiCandidate;
  const bool isPart = index < radicalComponentCount;
  const uint16_t bg = selected ? TFT_DARKGREY : TFT_BLACK;
  const uint16_t fg = selected ? TFT_YELLOW : isPart ? TFT_CYAN : TFT_WHITE;
  display.fillRect(x - 1, y - 1, cellW, 20, bg);
  display.setTextColor(fg, bg);
  display.drawString(text, x + 3, y);
}

void drawKanjiSearchGroupBorder(size_t pageStart, size_t pageEnd,
                                size_t groupStart, size_t groupEnd, int x0,
                                int y0, int cellW, uint16_t color) {
  auto &display = M5Cardputer.Display;
  constexpr int rowH = 21;
  const size_t start = groupStart > pageStart ? groupStart : pageStart;
  const size_t end = groupEnd < pageEnd ? groupEnd : pageEnd;
  if (start >= end) {
    return;
  }

  for (size_t rowStart = pageStart; rowStart < pageEnd;
       rowStart += kKanjiGridColumns) {
    const size_t rowEnd = rowStart + kKanjiGridColumns < pageEnd
                              ? rowStart + kKanjiGridColumns
                              : pageEnd;
    const size_t segmentStart = start > rowStart ? start : rowStart;
    const size_t segmentEnd = end < rowEnd ? end : rowEnd;
    if (segmentStart >= segmentEnd) {
      continue;
    }

    const int row = (rowStart - pageStart) / kKanjiGridColumns;
    const int colStart = segmentStart - rowStart;
    const int colEnd = segmentEnd - rowStart;
    display.drawRect(x0 + colStart * cellW - 2, y0 + row * rowH - 2,
                     (colEnd - colStart) * cellW + 3, rowH + 2, color);
  }
}

size_t visibleKanjiSearchItems(int gridTop, int gridBottom, size_t columns) {
  constexpr int rowH = 21;
  if (gridBottom <= gridTop) {
    return columns;
  }
  size_t rows = (gridBottom - gridTop) / rowH;
  if (rows == 0) {
    rows = 1;
  }
  return rows * columns;
}

void drawKanjiSearchCell(const String& text, int x, int y, int cellW,
                         bool selected, bool isPart) {
  auto &display = M5Cardputer.Display;
  const uint16_t bg = selected ? TFT_DARKGREY : TFT_BLACK;
  const uint16_t fg = selected ? TFT_YELLOW : isPart ? TFT_CYAN : TFT_WHITE;
  display.fillRect(x - 1, y - 1, cellW, 20, bg);
  display.setTextColor(fg, bg);
  display.drawString(text, x + 3, y);
}

void drawKanjiSearchPaneBox(const char* label, size_t count, int x, int y,
                            int w, int h, bool active, bool isPart) {
  auto &display = M5Cardputer.Display;
  const uint16_t color = active ? TFT_YELLOW : isPart ? TFT_CYAN : TFT_LIGHTGREY;
  display.drawRect(x, y, w, h, color);
  display.setFont(&fonts::efontJA_12);
  display.setTextColor(color, TFT_BLACK);
  display.drawString(String(label) + " " + count, x + 4, y + 3);
}

void drawKanjiSearch() {
  auto &display = M5Cardputer.Display;
  const int top = contentTop();
  const int bottom = contentBottom();
  constexpr int pad = 5;
  display.fillRect(0, top, display.width(), bottom - top, TFT_BLACK);
  display.setTextDatum(top_left);
  display.setFont(&fonts::efontJA_12);

  if (!kanjiIndex.isOpen()) {
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.drawString("Kanji index missing", pad, top + 2);
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.drawString("Expected /kanji", pad, top + 18);
    return;
  }

  const String input = kanjiSearchKey().length() > 0 ? kanjiSearchKey() : "-";
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawString(
      ellipsize(String("Input: ") + input, display.width() - pad * 2), pad,
      top + 2);

  const size_t total = radicalComponentCount + kanjiCandidateCount;
  if (total == 0) {
    if (!kanjiSearchHasInput() && selectedRadicalCount == 0) {
      display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      display.drawString("Type reading, part, or count", pad, top + 19);
      display.drawString("Enter: Pick  Esc: Back", pad, top + 35);
      if (kanjiIndex.isOpen() && !kanjiIndex.hasRadicalIndex()) {
        display.setTextColor(TFT_ORANGE, TFT_BLACK);
        display.drawString("Radical index unavailable", pad, top + 51);
      }
    } else {
      display.setTextColor(TFT_ORANGE, TFT_BLACK);
      display.drawString("No matches", pad, top + 19);
      if (!kanjiIndex.hasRadicalIndex()) {
        display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        display.drawString("Radical files not found", pad, top + 35);
      }
    }
    return;
  }

  constexpr int headerH = 16;
  constexpr int rowH = 21;
  const int paneTop = top + 19;
  const int paneH = bottom - paneTop - 1;
  const bool split = radicalComponentCount > 0 && kanjiCandidateCount > 0;
  const int gap = split ? 4 : 0;
  const int partW = split ? (display.width() - pad * 2 - gap) / 2
                          : display.width() - pad * 2;
  const int kanjiW = split ? display.width() - pad * 2 - gap - partW
                           : display.width() - pad * 2;
  const int partX = pad;
  const int kanjiX = split ? pad + partW + gap : pad;

  if (radicalComponentCount > 0) {
    const bool active = kanjiSearchPane == KanjiSearchPane::Parts;
    drawKanjiSearchPaneBox("Parts", radicalComponentCount, partX, paneTop,
                           partW, paneH, active, true);
    const size_t columns = split ? 3 : kKanjiGridColumns;
    const int cellW = (partW - 6) / columns;
    const int gridTop = paneTop + headerH;
    const size_t visibleItems = visibleKanjiSearchItems(
        gridTop, bottom - 2, columns);
    const size_t pageStart =
        (selectedRadicalCandidate / visibleItems) * visibleItems;
    const size_t pageEnd =
        pageStart + visibleItems < radicalComponentCount
            ? pageStart + visibleItems
            : radicalComponentCount;
    display.setFont(&fonts::efontJA_16);
    for (size_t i = pageStart; i < pageEnd; ++i) {
      const size_t pageIndex = i - pageStart;
      const int x = partX + 3 + (pageIndex % columns) * cellW;
      const int y = gridTop + (pageIndex / columns) * rowH;
      drawKanjiSearchCell(radicalComponents[i], x, y, cellW,
                          active && i == selectedRadicalCandidate, true);
    }
  }

  if (kanjiCandidateCount > 0) {
    const bool active = kanjiSearchPane == KanjiSearchPane::Kanji;
    drawKanjiSearchPaneBox("Kanji", kanjiCandidateCount, kanjiX, paneTop,
                           kanjiW, paneH, active, false);
    const size_t columns = split ? 3 : kKanjiGridColumns;
    const int cellW = (kanjiW - 6) / columns;
    const int gridTop = paneTop + headerH;
    const size_t visibleItems = visibleKanjiSearchItems(
        gridTop, bottom - 2, columns);
    const size_t pageStart =
        (selectedKanjiCandidate / visibleItems) * visibleItems;
    const size_t pageEnd =
        pageStart + visibleItems < kanjiCandidateCount
            ? pageStart + visibleItems
            : kanjiCandidateCount;
    display.setFont(&fonts::efontJA_16);
    for (size_t i = pageStart; i < pageEnd; ++i) {
      const size_t pageIndex = i - pageStart;
      const int x = kanjiX + 3 + (pageIndex % columns) * cellW;
      const int y = gridTop + (pageIndex / columns) * rowH;
      drawKanjiSearchCell(kanjiCandidates[i], x, y, cellW,
                          active && i == selectedKanjiCandidate, false);
    }
  }
  display.setFont(&fonts::efontJA_12);
}

void drawKanjiPicker() {
  auto &display = M5Cardputer.Display;
  const int top = contentTop();
  const int bottom = contentBottom();
  constexpr int pad = 5;
  display.fillRect(0, top, display.width(), bottom - top, TFT_BLACK);
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
  int y = top + 22;
  constexpr int cellH = 20;
  constexpr int rowH = 21;
  for (size_t i = pageStart; i < pageEnd; ++i) {
    const bool selected = i == selectedKanjiCandidate;
    const uint16_t bg = selected ? TFT_DARKGREY : TFT_BLACK;
    const uint16_t fg = selected ? TFT_YELLOW : TFT_WHITE;
    const int cellW = (display.width() - pad * 2) / kKanjiGridColumns;
    display.fillRect(x - 1, y - 1, cellW, cellH, bg);
    display.setTextColor(fg, bg);
    display.drawString(kanjiCandidates[i], x + 3, y);
    x += cellW;
    if ((i - pageStart + 1) % kKanjiGridColumns == 0) {
      x = pad;
      y += rowH;
    }
  }
  display.setFont(&fonts::efontJA_12);
}

void drawKanjiSpanPicker() {
  auto &display = M5Cardputer.Display;
  const int top = contentTop();
  const int bottom = contentBottom();
  constexpr int pad = 5;
  display.fillRect(0, top, display.width(), bottom - top, TFT_BLACK);
  display.setTextDatum(top_left);
  display.setFont(&fonts::efontJA_12);

  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawString("Kanji Span", pad, top + 2);

  if (kanjiSourceSegment.length() == 0) {
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.drawString("No trailing kana", pad, top + 24);
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.drawString("Type: Kana before opening", pad, top + 42);
    return;
  }

  const int spanStart = kanjiSourceStart + kanjiSpanOffset;
  const String before = committedKana.substring(0, spanStart);
  const String span = kanjiSourceSegment.substring(kanjiSpanOffset);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString(ellipsize(String("Before: ") + before,
                               display.width() - pad * 2),
                     pad, top + 22);

  display.setFont(&fonts::efontJA_16);
  display.setTextColor(TFT_YELLOW, TFT_BLACK);
  display.drawString(ellipsize(String("[") + span + "]",
                               display.width() - pad * 2),
                     pad, top + 42);
  display.setFont(&fonts::efontJA_12);

  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("<: Widen    >: Shrink", pad, top + 72);
  display.drawString("Enter: Candidates", pad, top + 88);
}

String helpTitle() {
  if (viewMode == ViewMode::KanjiSpanPicker) {
    return "Kanji Span";
  }
  if (viewMode == ViewMode::KanjiSearch) {
    return "Kanji Search";
  }
  if (viewMode == ViewMode::KanjiPicker) {
    return "Kanji Picker";
  }
  if (viewMode == ViewMode::Definition) {
    return "Definition";
  }
  if (searched && resultCount > 0) {
    return "Results";
  }
  return "Search";
}

size_t helpLines(String* lines, size_t maxLines) {
  size_t count = 0;
  auto addLine = [&](const String& line) {
    if (count < maxLines) {
      lines[count++] = line;
    }
  };

  if (viewMode == ViewMode::KanjiSpanPicker) {
    addLine("Up/Down: Span");
    addLine("Enter: Choose");
    addLine("Esc: Cancel");
  } else if (viewMode == ViewMode::KanjiSearch) {
    addLine("Type: reading/part/count");
    addLine("Parts: add filter");
    addLine("Kanji: insert");
    addLine("Arrows: Nav");
    addLine("Enter: Pick  Esc: Back");
  } else if (viewMode == ViewMode::KanjiPicker) {
    addLine("Up/Down: Nav");
    addLine("Enter: Insert");
    addLine("Esc: Cancel");
  } else if (viewMode == ViewMode::Definition) {
    addLine("Up/Down: Scroll");
    addLine("Enter: Headwords");
    addLine("Esc: Back");
  } else if (searched && resultCount > 0) {
    addLine("Up/Down: Nav");
    addLine("Enter: Open");
    addLine("Esc: Back");
  } else {
    addLine("Type: Romaji");
    addLine("Enter: Search");
    addLine("Right: Kanji");
    addLine("Esc: Clear");
  }

  if (count < maxLines && viewMode != ViewMode::Definition) {
    addLine("Ctrl: Hide Help");
  }
  return count;
}

void drawHelpOverlay() {
  if (!helpVisible) {
    return;
  }

  auto &display = M5Cardputer.Display;
  constexpr size_t maxLines = 5;
  String lines[maxLines];
  const size_t lineCount = helpLines(lines, maxLines);
  const int panelW = display.width() - 34;
  const int panelH = 26 + static_cast<int>(lineCount) * 15 + 8;
  const int panelX = (display.width() - panelW) / 2;
  const int panelY = (display.height() - panelH) / 2;

  display.fillRect(panelX - 3, panelY - 3, panelW + 6, panelH + 6, TFT_BLACK);
  display.fillRect(panelX, panelY, panelW, panelH, TFT_DARKGREY);
  display.drawRect(panelX, panelY, panelW, panelH, TFT_LIGHTGREY);
  display.setFont(&fonts::efontJA_12);
  display.setTextDatum(top_left);
  display.setTextColor(TFT_YELLOW, TFT_DARKGREY);
  display.drawString(helpTitle(), panelX + 8, panelY + 6);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  for (size_t i = 0; i < lineCount; ++i) {
    display.drawString(lines[i], panelX + 8, panelY + 24 + i * 15);
  }
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
    drawHeadwordPopup();
  } else if (viewMode == ViewMode::KanjiSearch) {
    drawKanjiSearch();
  } else if (viewMode == ViewMode::KanjiSpanPicker) {
    drawKanjiSpanPicker();
  } else if (viewMode == ViewMode::KanjiPicker) {
    drawKanjiPicker();
  } else {
    drawResults();
  }
  if (!headwordPopupVisible) {
    drawHelpOverlay();
  }
  dirty = false;
}

void openDefinition() {
  if (!searched || resultCount == 0) {
    runSearch();
    return;
  }
  viewMode = ViewMode::Definition;
  definitionScrollLine = 0;
  headwordPopupVisible = false;
  dirty = true;
}

void leaveDefinition() {
  if (viewMode != ViewMode::Definition) {
    return;
  }
  headwordPopupVisible = false;
  viewMode = ViewMode::Results;
  dirty = true;
}

void reduceCpuForIdle() {
  if (!idleCpuReduced) {
    setCpuFrequencyMhz(kIdleCpuFrequencyMhz);
    idleCpuReduced = true;
  }
}

void restoreCpuAfterIdle() {
  if (idleCpuReduced) {
    const uint32_t restoreFrequency =
        normalCpuFrequencyMhz > 0 ? normalCpuFrequencyMhz
                                  : kFallbackNormalCpuFrequencyMhz;
    setCpuFrequencyMhz(restoreFrequency);
    idleCpuReduced = false;
  }
}

bool noteInputActivity() {
  lastInputAt = millis();
  const bool wokeFromIdle = backlightDimmed || backlightOff || displaySleeping ||
                            idleCpuReduced;
  if (wokeFromIdle) {
    restoreCpuAfterIdle();
    if (displaySleeping) {
      M5Cardputer.Display.wakeup();
      displaySleeping = false;
    }
    M5Cardputer.Display.setBrightness(kBacklightNormal);
    backlightDimmed = false;
    backlightOff = false;
    dirty = true;
  }
  return wokeFromIdle;
}

void updateBacklightIdle(uint32_t now) {
  const uint32_t idleMs = now - lastInputAt;
  if (!displaySleeping && idleMs >= kDisplaySleepAfterMs) {
    M5Cardputer.Display.setBrightness(kBacklightOff);
    backlightDimmed = true;
    backlightOff = true;
    M5Cardputer.Display.sleep();
    displaySleeping = true;
    reduceCpuForIdle();
  } else if (!backlightDimmed && idleMs >= kBacklightDimAfterMs) {
    M5Cardputer.Display.setBrightness(kBacklightDim);
    backlightDimmed = true;
    reduceCpuForIdle();
  }
}

void handleKeyboard() {
  auto &keyboard = M5Cardputer.Keyboard;
  if (!keyboard.isChange() || !keyboard.isPressed()) {
    return;
  }

  if (noteInputActivity()) {
    return;
  }

  auto &keys = keyboard.keysState();
  if (keys.ctrl) {
    helpVisible = !helpVisible;
    dirty = true;
    return;
  }
  if (helpVisible) {
    helpVisible = false;
    dirty = true;
    return;
  }
  if (headwordPopupVisible) {
    headwordPopupVisible = false;
    dirty = true;
    return;
  }

  if (keys.del) {
    if (viewMode == ViewMode::KanjiSearch) {
      deleteKanjiSearchChar();
      return;
    }
    if (viewMode == ViewMode::Results) {
      deleteChar();
    }
    return;
  }
  if (keys.enter) {
    if (viewMode == ViewMode::Definition) {
      headwordPopupVisible = true;
      marqueeActive = false;
      helpVisible = false;
      dirty = true;
      return;
    }
    if (viewMode == ViewMode::KanjiSpanPicker) {
      confirmKanjiSpan();
      return;
    }
    if (viewMode == ViewMode::KanjiPicker) {
      insertSelectedKanji();
      return;
    }
    if (viewMode == ViewMode::KanjiSearch) {
      const bool hadPending = kanjiSearchPending.length() > 0;
      composeKanjiSearchPending(true);
      if (hadPending) {
        refreshKanjiSearchCandidates();
      }
      chooseKanjiSearchItem();
      return;
    }
    openDefinition();
    return;
  }
  if (keys.tab) {
    return;
  }

  if (viewMode == ViewMode::Definition) {
    for (const char ch : keys.word) {
      if (isBackKey(ch)) {
        handleEscCommand();
      } else if (ch == ';') {
        selectUp();
      } else if (ch == '.' || ch == ' ') {
        selectDown();
      }
    }
    return;
  }

  if (viewMode == ViewMode::KanjiSearch) {
    for (const char ch : keys.word) {
      if (isBackKey(ch)) {
        handleEscCommand();
      } else if (ch == ';') {
        selectUp();
      } else if (ch == '.' || ch == ' ') {
        selectDown();
      } else if (ch == ',') {
        selectPreviousResult();
      } else if (ch == '/') {
        selectNextResult();
      } else {
        appendKanjiSearchChar(ch);
      }
    }
    return;
  }

  if (viewMode == ViewMode::KanjiSpanPicker) {
    for (const char ch : keys.word) {
      if (isBackKey(ch)) {
        handleEscCommand();
      } else if (ch == ';' || ch == ',') {
        selectPreviousResult();
      } else if (ch == '.' || ch == ' ' || ch == '/') {
        selectNextResult();
      }
    }
    return;
  }

  if (viewMode == ViewMode::KanjiPicker) {
    for (const char ch : keys.word) {
      if (isBackKey(ch)) {
        handleEscCommand();
      } else if (ch == ';') {
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
    if (isBackKey(ch)) {
      handleEscCommand();
    } else if (ch == ';') {
      selectUp();
    } else if (ch == '.' || ch == ' ') {
      selectDown();
    } else if (ch == ',') {
      // Left arrow is not used for main navigation.
    } else if (ch == '/') {
      if (!searched) {
        openKanjiSearch();
      }
    } else {
      appendChar(ch);
    }
  }
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  WiFi.mode(WIFI_OFF);

  Serial.begin(115200);
  delay(100);
  normalCpuFrequencyMhz = getCpuFrequencyMhz();

  auto &display = M5Cardputer.Display;
  display.setRotation(1);
  display.setBrightness(kBacklightNormal);
  display.clear(TFT_BLACK);

  lastInputAt = millis();
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

  if (!helpVisible && !headwordPopupVisible && marqueeActive &&
      shouldAnimateMarquee(now) &&
      now - lastMarqueeFrame >= kMarqueeFrameMs) {
    lastMarqueeFrame = now;
    drawMarqueeFrame();
  }

  updateBacklightIdle(now);
  delay(kIdleDelayMs);
}
