#include <cstring>
#include <cstdint>
#include <cinttypes>

#include "Common.h"


namespace {

  struct Context
  {
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

    return true;
  }


}



bool e57Parser(Logger logger, const char* path, const char* ptr, size_t size)
{

  Context ctx {
    .logger = logger,
    .begin = ptr,
    .end = ptr + size
  };

  if (!parseHeader(ctx)) {
    return false;
  }

  


  return true;
}
