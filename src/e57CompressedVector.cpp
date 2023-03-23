#include "Common.h"
#include "e57File.h"

#include <cassert>


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

    struct {
      uint8_t data[0x10000]; // packet length is 16 bits.
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
      if (ctx.packet.size <= expectedPacketSize) {
        ctx.logger(2, "Expected packet size=%zu is greater than actual packet size=%zu", expectedPacketSize, ctx.packet.size);
        return false;
      }

      ctx.logger(0, "Data packet: size=%zu byteStreamCount=%zu expectedPacketSize=%zu", ctx.packet.size, bytestreamCount, expectedPacketSize);

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
    .logger = logger
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

  // Offset of first datapacket
  size_t dataPhysicalOffset = readUint64LE(ptr);
  size_t dataPhysicalOffset_ = dataPhysicalOffset;

  // Offset of first index packet
  size_t indexPhysicalOffset = readUint64LE(ptr);

  logger(2, "sectionLogicalLength=0x%zx dataPhysicalOffset=0x%zx indexPhysicalOffset=%zx",
         sectionLogicalLength, dataPhysicalOffset, indexPhysicalOffset);

  size_t logicalBytesRead = 0;
  for (size_t i = 0; i < 5; i++) {
    ctx.logger(0, "dataPhysicalOffset=0x%zx", dataPhysicalOffset);
    if (!readPacket(ctx, dataPhysicalOffset)) {
      return false;
    }
    logicalBytesRead += ctx.packet.size;
  }

  ctx.logger(0, "diff=%zu / %zu", sectionLogicalLength - logicalBytesRead, (dataPhysicalOffset - dataPhysicalOffset_) - sectionLogicalLength);

  return true;
}
