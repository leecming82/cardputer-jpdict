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
  records_ = SD.open(joinPath(basePath, "lookup.records.bin"), FILE_READ);
  strings_ = SD.open(joinPath(basePath, "lookup.strings.bin"), FILE_READ);
  if (!records_ || !strings_) {
    close();
    return false;
  }
  recordCount_ = records_.size() / kRecordBytes;
  return true;
}

void KanjiIndex::close() {
  if (records_) {
    records_.close();
  }
  if (strings_) {
    strings_.close();
  }
  recordCount_ = 0;
  basePath_ = "";
}

bool KanjiIndex::isOpen() const {
  return records_ && strings_;
}

const String& KanjiIndex::path() const {
  return basePath_;
}

bool KanjiIndex::hasRadicalIndex() const {
  return isOpen();
}

bool KanjiIndex::readRecord(uint32_t index, Record& record) {
  uint8_t data[kRecordBytes] = {};
  if (!records_ || !records_.seek(index * kRecordBytes) ||
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
  if (!strings_ || length == 0 || !strings_.seek(offset)) {
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
  return lookupTyped("r:", reading, outCandidates, maxCandidates);
}

size_t KanjiIndex::lookupRadicalAliases(const String& alias,
                                        String* outComponents,
                                        size_t maxComponents) {
  return lookupTyped("a:", alias, outComponents, maxComponents);
}

size_t KanjiIndex::lookupRadicalsByStroke(const String& strokes,
                                          String* outComponents,
                                          size_t maxComponents) {
  return lookupTyped("s:", strokes, outComponents, maxComponents);
}

size_t KanjiIndex::lookupKanjiByStroke(const String& strokes,
                                       String* outCandidates,
                                       size_t maxCandidates) {
  return lookupTyped("k:", strokes, outCandidates, maxCandidates);
}

size_t KanjiIndex::lookupComponentKanji(const String& component,
                                        String* outCandidates,
                                        size_t maxCandidates) {
  return lookupTyped("c:", component, outCandidates, maxCandidates);
}

size_t KanjiIndex::lookupTyped(const char* typePrefix, const String& key,
                               String* outCandidates, size_t maxCandidates) {
  if (!isOpen() || typePrefix == nullptr || key.length() == 0 ||
      outCandidates == nullptr || recordCount_ == 0 ||
      maxCandidates == 0) {
    return 0;
  }

  String typedKey = typePrefix;
  typedKey += key;

  uint32_t lo = 0;
  uint32_t hi = recordCount_;
  Record record;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (!readRecord(mid, record)) {
      return 0;
    }
    const int cmp = strcmp(record.key, typedKey.c_str());
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  if (lo >= recordCount_ || !readRecord(lo, record) ||
      strcmp(record.key, typedKey.c_str()) != 0) {
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
