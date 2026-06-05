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

 private:
  static constexpr uint32_t kUnicodeBuckets = 0x110000;
  static constexpr size_t kBucketBytes = 8;
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
  File buckets_;
  File records_;
  File strings_;

  bool readBucket(uint32_t codepoint, uint32_t& start, uint32_t& count);
  bool readRecord(uint32_t index, Record& record);
  String readString(uint32_t offset, uint32_t length);
};
