#include "JapaneseDictionary.h"

#include "JapaneseDeinflector.h"
#include <SD.h>
#include <cstring>
#include <new>

namespace {

String joinPath(const char* base, const char* leaf) {
  String path = base;
  if (!path.endsWith("/")) {
    path += "/";
  }
  path += leaf;
  return path;
}

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

int32_t readLeI32(const uint8_t* p) {
  return static_cast<int32_t>(readLe32(p));
}

uint32_t fnv1a32(const char* data, uint32_t seed) {
  uint32_t value = seed;
  while (*data != '\0') {
    value ^= static_cast<uint8_t>(*data);
    value *= 0x01000193UL;
    ++data;
  }
  return value;
}

}  // namespace

uint32_t firstUtf8Codepoint(const String& text) {
  if (text.length() == 0) {
    return 0;
  }

  const auto* data = reinterpret_cast<const uint8_t*>(text.c_str());
  const uint8_t first = data[0];
  if (first < 0x80) {
    return first;
  }
  if ((first & 0xE0) == 0xC0 && text.length() >= 2) {
    return ((first & 0x1F) << 6) | (data[1] & 0x3F);
  }
  if ((first & 0xF0) == 0xE0 && text.length() >= 3) {
    return ((first & 0x0F) << 12) | ((data[1] & 0x3F) << 6) |
           (data[2] & 0x3F);
  }
  if ((first & 0xF8) == 0xF0 && text.length() >= 4) {
    return ((first & 0x07) << 18) | ((data[1] & 0x3F) << 12) |
           ((data[2] & 0x3F) << 6) | (data[3] & 0x3F);
  }
  return 0;
}

bool JapaneseDictionary::open(const char* basePath) {
  close();
  basePath_ = basePath;
  buckets_ = SD.open(joinPath(basePath, "buckets.bin"), FILE_READ);
  records_ = SD.open(joinPath(basePath, "records.bin"), FILE_READ);
  strings_ = SD.open(joinPath(basePath, "strings.bin"), FILE_READ);

  if (!buckets_ || !records_ || !strings_) {
    close();
    return false;
  }
  loadKeyFilter(basePath);
  return true;
}

void JapaneseDictionary::close() {
  delete[] keyFilter_;
  keyFilter_ = nullptr;
  keyFilterBytes_ = 0;
  if (buckets_) {
    buckets_.close();
  }
  if (records_) {
    records_.close();
  }
  if (strings_) {
    strings_.close();
  }
  basePath_ = "";
}

bool JapaneseDictionary::isOpen() const {
  return buckets_ && records_ && strings_;
}

const String& JapaneseDictionary::path() const {
  return basePath_;
}

bool JapaneseDictionary::hasKeyFilter() const {
  return keyFilter_ != nullptr && keyFilterBytes_ > 0;
}

bool JapaneseDictionary::loadKeyFilter(const char* basePath) {
  File filter = SD.open(joinPath(basePath, "key_filter.bin"), FILE_READ);
  if (!filter) {
    return false;
  }
  if (filter.size() != kKeyFilterBytes) {
    filter.close();
    return false;
  }

  uint8_t* data = new (std::nothrow) uint8_t[kKeyFilterBytes];
  if (data == nullptr) {
    filter.close();
    return false;
  }

  const int read = filter.read(data, kKeyFilterBytes);
  filter.close();
  if (read != static_cast<int>(kKeyFilterBytes)) {
    delete[] data;
    return false;
  }

  keyFilter_ = data;
  keyFilterBytes_ = kKeyFilterBytes;
  return true;
}

bool JapaneseDictionary::keyMightExist(const String& key) const {
  if (!hasKeyFilter() || key.length() == 0) {
    return true;
  }

  const uint32_t bitCount = keyFilterBytes_ * 8;
  const uint32_t h1 = fnv1a32(key.c_str(), 0x811C9DC5UL);
  const uint32_t h2 = fnv1a32(key.c_str(), 0xCBF29CE4UL) | 1UL;
  for (uint8_t i = 0; i < kKeyFilterHashes; ++i) {
    const uint32_t pos =
        (static_cast<uint64_t>(h1) + static_cast<uint64_t>(i) * h2) %
        bitCount;
    if ((keyFilter_[pos >> 3] & (1U << (pos & 7))) == 0) {
      return false;
    }
  }
  return true;
}

bool JapaneseDictionary::readBucket(uint32_t codepoint, uint32_t& start,
                                    uint32_t& count) {
  if (!isOpen() || codepoint >= kUnicodeBuckets) {
    start = 0;
    count = 0;
    return false;
  }

  uint8_t data[kBucketBytes] = {};
  if (!buckets_.seek(codepoint * kBucketBytes) ||
      buckets_.read(data, sizeof(data)) != sizeof(data)) {
    start = 0;
    count = 0;
    return false;
  }

  start = readLe32(data);
  count = readLe32(data + 4);
  return true;
}

bool JapaneseDictionary::readRecord(uint32_t index, Record& record) {
  uint8_t data[kRecordBytes] = {};
  if (!isOpen() || !records_.seek(index * kRecordBytes) ||
      records_.read(data, sizeof(data)) != sizeof(data)) {
    return false;
  }

  record.keyLen = readLe16(data + 96);
  if (record.keyLen > kKeyBytes) {
    return false;
  }
  memcpy(record.key, data, record.keyLen);
  record.key[record.keyLen] = '\0';
  record.tier = data[98];
  record.flags = data[99];
  record.termOffset = readLe32(data + 100);
  record.termLen = readLe16(data + 104);
  record.readingOffset = readLe32(data + 106);
  record.readingLen = readLe16(data + 110);
  record.definitionOffset = readLe32(data + 112);
  record.definitionLen = readLe32(data + 116);
  record.score = readLeI32(data + 120);
  record.sequence = readLeI32(data + 124);
  return true;
}

String JapaneseDictionary::readString(uint32_t offset, uint32_t length) {
  if (!isOpen() || length == 0 || !strings_.seek(offset)) {
    return "";
  }

  String out;
  out.reserve(length + 1);
  constexpr size_t kChunk = 96;
  uint8_t buffer[kChunk];
  uint32_t remaining = length;

  while (remaining > 0) {
    const size_t wanted = remaining > kChunk ? kChunk : remaining;
    const int read = strings_.read(buffer, wanted);
    if (read <= 0) {
      break;
    }
    for (int i = 0; i < read; ++i) {
      out += static_cast<char>(buffer[i]);
    }
    remaining -= read;
  }
  return out;
}

bool JapaneseDictionary::lowerBoundInBucket(const String& key,
                                            uint32_t& bucketStart,
                                            uint32_t& bucketCount,
                                            uint32_t& lowerBound) {
  bucketStart = 0;
  bucketCount = 0;
  lowerBound = 0;
  if (!readBucket(firstUtf8Codepoint(key), bucketStart, bucketCount) ||
      bucketCount == 0) {
    return false;
  }

  uint32_t lo = bucketStart;
  uint32_t hi = bucketStart + bucketCount;
  Record record;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (!readRecord(mid, record)) {
      return false;
    }
    const int cmp = strcmp(record.key, key.c_str());
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  lowerBound = lo;
  return true;
}

void JapaneseDictionary::populateMatch(const Record& record,
                                       const String& sourceText,
                                       uint8_t deinflectionDepth,
                                       JapaneseDictionaryMatch& match) {
  match.key = record.key;
  match.term = readString(record.termOffset, record.termLen);
  match.terms = match.term;
  match.reading = readString(record.readingOffset, record.readingLen);
  match.definition = readString(record.definitionOffset, record.definitionLen);
  match.sourceText = sourceText;
  match.score = record.score;
  match.sequence = record.sequence;
  match.tier = record.tier;
  match.flags = record.flags;
  match.deinflectionDepth = deinflectionDepth;
  match.termCount = 1;
}

namespace {

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

bool mergeMatched(JapaneseDictionaryMatch* matches, size_t count,
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

}  // namespace

size_t JapaneseDictionary::appendExactMatches(
    const String& key, const String& sourceText, uint8_t deinflectionDepth,
    JapaneseDictionaryMatch* outMatches, size_t found, size_t maxMatches) {
  if (!isOpen() || key.length() == 0 || outMatches == nullptr) {
    return found;
  }
  if (!keyMightExist(key)) {
    return found;
  }

  uint32_t start = 0;
  uint32_t count = 0;
  uint32_t lo = 0;
  if (!lowerBoundInBucket(key, start, count, lo)) {
    return found;
  }

  for (uint32_t pos = lo; pos < start + count && found < maxMatches; ++pos) {
    Record record;
    if (!readRecord(pos, record)) {
      break;
    }
    if (strcmp(record.key, key.c_str()) != 0) {
      break;
    }

    JapaneseDictionaryMatch candidate;
    populateMatch(record, sourceText, deinflectionDepth, candidate);
    if (!mergeMatched(outMatches, found, candidate)) {
      outMatches[found++] = candidate;
    }
  }

  return found;
}

size_t JapaneseDictionary::lookupExact(const String& key,
                                       JapaneseDictionaryMatch* outMatches,
                                       size_t maxMatches) {
  if (!isOpen() || key.length() == 0 || outMatches == nullptr ||
      maxMatches == 0) {
    return 0;
  }
  return appendExactMatches(key, key, 0, outMatches, 0, maxMatches);
}

bool JapaneseDictionary::hasExact(const String& key) {
  if (!isOpen() || key.length() == 0) {
    return false;
  }
  if (!keyMightExist(key)) {
    return false;
  }

  uint32_t start = 0;
  uint32_t count = 0;
  uint32_t lo = 0;
  if (!lowerBoundInBucket(key, start, count, lo) || lo >= start + count) {
    return false;
  }

  Record record;
  return readRecord(lo, record) && strcmp(record.key, key.c_str()) == 0;
}

size_t JapaneseDictionary::appendDeinflectedMatches(
    const String& key, JapaneseDictionaryMatch* outMatches, size_t found,
    size_t maxMatches, uint8_t minDepth, uint8_t maxDepth) {
  if (!isOpen() || key.length() == 0 || outMatches == nullptr ||
      found >= maxMatches || minDepth > maxDepth) {
    return found;
  }

  const auto candidates =
      jpdict::expandDeinflections(key.c_str(), maxDepth, maxMatches * 4 + 8);
  for (const auto& candidate : candidates) {
    if (found >= maxMatches) {
      return found;
    }
    if (candidate.depth < minDepth || candidate.depth > maxDepth ||
        candidate.term == key.c_str()) {
      continue;
    }
    found = appendExactMatches(candidate.term.c_str(), key,
                               candidate.depth, outMatches, found, maxMatches);
  }
  return found;
}

size_t JapaneseDictionary::appendPrefixMatches(
    const String& key, JapaneseDictionaryMatch* outMatches, size_t found,
    size_t maxMatches, size_t maxPrefixRecords) {
  if (!isOpen() || key.length() == 0 || outMatches == nullptr ||
      found >= maxMatches || maxPrefixRecords == 0) {
    return found;
  }

  uint32_t start = 0;
  uint32_t count = 0;
  uint32_t lo = 0;
  if (!lowerBoundInBucket(key, start, count, lo)) {
    return found;
  }

  const size_t keyLen = key.length();
  size_t scanned = 0;
  for (uint32_t pos = lo;
       pos < start + count && found < maxMatches && scanned < maxPrefixRecords;
       ++pos, ++scanned) {
    Record record;
    if (!readRecord(pos, record)) {
      break;
    }
    if (strncmp(record.key, key.c_str(), keyLen) != 0) {
      break;
    }
    if (strcmp(record.key, key.c_str()) == 0) {
      continue;
    }
    JapaneseDictionaryMatch candidate;
    populateMatch(record, record.key, 0, candidate);
    if (mergeMatched(outMatches, found, candidate)) {
      continue;
    }
    outMatches[found++] = candidate;
  }

  return found;
}

size_t JapaneseDictionary::lookupExactThenPrefix(
    const String& key, JapaneseDictionaryMatch* outMatches, size_t maxMatches,
    size_t maxPrefixRecords) {
  size_t found = appendExactMatches(key, key, 0, outMatches, 0, maxMatches);
  found = appendDeinflectedMatches(key, outMatches, found, maxMatches, 1, 1);
  if (found < maxMatches) {
    found = appendDeinflectedMatches(key, outMatches, found, maxMatches, 2, 2);
  }
  return appendPrefixMatches(key, outMatches, found, maxMatches,
                             maxPrefixRecords);
}

size_t JapaneseDictionary::lookupDeinflected(const String& key,
                                             JapaneseDictionaryMatch* outMatches,
                                             size_t maxMatches) {
  size_t found =
      appendDeinflectedMatches(key, outMatches, 0, maxMatches, 1, 1);
  if (found == 0) {
    found =
        appendDeinflectedMatches(key, outMatches, found, maxMatches, 2, 2);
  }
  return found;
}

size_t JapaneseDictionary::lookupDeinflectedThenPrefix(
    const String& key, JapaneseDictionaryMatch* outMatches, size_t maxMatches,
    size_t maxPrefixRecords) {
  size_t found =
      appendDeinflectedMatches(key, outMatches, 0, maxMatches, 1, 1);
  if (found == 0) {
    found =
        appendDeinflectedMatches(key, outMatches, found, maxMatches, 2, 2);
  }
  return appendPrefixMatches(key, outMatches, found, maxMatches,
                             maxPrefixRecords);
}

size_t JapaneseDictionary::lookupPrefix(const String& key,
                                        JapaneseDictionaryMatch* outMatches,
                                        size_t maxMatches,
                                        size_t maxPrefixRecords) {
  return appendPrefixMatches(key, outMatches, 0, maxMatches, maxPrefixRecords);
}
