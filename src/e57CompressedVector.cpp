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

  
  constexpr uint32_t AllBitsRead = ~uint32_t(0);

  struct BitUnpackState
  {
    size_t itemsWritten = 0;
    uint32_t bitsConsumed = 0;
  };

  struct BitUnpackDesc
  {
    size_t maxItems = 0;
    uint32_t byteStreamOffset = 0;
    uint32_t bitsAvailable = 0;
  };

  void consumeBits(const Context& ctx, BitUnpackState& unpackState, const BitUnpackDesc& unpackDesc, const Component& comp)
  {
    const size_t maxItems = unpackDesc.maxItems;
    const size_t byteStreamOffset = unpackDesc.byteStreamOffset;
    const uint32_t bitsAvailable = unpackDesc.bitsAvailable;

    uint32_t bitsConsumed = unpackState.bitsConsumed;
    size_t item = unpackState.itemsWritten;

    if (comp.type == Component::Type::ScaledInteger) {

      const uint8_t w = comp.integer.bitWidth;
      const uint64_t m = (uint64_t(1u) << w) - 1u;

      ctx.logger(0, "component (role=0x%x) bitwidth=%d mask=0x%x", comp.role, w, m);

      uint32_t bitsConsumedNext = bitsConsumed + w;

      for (; item < maxItems; item++) {

        if (bitsAvailable < bitsConsumedNext) {
          bitsConsumed = AllBitsRead;
          break;
        }

        uint64_t byteOffset = bitsConsumed >> 3u;
        uint64_t shift = bitsConsumed & 7u;
        uint64_t bits = (getUint64LEUnaligned(ctx.packet.data + byteStreamOffset + byteOffset) >> shift) & m;

        bitsConsumed = bitsConsumedNext;
        bitsConsumedNext += w;

        int64_t value = comp.integer.min + static_cast<int64_t>(bits);
        ctx.logger(0, "%zu: %f", item, comp.integer.scale * static_cast<double>(value) + comp.integer.offset);
      }
    }
    else if (comp.type == Component::Type::Float) {

      constexpr uint32_t w = 8 * 4;
      uint32_t bitsConsumedNext = bitsConsumed + w;

      for (; item < maxItems; item++) {

        if (bitsAvailable < bitsConsumedNext) {
          bitsConsumed = AllBitsRead;
          break;
        }

        uint64_t byteOffset = bitsConsumed >> 3u;
        float value = getFloat32LEUnaligned(ctx.packet.data + byteStreamOffset + byteOffset);

        bitsConsumed = bitsConsumedNext;
        bitsConsumedNext += w;

        ctx.logger(0, "%zu: %f", item, value);
      }
    }
    else if (comp.type == Component::Type::Double) {

      constexpr uint32_t w = 8 * 8;
      uint32_t bitsConsumedNext = bitsConsumed + w;

      for (; item < maxItems; item++) {

        if (bitsAvailable < bitsConsumedNext) {
          bitsConsumed = AllBitsRead;
          break;
        }

        uint64_t byteOffset = bitsConsumed >> 3u;
        double value = getFloat64LEUnaligned(ctx.packet.data + byteStreamOffset + byteOffset);

        bitsConsumed = bitsConsumedNext;
        bitsConsumedNext += w;

        ctx.logger(0, "%zu: %f", item, value);
      }
    }

    unpackState = { item,  bitsConsumed };
  }

  bool readPacket(Context& ctx, uint64_t& fileOffset)
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

        BitUnpackState unpackState{};

        BitUnpackDesc unpackDesc{
          .maxItems = 2,
          .byteStreamOffset = static_cast<uint32_t>(byteStreamOffset),
          .bitsAvailable = 8 * static_cast<uint32_t>(byteStreamsByteCount)
        };

        consumeBits(ctx, unpackState, unpackDesc,  ctx.pts.components[i]);

        unpackDesc.maxItems = 5;
        consumeBits(ctx, unpackState, unpackDesc, ctx.pts.components[i]);

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

  uint64_t calculateSectionLogicalEnd(Context& ctx, uint64_t fileOffset, uint64_t sectionLogicalLength)
  {
    uint64_t sectionLogicalOffset = ((fileOffset >> ctx.e57->page.shift) * ctx.e57->page.logicalSize +
                                     (fileOffset & ctx.e57->page.mask));
    return sectionLogicalOffset + sectionLogicalLength;
  }

  uint64_t calculateSectionPhysicalEnd(Context& ctx, const uint64_t fileOffset, const uint64_t sectionLogicalLength)
  {
    uint64_t sectionLogicalEnd = calculateSectionLogicalEnd(ctx, fileOffset, sectionLogicalLength);
    return ((sectionLogicalEnd / ctx.e57->page.logicalSize) * ctx.e57->page.size +
            (sectionLogicalEnd % ctx.e57->page.logicalSize));
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
  constexpr uint64_t CompressedVectorSectionHeaderSize = 8 + 3 * 8;

  uint64_t fileOffset = points.fileOffset;


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
  uint64_t sectionLogicalLength = readUint64LE(ptr); 

  // Calculate section end 
  uint64_t sectionPhysicalEnd = calculateSectionPhysicalEnd(ctx, points.fileOffset, sectionLogicalLength);

  // Offset of first datapacket
  uint64_t dataPhysicalOffset = readUint64LE(ptr);

  // Offset of first index packet
  uint64_t indexPhysicalOffset = readUint64LE(ptr);

  logger(0, "sectionLogicalLength=0x%zx dataPhysicalOffset=0x%zx indexPhysicalOffset=%zx sectionPhysicalEnd=0x%zx",
         sectionLogicalLength, dataPhysicalOffset, indexPhysicalOffset, sectionPhysicalEnd);

  while(dataPhysicalOffset < sectionPhysicalEnd) {
    if (!readPacket(ctx, dataPhysicalOffset)) {
      return false;
    }
  }
  return true;
}
