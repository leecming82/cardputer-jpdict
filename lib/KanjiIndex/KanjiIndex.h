#pragma once

#include <Arduino.h>
#include <FS.h>

class KanjiIndex {
 public:
  bool open(const char* basePath);
  void close();
  bool isOpen() const;
  const String& path() const;
  size_t lookup(const String& reading, String* outCandidates,
                size_t maxCandidates);
  bool hasRadicalIndex() const;
  size_t lookupRadicalAliases(const String& alias, String* outComponents,
                              size_t maxComponents);
  size_t lookupRadicalsByStroke(const String& strokes, String* outComponents,
                                size_t maxComponents);
  size_t lookupKanjiByStroke(const String& strokes, String* outCandidates,
                             size_t maxCandidates);
  size_t lookupComponentKanji(const String& component, String* outCandidates,
                              size_t maxCandidates);

 private:
  static constexpr size_t kKeyBytes = 64;
  static constexpr size_t kRecordBytes = 76;

  struct Record {
    char key[kKeyBytes + 1] = {};
    uint16_t keyLen = 0;
    uint32_t candidatesOffset = 0;
    uint32_t candidatesLen = 0;
    uint16_t candidateCount = 0;
  };

  String basePath_;
  File records_;
  File strings_;
  uint32_t recordCount_ = 0;

  bool readRecord(uint32_t index, Record& record);
  String readString(uint32_t offset, uint32_t length);
  size_t lookupTyped(const char* typePrefix, const String& key,
                     String* outCandidates, size_t maxCandidates);
};
