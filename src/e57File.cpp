// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#include <bit>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <algorithm>
#include <cassert>

#include "Common.h"
#include "e57File.h"
#include "cd_xml.h"


namespace {

  uint32_t readUint32LE(const char*& curr)
  {
    const uint8_t* q = reinterpret_cast<const uint8_t*>(curr);
    uint32_t rv = uint32_t(q[3]) << 24 | uint32_t(q[2]) << 16 | uint32_t(q[1]) << 8 | uint32_t(q[0]);
    curr += sizeof(uint32_t);
    return rv;
  }

  uint64_t readUint64LE(const char*& curr)
  {
    const uint8_t* q = reinterpret_cast<const uint8_t*>(curr);
    uint64_t rv =
      uint64_t(q[7]) << 56 | uint64_t(q[6]) << 48 | uint64_t(q[5]) << 40 | uint64_t(q[4]) << 32 |
      uint64_t(q[3]) << 24 | uint64_t(q[2]) << 16 | uint64_t(q[1]) << 8 | uint64_t(q[0]);
    curr += sizeof(uint64_t);
    return rv;
  }

  View<const char> e57Read(const E57File* e57, Logger logger, uint64_t offset, uint64_t size)
  {
    View<const char> rv = e57->fileRead(e57->fileReadData, offset, size);
    if (rv.size != size) {
      logError(logger, "File read error, offset=%" PRIu64 ", size=%" PRIu64, offset, size);
      rv.data = nullptr;
      rv.size = 0;
    }
    return rv;
  }

  bool parseHeader(E57File* e57, Logger logger)
  {
    const size_t headerSize = 8 + 2 * 4 + 4 * 8;
    if (e57->fileSize < headerSize) {
      logError(logger, "File smaller than e57 file header");
      return false;
    }

    View<const char> bytes = e57Read(e57, logger, 0, headerSize);
    if (!bytes.size) {
      return false;
    }

    const char* curr = bytes.data;

    if (std::memcmp("ASTM-E57", curr, 8) != 0) {
      logError(logger, "Wrong file signature");
      return false;
    }
    curr += 8;

    e57->header.major = readUint32LE(curr);
    e57->header.minor = readUint32LE(curr);

    e57->header.filePhysicalLength = readUint64LE(curr);
    e57->header.xmlPhysicalOffset = readUint64LE(curr);
    e57->header.xmlLogicalLength = readUint64LE(curr);
    e57->header.pageSize = readUint64LE(curr);

    if ((e57->header.pageSize == 0) || (e57->header.pageSize & (e57->header.pageSize - 1)) != 0) {
      logError(logger, "page size is not a power of 2");
      return false;
    }

    e57->page.size = static_cast<size_t>(e57->header.pageSize);
    e57->page.logicalSize = e57->page.size - sizeof(uint32_t);
    e57->page.mask = e57->page.size - 1;
    e57->page.shift = static_cast<uint8_t>(std::countr_zero(e57->header.pageSize));

    logDebug(logger, "pageSize=0x%zx pageMask=0x%zx pageShift=%u", e57->page.size, e57->page.mask, e57->page.shift);

    return true;
  }

  bool checkPage(const E57File* e57, Logger logger, const View<const char>& bytes)
  {
    static bool first = true;
    static uint32_t table[256];
    if (first) {
      const uint32_t polynomial = 0x82f63b78; // reflected 0x1EDC6F41
      for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (size_t k = 0; k < 8; k++) {
          if (c & 1) {
            c = polynomial ^ (c >> 1);
          }
          else {
            c = c >> 1;
          }
        }
        table[n] = c;
      }
    }

    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(bytes.data);
    for (size_t i = 0; i < e57->page.logicalSize; i++) {
      crc = (crc >> 8) ^ table[(crc ^ *ptr++) & 0xff];
    }
    crc ^= 0xFFFFFFFFu;

    // For some reason the CRC calc above gets endian swapped, so we read this as big endian for now...
    uint32_t crcRef = uint32_t(ptr[0]) << 24 | uint32_t(ptr[1]) << 16 | uint32_t(ptr[2]) << 8 | uint32_t(ptr[3]);
    if (crc != crcRef) {
      logError(logger, "CRC error, expected 0x%8x, got 0x%8x", crcRef, crc);
      return false;
    }

    return true;
  }

}

bool readE57Bytes(const E57File* e57, Logger logger, void* dst_, uint64_t& physicalOffset, uint64_t bytesToRead)
{
  size_t page = physicalOffset >> e57->page.shift;
  size_t offsetInPage = physicalOffset & e57->page.mask;
  if (e57->page.logicalSize <= offsetInPage) {
    logError(logger, "Physical offset %zu is outside page payload", physicalOffset);
    return false;
  }

  char* dst = static_cast<char*>(dst_);
  while (bytesToRead) {

    View<const char> pageBytes = e57Read(e57, logger, page * e57->page.size, e57->page.size);
    if (!checkPage(e57, logger, pageBytes)) {
      return false;
    }
    size_t bytesToReadFromPage = std::min(e57->page.logicalSize - offsetInPage, bytesToRead);
#if 0
    logDebug(logger, "copy %zu bytes from page %zu", bytesToReadFromPage, page);
#endif
    std::memcpy(dst, pageBytes.data + offsetInPage, bytesToReadFromPage);
    physicalOffset = page * e57->header.pageSize + offsetInPage + bytesToReadFromPage;
    offsetInPage = 0;

    dst += bytesToReadFromPage;
    bytesToRead -= bytesToReadFromPage;
    page++;
  }

  // If we end reading precisely on the end of a page payload before checksum,
  // bump offset past the checksum so we resume on a valid physical offset.
  if ((physicalOffset & e57->page.mask) == e57->page.logicalSize) {
    physicalOffset += 4;
  }

  return true;
}


void Component::initInteger(Type type_) {
  type = type_;
  integer.min = std::numeric_limits<int64_t>::max();
  integer.max = std::numeric_limits<int64_t>::min();
  integer.scale = 1.0;
  integer.offset = 0.0;
}

void Component::initReal(Type type_) {
  type = type_;
  real.min = std::numeric_limits<double>::max();
  real.max = std::numeric_limits<double>::min();
}

bool openE57(E57File& e57, Logger logger, ReadCallback fileRead, void* fileReadData, uint64_t fileSize)
{
  if (e57.ready) {
    logError(logger, "E57 file object already open");
    return false;
  }

  e57.fileRead = fileRead;
  e57.fileReadData = fileReadData;
  e57.fileSize = fileSize;

  if (!parseHeader(&e57, logger)) {
    return false;
  }


  Buffer<char> xml;
  xml.accommodate(e57.header.xmlLogicalLength);

  uint64_t xmlPhysicalOffset = e57.header.xmlPhysicalOffset;
  if (!readE57Bytes(&e57, logger, xml.data(), xmlPhysicalOffset, e57.header.xmlLogicalLength)) {
    return false;
  }


  if (!parseE57Xml(&e57, logger, xml.data(), e57.header.xmlLogicalLength)) {
    return false;
  }

  e57.ready = true;
  return true;
}
