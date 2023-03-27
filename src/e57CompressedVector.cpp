#include "Common.h"
#include "e57File.h"

#include <bit>
#include <cassert>
#include <cstring>

namespace {

  enum struct PacketType : uint8_t {
    Index = 0,
    Data,
    Empty
  };

  struct Context
  {
    const E57File* e57 = nullptr;
    Logger logger = nullptr;
    const Points& pts;

    struct {
      uint8_t data[0x10000 + 8]; // packet length is 16 bits. Include extra 8 bytes so it is safe to do a 64-bit unaligned fetch at end.
      size_t size = 0;
      PacketType type = PacketType::Empty;
      const uint8_t operator[](size_t ix) const{ return data[ix]; }
    } packet;

  };

  inline uint16_t readUint16LE(const uint16_t*& curr)
  {
    const uint8_t* q = reinterpret_cast<const uint8_t*>(curr);
    uint16_t rv = uint16_t(uint16_t(q[1]) << 8 | uint16_t(q[0]));
    curr += sizeof(uint16_t);
    return rv;
  }

  uint64_t getUint64LEUnaligned(const uint8_t* ptr)
  {
    static_assert(std::endian::native == std::endian::little);
#ifdef _MSC_VER
    return *reinterpret_cast<__unaligned const uint64_t*>(ptr);
#else
    uint64_t rv;
    std::memcpy(&rv, ptr, sizeof(rv));
    return rv;
#endif
  }

  float getFloat32LEUnaligned(const uint8_t* ptr) {
    static_assert(std::endian::native == std::endian::little);
#ifdef _MSC_VER
    return *reinterpret_cast<__unaligned const float*>(ptr);
#else
    float rv;
    std::memcpy(&rv, ptr, sizeof(rv));
    return rv;
#endif
  }

  double getFloat64LEUnaligned(const uint8_t* ptr) {
    static_assert(std::endian::native == std::endian::little);
#ifdef _MSC_VER
    return *reinterpret_cast<__unaligned const double*>(ptr);
#else
    double rv;
    std::memcpy(&rv, ptr, sizeof(rv));
    return rv;
#endif
  }

  bool processByteStream(Context& ctx, const Component& comp, size_t offset, size_t count)
  {
    if (comp.type == Component::Type::ScaledInteger) {

      int64_t diff = comp.integer.max - comp.integer.min;
      assert(0 <= diff);

      int w = std::bit_width(static_cast<uint64_t>(diff));

      uint64_t m = (uint64_t(1u) << w) - 1u;
      size_t bitOffset = 0;

      ctx.logger(0, "component (role=0x%x) bitwidth=%d mask=0x%x", comp.role, w, m);
      for (size_t i = 0; i < 5; i++) {
        size_t byteOffset = bitOffset >> 3u;
        size_t shift = bitOffset & 7u;

        uint64_t bits = (getUint64LEUnaligned(ctx.packet.data + offset + byteOffset) >> shift) & m;
        bitOffset += w;

        int64_t value = comp.integer.min + static_cast<int64_t>(bits);

        ctx.logger(0, "%zu: %f", i, comp.integer.scale * static_cast<double>(value) + comp.integer.offset);
      }

    }

    else if (comp.type == Component::Type::Float) {
      size_t byteOffset = 0;
      ctx.logger(0, "component (role=0x%x) float32");
      for (size_t i = 0; i < 5; i++) {
        float value = getFloat32LEUnaligned(ctx.packet.data + offset + byteOffset);
        byteOffset += 4;
        ctx.logger(0, "%zu: %f", i, value);
      }
    }

    else if (comp.type == Component::Type::Double) {
      size_t byteOffset = 0;
      ctx.logger(0, "component (role=0x%x) double");
      for (size_t i = 0; i < 5; i++) {
        double value = getFloat64LEUnaligned(ctx.packet.data + offset + byteOffset);
        byteOffset += 8;
        ctx.logger(0, "%zu: %f", i, value);
      }
    }


    return true;
  }

  bool readPacket(Context& ctx, size_t& fileOffset)
  {
    // Read 4-byte sized packet header
    if (!readE57Bytes(ctx.e57, ctx.logger, ctx.packet.data, fileOffset, 4)) {
      return false;
    }
    ctx.packet.type = static_cast<PacketType>(ctx.packet[0]);
    ctx.packet.size = size_t(ctx.packet[2]) + (size_t(ctx.packet[3]) << 8) + 1;
    if (ctx.packet.size < 4) {
      ctx.logger(2, "Packet size %zu is less than header size (=4)", ctx.packet.size);
      return false;
    }

    // Read rest of packet
    if (!readE57Bytes(ctx.e57, ctx.logger, ctx.packet.data + 4, fileOffset, ctx.packet.size - 4)) {
      return false;
    }

    switch (ctx.packet.type)
    {
    case PacketType::Index: {
      uint8_t flags = ctx.packet[1];
      size_t entryCount = getUint16LE(ctx.packet.data + 4);
      uint8_t indexLevel = ctx.packet[6];
      // Payload starts at 16, entryCount of struct { uint64_t chunkRecordNumber, chunkPhysicalOffset = 0 }

      ctx.logger(0, "Index packet: size=%zu flags=%u entryCount=%zu indexLevel=%u", ctx.packet.size, flags, entryCount, indexLevel);
      break;
    }
    case PacketType::Data: {
      uint8_t flags = ctx.packet[1];
      size_t bytestreamCount = getUint16LE(ctx.packet.data + 4);
      // Payload starts at 6

      if ((ctx.packet.size % 4) != 0) {
        ctx.logger(2, "Packet size=%zu is not a multiple of 4", ctx.packet.size);
        return false;
      }
      if (bytestreamCount == 0) {
        ctx.logger(2, "No bytestreams in packet");
        return false;
      }
      if (bytestreamCount != ctx.pts.components.size) {
        ctx.logger(2, "Packet got %zu bytes streams, but points has %zu components.", bytestreamCount, ctx.pts.components);
        return false;
      }

      size_t byteStreamsByteCount = 0;
      for (size_t i = 0; i < bytestreamCount; i++) {
        size_t byteStreamByteCount = getUint16LE(ctx.packet.data + 6 + 2 * i);
        byteStreamsByteCount += byteStreamByteCount;
      }

      // Byte streams are packed as:
      // - array with per-stream 16 byte lengths
      // - each stream after each other
      // - size should be less than packet size, since packet size must be mul of 4, we might get a bit padding.
      size_t expectedPacketSize = 6 + 2 * bytestreamCount + byteStreamsByteCount;
      if (ctx.packet.size < expectedPacketSize) {
        ctx.logger(2, "Expected packet size=%zu is greater than actual packet size=%zu", expectedPacketSize, ctx.packet.size);
        return false;
      }

      size_t byteStreamOffset = 6 + 2 * bytestreamCount;
      for (size_t i = 0; i < bytestreamCount; i++) {
        size_t byteStreamByteCount = getUint16LE(ctx.packet.data + 6 + 2 * i);
        size_t byteStreamEnd = byteStreamOffset + byteStreamByteCount;

        if (ctx.packet.size < byteStreamEnd) {
          ctx.logger(2, "Bytestream %zu spans outside packet size", i);
          return false;
        }

        if (!processByteStream(ctx, ctx.pts.components[i], byteStreamOffset, byteStreamsByteCount)) {
          return false;
        }

        byteStreamOffset = byteStreamEnd;
      }

      ctx.logger(0, "Got data packet: size=%zu byteStreamCount=%zu expectedPacketSize=%zu", ctx.packet.size, bytestreamCount, expectedPacketSize);

      break;
    }
    case PacketType::Empty: {
      ctx.logger(0, "Empty packet: size=%zu ", ctx.packet.size);
      break;
    }
    default:
      ctx.logger(2, "Unrecognized packet type 0x%x", unsigned(ctx.packet.type));
      return false;
    }



    return true;
  }



}


bool parseE57CompressedVector(const E57File* e57, Logger logger, size_t pointsIndex)
{
  Context ctx{
    .e57 = e57,
    .logger = logger,
    .pts = e57->points[pointsIndex]
  };


  const Points& points = e57->points[pointsIndex];
  logger(0, "Reading compressed vector: fileOffset=0x%zx recordCount=0x%zx",
         points.fileOffset, points.recordCount);

  constexpr uint8_t CompressedVectorSectionId = 1;
  constexpr size_t CompressedVectorSectionHeaderSize = 8 + 3 * 8;

  size_t fileOffset = points.fileOffset;


  Buffer<char> buf;
  buf.accommodate(CompressedVectorSectionHeaderSize);
  if (!readE57Bytes(e57, logger, buf.data(), fileOffset, CompressedVectorSectionHeaderSize)) {
    return false;
  }
  

  const char* ptr = buf.data();
  if (uint8_t sectionId = static_cast<uint8_t>(*ptr); sectionId != CompressedVectorSectionId) {
    logger(2, "Expected section id 0x%x, got 0x%x", CompressedVectorSectionId, sectionId);
    return false;
  }
  ptr += 8;

  // Bytelength of whole section
  size_t sectionLogicalLength = readUint64LE(ptr); 

  // Calculate section end 
  size_t sectionPhysicalEnd = 0;
  {
    size_t sectionLogicalOffset = ((points.fileOffset >> e57->page.shift) * e57->page.logicalSize +
                                   (points.fileOffset & e57->page.mask));

    size_t sectionLogicalEnd = sectionLogicalOffset + sectionLogicalLength;

    sectionPhysicalEnd = ((sectionLogicalEnd / e57->page.logicalSize) * e57->page.size +
                          (sectionLogicalEnd % e57->page.logicalSize));
  }

  // Offset of first datapacket
  size_t dataPhysicalOffset = readUint64LE(ptr);

  // Offset of first index packet
  size_t indexPhysicalOffset = readUint64LE(ptr);

  logger(0, "sectionLogicalLength=0x%zx dataPhysicalOffset=0x%zx indexPhysicalOffset=%zx sectionPhysicalEnd=0x%zx",
         sectionLogicalLength, dataPhysicalOffset, indexPhysicalOffset, sectionPhysicalEnd);

  while(dataPhysicalOffset < sectionPhysicalEnd) {
    if (!readPacket(ctx, dataPhysicalOffset)) {
      return false;
    }
  }
  return true;
}
