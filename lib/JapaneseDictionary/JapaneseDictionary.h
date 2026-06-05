#pragma once

#include <Arduino.h>
#include <FS.h>

struct JapaneseDictionaryMatch {
  String key;
  String term;
  String terms;
  String reading;
  String definition;
  String sourceText;
  int32_t score = 0;
  int32_t sequence = 0;
  uint8_t tier = 0;
  uint8_t flags = 0;
  uint8_t deinflectionDepth = 0;
  uint8_t termCount = 0;
};

class JapaneseDictionary {
 public:
  bool open(const char* basePath);
  void close();
  bool isOpen() const;
  const String& path() const;
  bool hasExact(const String& key);
  size_t lookupExact(const String& key, JapaneseDictionaryMatch* outMatches,
                     size_t maxMatches);
  size_t lookupExactThenPrefix(const String& key,
                               JapaneseDictionaryMatch* outMatches,
                               size_t maxMatches,
                               size_t maxPrefixRecords = 48);
  size_t lookupDeinflected(const String& key,
                           JapaneseDictionaryMatch* outMatches,
                           size_t maxMatches);
  size_t lookupDeinflectedThenPrefix(const String& key,
                                     JapaneseDictionaryMatch* outMatches,
                                     size_t maxMatches,
                                     size_t maxPrefixRecords = 48);
  size_t lookupPrefix(const String& key, JapaneseDictionaryMatch* outMatches,
                      size_t maxMatches, size_t maxPrefixRecords = 48);

 private:
  static constexpr uint32_t kUnicodeBuckets = 0x110000;
  static constexpr size_t kBucketBytes = 8;
  static constexpr size_t kKeyBytes = 96;
  static constexpr size_t kRecordBytes = 128;
  static constexpr size_t kKeyFilterBytes = 128 * 1024;
  static constexpr uint8_t kKeyFilterHashes = 2;

  struct Record {
    char key[kKeyBytes + 1] = {};
    uint16_t keyLen = 0;
    uint8_t tier = 0;
    uint8_t flags = 0;
    uint32_t termOffset = 0;
    uint16_t termLen = 0;
    uint32_t readingOffset = 0;
    uint16_t readingLen = 0;
    uint32_t definitionOffset = 0;
    uint32_t definitionLen = 0;
    int32_t score = 0;
    int32_t sequence = 0;
  };

  String basePath_;
  File buckets_;
  File records_;
  File strings_;
  uint8_t* keyFilter_ = nullptr;
  size_t keyFilterBytes_ = 0;

  bool hasKeyFilter() const;
  bool loadKeyFilter(const char* basePath);
  bool keyMightExist(const String& key) const;
  bool readBucket(uint32_t codepoint, uint32_t& start, uint32_t& count);
  bool readRecord(uint32_t index, Record& record);
  bool lowerBoundInBucket(const String& key, uint32_t& bucketStart,
                          uint32_t& bucketCount, uint32_t& lowerBound);
  void populateMatch(const Record& record, const String& sourceText,
                     uint8_t deinflectionDepth,
                     JapaneseDictionaryMatch& match);
  size_t appendExactMatches(const String& key, const String& sourceText,
                            uint8_t deinflectionDepth,
                            JapaneseDictionaryMatch* outMatches, size_t found,
                            size_t maxMatches);
  size_t appendDeinflectedMatches(const String& key,
                                  JapaneseDictionaryMatch* outMatches,
                                  size_t found, size_t maxMatches,
                                  uint8_t minDepth, uint8_t maxDepth);
  size_t appendPrefixMatches(const String& key,
                             JapaneseDictionaryMatch* outMatches, size_t found,
                             size_t maxMatches, size_t maxPrefixRecords);
  String readString(uint32_t offset, uint32_t length);
};

uint32_t firstUtf8Codepoint(const String& text);
