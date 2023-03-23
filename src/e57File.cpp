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

  struct Context
  {
    E57File* e57File = nullptr;
    Logger logger = nullptr;
    const char* begin = nullptr;
    const char* end = nullptr;

    struct Header {
      uint32_t  major = 0;
      uint32_t  minor = 0;
      uint64_t  filePhysicalLength = 0;
      uint64_t  xmlPhysicalOffset = 0;
      uint64_t  xmlLogicalLength = 0;
      uint64_t  pageSize = 0;
    } header;

    struct Page
    {
      size_t size = 0;
      size_t logicalSize = 0;
      size_t mask = 0;
      uint8_t shift = 0;

    } page;

  };

  uint32_t readUint32LE(Context& ctx, const char*& curr)
  {
    const uint8_t* q = reinterpret_cast<const uint8_t*>(curr);
    uint32_t rv = uint32_t(q[3]) << 24 | uint32_t(q[2]) << 16 | uint32_t(q[1]) << 8 | uint32_t(q[0]);
    curr += sizeof(uint32_t);
    return rv;
  }

  uint64_t readUint64LE(Context& ctx, const char*& curr)
  {
    const uint8_t* q = reinterpret_cast<const uint8_t*>(curr);
    uint64_t rv =
      uint64_t(q[7]) << 56 | uint64_t(q[6]) << 48 | uint64_t(q[5]) << 40 | uint64_t(q[4]) << 32 |
      uint64_t(q[3]) << 24 | uint64_t(q[2]) << 16 | uint64_t(q[1]) << 8 | uint64_t(q[0]);
    curr += sizeof(uint64_t);
    return rv;
  }

  bool parseHeader(Context& ctx)
  {
    const char* curr = ctx.begin;
    if (ctx.end < curr + 8 + 2 * 4 + 4 * 8) {
      ctx.logger(2, "File smaller than e57 file header");
      return false;
    }

    if (std::memcmp("ASTM-E57", curr, 8) != 0) {
      ctx.logger(2, "Wrong file signature");
      return false;
    }
    curr += 8;

    ctx.header.major = readUint32LE(ctx, curr);
    ctx.header.minor = readUint32LE(ctx, curr);

    ctx.header.filePhysicalLength = readUint64LE(ctx, curr);
    ctx.header.xmlPhysicalOffset = readUint64LE(ctx, curr);
    ctx.header.xmlLogicalLength = readUint64LE(ctx, curr);
    ctx.header.pageSize = readUint64LE(ctx, curr);

    ctx.logger(0, "version=%" PRIu32 ".%" PRIu32 ", length=%" PRIu64 ", xmlOffset=%" PRIu64 ", xmlLength=%" PRIu64 ", pageSize=%" PRIu64,
               ctx.header.major, ctx.header.minor,
               ctx.header.filePhysicalLength,
               ctx.header.xmlPhysicalOffset, ctx.header.xmlLogicalLength,
               ctx.header.pageSize);

    if ((ctx.header.pageSize == 0) || (ctx.header.pageSize & (ctx.header.pageSize - 1)) != 0) {
      ctx.logger(2, "page size is not a power of 2");
      return false;
    }

    ctx.page.size = static_cast<size_t>(ctx.header.pageSize);
    ctx.page.logicalSize = ctx.page.size - sizeof(uint32_t);
    ctx.page.mask = ctx.page.size - 1;
    ctx.page.shift = static_cast<uint8_t>(std::countr_zero(ctx.header.pageSize));

    ctx.logger(0, "pageSize=0x%zx pageMask=0x%zx pageShift=%u", ctx.page.size, ctx.page.mask, ctx.page.shift);

    return true;
  }

  bool checkPage(Context& ctx, size_t page)
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
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(ctx.begin) + page * ctx.page.size;
    for (size_t i = 0; i < ctx.page.logicalSize; i++) {
      crc = (crc >> 8) ^ table[(crc ^ *ptr++) & 0xff];
    }
    crc ^= 0xFFFFFFFFu;

    // For some reason the CRC calc above gets endian swapped, so we read this as big endian for now...
    uint32_t crcRef = uint32_t(ptr[0]) << 24 | uint32_t(ptr[1]) << 16 | uint32_t(ptr[2]) << 8 | uint32_t(ptr[3]);
    if (crc != crcRef) {
      ctx.logger(2, "CRC error, expected 0x%8x, got 0x%8x", crcRef, crc);
      return false;
    }

    return true;
  }

  bool read(Context& ctx, char* dst, size_t physicalOffset, size_t bytesToRead)
  {
    size_t page = physicalOffset >> ctx.page.shift;
    size_t offsetInPage = physicalOffset & ctx.page.mask;
    if (ctx.page.logicalSize <= offsetInPage) {
      ctx.logger(2, "Physical offset %zu is outside page payload", physicalOffset);
      return false;
    }

    while (bytesToRead) {
      if (!checkPage(ctx, page)) {
        return false;
      }
      size_t bytesToReadFromPage = std::min(ctx.page.logicalSize - offsetInPage, bytesToRead);
      ctx.logger(0, "copy %zu bytes from page %zu", bytesToReadFromPage, page);
      std::memcpy(dst, ctx.begin + page * ctx.header.pageSize + offsetInPage, bytesToReadFromPage);
      offsetInPage = 0;

      dst += bytesToReadFromPage;
      bytesToRead -= bytesToReadFromPage;
      page++;
    }

    return true;
  }

}

void Component::initInteger() {
  type = Type::Integer;
  integer.min = std::numeric_limits<int32_t>::max();
  integer.max = std::numeric_limits<int32_t>::min();
}

void Component::initScaledInteger() {
  type = Type::ScaledInteger;
  scaledInteger.min = std::numeric_limits<int32_t>::max();
  scaledInteger.max = std::numeric_limits<int32_t>::min();
  scaledInteger.scale = 1.0;
  scaledInteger.offset = 0.0;
}

bool e57Parser(Logger logger, const char* path, const char* ptr, size_t size)
{

  Context ctx{
    .e57File = new E57File,
    .logger = logger,
    .begin = ptr,
    .end = ptr + size,
  };

  if (!parseHeader(ctx)) {
    return false;
  }


  Buffer<char> xml;
  xml.accommodate(ctx.header.xmlLogicalLength);

  if (!read(ctx, xml.data(), ctx.header.xmlPhysicalOffset, ctx.header.xmlLogicalLength)) {
    return false;
  }

#if 0
  fwrite(xml.data(), 1, ctx.header.xmlLogicalLength, stderr);
  ctx.logger(0, "----");
#endif

  if (!parseE57Xml(ctx.e57File, logger, xml.data(), ctx.header.xmlLogicalLength)) {
    return false;
  }

  return true;
}
