#include "KanjiIndex.h"

#include <SD.h>
#include <cstring>

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

String utf8CharAt(const String& text, int pos) {
  if (pos >= static_cast<int>(text.length())) {
    return "";
  }
  int end = pos + 1;
  while (end < static_cast<int>(text.length()) &&
         (static_cast<uint8_t>(text[end]) & 0xC0) == 0x80) {
    ++end;
  }
  return text.substring(pos, end);
}

}  // namespace

bool KanjiIndex::open(const char* basePath) {
  close();
  basePath_ = basePath;
  buckets_ = SD.open(joinPath(basePath, "buckets.bin"), FILE_READ);
  records_ = SD.open(joinPath(basePath, "records.bin"), FILE_READ);
  strings_ = SD.open(joinPath(basePath, "strings.bin"), FILE_READ);
  if (!buckets_ || !records_ || !strings_) {
    close();
    return false;
  }
  return true;
}

void KanjiIndex::close() {
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

bool KanjiIndex::isOpen() const {
  return buckets_ && records_ && strings_;
}

const String& KanjiIndex::path() const {
  return basePath_;
}

bool KanjiIndex::readBucket(uint32_t codepoint, uint32_t& start,
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

bool KanjiIndex::readRecord(uint32_t index, Record& record) {
  uint8_t data[kRecordBytes] = {};
  if (!isOpen() || !records_.seek(index * kRecordBytes) ||
      records_.read(data, sizeof(data)) != sizeof(data)) {
    return false;
  }
  record.keyLen = readLe16(data + 64);
  if (record.keyLen > kKeyBytes) {
    return false;
  }
  memcpy(record.key, data, record.keyLen);
  record.key[record.keyLen] = '\0';
  record.candidatesOffset = readLe32(data + 66);
  record.candidatesLen = readLe32(data + 70);
  record.candidateCount = readLe16(data + 74);
  return true;
}

String KanjiIndex::readString(uint32_t offset, uint32_t length) {
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

size_t KanjiIndex::lookup(const String& reading, String* outCandidates,
                          size_t maxCandidates) {
  if (!isOpen() || reading.length() == 0 || outCandidates == nullptr ||
      maxCandidates == 0) {
    return 0;
  }

  uint32_t start = 0;
  uint32_t count = 0;
  if (!readBucket(firstUtf8Codepoint(reading), start, count) || count == 0) {
    return 0;
  }

  uint32_t lo = start;
  uint32_t hi = start + count;
  Record record;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (!readRecord(mid, record)) {
      return 0;
    }
    const int cmp = strcmp(record.key, reading.c_str());
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  if (!readRecord(lo, record) || strcmp(record.key, reading.c_str()) != 0) {
    return 0;
  }

  const String packed = readString(record.candidatesOffset, record.candidatesLen);
  size_t found = 0;
  for (int pos = 0; pos < static_cast<int>(packed.length()) &&
                    found < maxCandidates;) {
    const String ch = utf8CharAt(packed, pos);
    if (ch.length() == 0) {
      break;
    }
    outCandidates[found++] = ch;
    pos += ch.length();
  }
  return found;
}
