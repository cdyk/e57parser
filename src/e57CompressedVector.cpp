#include "Common.h"
#include "e57File.h"

#include <bit>
#include <cassert>
#include <cstring>
#include <vector>

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
      uint64_t currentOffset = 0u; // Current packet offset
      uint64_t nextOffset = 0u;    // Next packet
      uint8_t data[0x10000 + 8]; // packet length is 16 bits. Include extra 8 bytes so it is safe to do a 64-bit unaligned fetch at end.
      size_t size = 0;
      PacketType type = PacketType::Empty;
      const uint8_t operator[](size_t ix) const{ return data[ix]; }
    } packet;

    // Decoded data-packet fields
    struct {
      uint16_t byteStreamsCount = 0; // Cannot be more than 0xFFFF streams
      uint16_t pad;
      uint32_t byteStreamOffsets[0x10000]; // Packet length is 16 bytes, and some are counts etc, so offsets cannot be more than 16 bits.
    } dataPacket;

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

  BitUnpackState consumeBits(const Context& ctx, const BitUnpackState& unpackState, const BitUnpackDesc& unpackDesc, const Component& comp)
  {
    const size_t maxItems = unpackDesc.maxItems;
    const size_t byteStreamOffset = unpackDesc.byteStreamOffset;
    const uint32_t bitsAvailable = unpackDesc.bitsAvailable;

    uint32_t bitsConsumed = unpackState.bitsConsumed;
    size_t item = unpackState.itemsWritten;

    if (comp.type == Component::Type::Integer) {
      const uint8_t w = comp.integer.bitWidth;
      const uint64_t m = (uint64_t(1u) << w) - 1u;
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
        ctx.logger(0, "%zu: %llu" , item, value);
      }
    }
    else if (comp.type == Component::Type::ScaledInteger) {
      const uint8_t w = comp.integer.bitWidth;
      const uint64_t m = (uint64_t(1u) << w) - 1u;
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

    assert((bitsConsumed == AllBitsRead || item != unpackState.itemsWritten) && "No progress");
    return { item,  bitsConsumed };
  }

  bool checkCurrentPacket(Context& ctx)
  {
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

    return checkCurrentPacket(ctx);
  }

  uint64_t getPacket(Context& ctx, uint64_t packetOffset, PacketType expectedPacketType)
  {
    // Check if we already have read this packet.
    if(ctx.packet.currentOffset == packetOffset) {
      return ctx.packet.nextOffset;
    }

    // Read packet
    ctx.packet.currentOffset = packetOffset;

    // Read 4 - byte sized packet header
    if (!readE57Bytes(ctx.e57, ctx.logger, ctx.packet.data, packetOffset, 4)) {
      return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
    }
    ctx.packet.type = static_cast<PacketType>(ctx.packet[0]);
    ctx.packet.size = size_t(ctx.packet[2]) + (size_t(ctx.packet[3]) << 8) + 1;
    if (ctx.packet.size < 4) {
      ctx.logger(2, "Packet size %zu is less than header size (=4)", ctx.packet.size);
      return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
    }

    // Check if packet is the expected type
    if (ctx.packet.type != expectedPacketType) {
      ctx.logger(2, "Unexpected packet type, expected 0x%x but got 0x%x", uint32_t(expectedPacketType), uint32_t(ctx.packet.type));
      return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
    }

    // Read rest of packet
    if (!readE57Bytes(ctx.e57, ctx.logger, ctx.packet.data + 4, packetOffset, ctx.packet.size - 4)) {
      return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
    }

    // Decode data packet
    if (ctx.packet.type == PacketType::Data) {
      ctx.dataPacket.byteStreamsCount = getUint16LE(ctx.packet.data + 4);
      uint32_t offset = 6 + 2 * ctx.dataPacket.byteStreamsCount;
      for (size_t i = 0; i < ctx.dataPacket.byteStreamsCount; i++) {
        ctx.dataPacket.byteStreamOffsets[i] = offset;
        offset += getUint16LE(ctx.packet.data + 6 + 2 * i);
        if (ctx.packet.size < offset) {
          ctx.logger(2, "Bytestream offset %u beyond packet length %u", offset, ctx.packet.size);
          return false;
        }
      }
      ctx.dataPacket.byteStreamOffsets[ctx.dataPacket.byteStreamsCount] = offset;
    }

    if (!checkCurrentPacket(ctx)) {
      return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
    }

    return ctx.packet.nextOffset = packetOffset;
  }


  struct ComponentReadState
  {
    uint64_t packetOffset = 0;

    BitUnpackState unpackState{};
    BitUnpackDesc unpackDesc{};

    uint32_t stream = 0;
  };


  bool readPointsIteration(Context& ctx, View<ComponentReadState> readStates, const Points& pts, size_t pointsToDo, uint64_t dataPhysicalOffset, uint64_t sectionPhysicalEnd)
  {
    // Initialize items written for this round
    for (size_t i = 0; i < readStates.size; i++) {
      readStates[i].unpackState.itemsWritten = 0;
      readStates[i].unpackDesc.maxItems = pointsToDo;
    }


    bool done;
    do {

      done = true;
      for (size_t i = 0; i < readStates.size; i++) {
        ComponentReadState& readState = readStates[i];

        // Skip components where we have enough items
        if (readState.unpackState.itemsWritten < readState.unpackDesc.maxItems) {

          // Fetch packet if we have no bits ready
          if (readState.unpackState.bitsConsumed == AllBitsRead) {

            if (sectionPhysicalEnd <= readState.packetOffset) {
              ctx.logger(2, "Premature end of section when reading compressed vector");
              return false;
            }
            readState.packetOffset = getPacket(ctx, readState.packetOffset, PacketType::Data);
            if (readState.packetOffset == 0) return false;

            if (ctx.dataPacket.byteStreamsCount <= readState.stream) {
              ctx.logger(2, "Stream %u not in packet", uint32_t(readState.stream));
              return false;
            }

            // Update unpack state and desc for newly read package
            readState.unpackState.bitsConsumed = 0;
            readState.unpackDesc.byteStreamOffset = ctx.dataPacket.byteStreamOffsets[readState.stream];
            readState.unpackDesc.bitsAvailable = 8 * (ctx.dataPacket.byteStreamOffsets[readState.stream + 1] - readState.unpackDesc.byteStreamOffset);
          }

          BitUnpackState unpackStateNew = consumeBits(ctx, readState.unpackState, readState.unpackDesc, pts.components[readState.stream]);

          assert((unpackStateNew.bitsConsumed == AllBitsRead || unpackStateNew.itemsWritten != readState.unpackState.itemsWritten) && "No progress");

          readState.unpackState = unpackStateNew;

          // Continue until we have enough items for all components
          done = done && readState.unpackState.itemsWritten < readState.unpackDesc.maxItems;
        }
      }
    } while (!done);

    return true;
  }


  bool readPoints(Context& ctx,  const Points& pts, uint64_t dataPhysicalOffset, uint64_t sectionPhysicalEnd)
  {
    size_t M = 5; // number of points in one round 

    std::vector<ComponentReadState> readStates_(pts.components.size);

    View<ComponentReadState> readStates(readStates_.data(), pts.components.size);
 
    for(size_t i=0; i<readStates.size; i++) {
      readStates[i].packetOffset = dataPhysicalOffset;
      readStates[i].stream = static_cast<uint32_t>(i);
      readStates[i].unpackState.bitsConsumed = AllBitsRead;
      readStates[i].unpackDesc.maxItems = 5;
    }

    size_t pointsDone = 0;
    while (pointsDone < pts.recordCount) {
      size_t pointsToDo = std::min(pts.recordCount - pointsDone, M);
      if (!readPointsIteration(ctx, readStates, pts, pointsToDo, dataPhysicalOffset, sectionPhysicalEnd)) {
        return false;
      }

      // callback to process the pointsToDo

      pointsDone += pointsToDo;
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

  // CompressedVectorSectionHeader:
  // -----------------------------
  // 
  //   0x00  uint8_t      Section id: 1 = compressed vector section
  //   0x01  uint8_t[7]   Reserved, must be zero.
  //   0x08  uint64_t     Section logical length, byte length
  //   0x10  uint64_t     Data physical offset, offset of first data packet.
  //   0x18  uint64_t     Index physical offset, offset of first index packet.
  //   0x20               Header size.


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

  if (!readPoints(ctx, points, dataPhysicalOffset, sectionPhysicalEnd)) {
    return false;
  }

  while(dataPhysicalOffset < sectionPhysicalEnd) {
    if (!readPacket(ctx, dataPhysicalOffset)) {
      return false;
    }
  }
  return true;
}
