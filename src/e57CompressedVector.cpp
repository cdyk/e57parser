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
    const ReadPointsArgs& args;
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

    // Decode index packet
    if (ctx.packet.type == PacketType::Index) {
      uint8_t flags = ctx.packet[1];
      size_t entryCount = getUint16LE(ctx.packet.data + 4);
      uint8_t indexLevel = ctx.packet[6];
      // Payload starts at 16, entryCount of struct { uint64_t chunkRecordNumber, chunkPhysicalOffset = 0 }
      ctx.logger(0, "Index packet: size=%zu flags=%u entryCount=%zu indexLevel=%u", ctx.packet.size, flags, entryCount, indexLevel);
    }

    // Decode data packet
    else if (ctx.packet.type == PacketType::Data) {

      if ((ctx.packet.size & 3) != 0) {
        ctx.logger(2, "Packet size=%zu is not a multiple of 4", ctx.packet.size);
        return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
      }

      ctx.dataPacket.byteStreamsCount = getUint16LE(ctx.packet.data + 4);
      if (ctx.dataPacket.byteStreamsCount == 0) {
        ctx.logger(2, "No bytestreams in packet");
        return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
      }

      uint32_t offset = 6 + 2 * ctx.dataPacket.byteStreamsCount;
      for (size_t i = 0; i < ctx.dataPacket.byteStreamsCount; i++) {
        ctx.dataPacket.byteStreamOffsets[i] = offset;
        offset += getUint16LE(ctx.packet.data + 6 + 2 * i);
        if (ctx.packet.size < offset) {
          ctx.logger(2, "Bytestream offset %u beyond packet length %u", offset, ctx.packet.size);
          return ctx.packet.nextOffset = ctx.packet.currentOffset = 0;
        }
      }
      ctx.dataPacket.byteStreamOffsets[ctx.dataPacket.byteStreamsCount] = offset;
      ctx.logger(0, "Got data packet: size=%zu byteStreamCount=%zu expectedPacketSize=%zu", ctx.packet.size, ctx.dataPacket.byteStreamsCount, offset);
    }

    else if (ctx.packet.type == PacketType::Empty) {
      ctx.logger(0, "Empty packet: size=%zu ", ctx.packet.size);
    }

    return ctx.packet.nextOffset = packetOffset;
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

  struct ComponentReadState
  {
    uint64_t packetOffset = 0;

    BitUnpackState unpackState{};
    BitUnpackDesc unpackDesc{};
  };


  BitUnpackState consumeBits(const Context& ctx, const BitUnpackState& unpackState, const BitUnpackDesc& unpackDesc, const ComponentWriteDesc& writeDesc, const Component& comp)
  {
    const size_t maxItems = unpackDesc.maxItems;
    const size_t byteStreamOffset = unpackDesc.byteStreamOffset;
    const uint32_t bitsAvailable = unpackDesc.bitsAvailable;

    uint32_t bitsConsumed = unpackState.bitsConsumed;
    size_t item = unpackState.itemsWritten;

    char* ptr = ctx.args.buffer.data + writeDesc.offset;
    char* end = ctx.args.buffer.data + ctx.args.buffer.size;
    size_t stride = writeDesc.stride;

    size_t moo = ctx.args.buffer.size;

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

        char* pptr = ptr + stride * item;
        assert(pptr + sizeof(float) <= end);
        *reinterpret_cast<float*>(pptr) = static_cast<float>(value);
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

        char* pptr = ptr + stride * item;
        assert(pptr + sizeof(float) <= end);
        *reinterpret_cast<float*>(pptr) = static_cast<float>(comp.integer.scale * static_cast<double>(value) + comp.integer.offset);
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

        char* pptr = ptr + stride * item;
        assert(pptr + sizeof(float) <= end);
        *reinterpret_cast<float*>(pptr) = static_cast<float>(value);
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

        char* pptr = ptr + stride * item;
        assert(pptr + sizeof(float) <= end);
        *reinterpret_cast<float*>(pptr) = static_cast<float>(value);
      }
    }

    assert((bitsConsumed == AllBitsRead || item != unpackState.itemsWritten) && "No progress");
    return { item,  bitsConsumed };
  }


  bool readPointsIteration(Context& ctx, View<ComponentReadState> readStates, size_t pointsToDo, uint64_t dataPhysicalOffset, uint64_t sectionPhysicalEnd)
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
        const ComponentWriteDesc& writeDesc = ctx.args.writeDesc[i];
        const uint32_t stream = writeDesc.stream;

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

            if (ctx.dataPacket.byteStreamsCount <= stream) {
              ctx.logger(2, "Stream %u not in packet", uint32_t(stream));
              return false;
            }

            // Update unpack state and desc for newly read package
            readState.unpackState.bitsConsumed = 0;
            readState.unpackDesc.byteStreamOffset = ctx.dataPacket.byteStreamOffsets[stream];
            readState.unpackDesc.bitsAvailable = 8 * (ctx.dataPacket.byteStreamOffsets[stream + 1] - readState.unpackDesc.byteStreamOffset);
          }

          BitUnpackState unpackStateNew = consumeBits(ctx, readState.unpackState, readState.unpackDesc, writeDesc, ctx.pts.components[stream]);

          assert((unpackStateNew.bitsConsumed == AllBitsRead || unpackStateNew.itemsWritten != readState.unpackState.itemsWritten) && "No progress");

          readState.unpackState = unpackStateNew;

          // Continue until we have enough items for all components
          done = done && readState.unpackState.itemsWritten < readState.unpackDesc.maxItems;
        }
      }
    } while (!done);

    return true;
  }


  bool readPoints(Context& ctx, uint64_t dataPhysicalOffset, uint64_t sectionPhysicalEnd)
  {
    std::vector<ComponentReadState> readStates_(ctx.args.writeDesc.size);

    View<ComponentReadState> readStates(readStates_.data(), ctx.args.writeDesc.size);
    for (size_t i = 0; i < ctx.args.writeDesc.size; i++) {
      readStates[i].packetOffset = dataPhysicalOffset;
      readStates[i].unpackState.bitsConsumed = AllBitsRead;
      readStates[i].unpackDesc.maxItems = 5;
    }

    size_t pointsDone = 0;
    while (pointsDone < ctx.pts.recordCount) {
      size_t pointsToDo = std::min(ctx.pts.recordCount - pointsDone,ctx.args.pointCapacity);
      if (!readPointsIteration(ctx, readStates, pointsToDo, dataPhysicalOffset, sectionPhysicalEnd)) {
        return false;
      }

      // callback to process the pointsToDo
      ctx.args.consumeCallback(ctx.args.consumeCallbackData, pointsToDo);

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


bool readE57Points(const E57File* e57, Logger logger, const ReadPointsArgs& args)
{
  Context ctx{
    .e57 = e57,
    .logger = logger,
    .args = args,
    .pts = e57->points[args.pointSetIndex]
  };


  logger(0, "Reading compressed vector %zu: fileOffset=0x%zx recordCount=0x%zx",
         args.pointSetIndex, ctx.pts.fileOffset, ctx.pts.recordCount);

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

  uint64_t fileOffset = ctx.pts.fileOffset;

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
  uint64_t sectionPhysicalEnd = calculateSectionPhysicalEnd(ctx, ctx.pts.fileOffset, sectionLogicalLength);

  // Offset of first datapacket
  uint64_t dataPhysicalOffset = readUint64LE(ptr);

  // Offset of first index packet
  uint64_t indexPhysicalOffset = readUint64LE(ptr);

  logger(0, "sectionLogicalLength=0x%zx dataPhysicalOffset=0x%zx indexPhysicalOffset=%zx sectionPhysicalEnd=0x%zx",
         sectionLogicalLength, dataPhysicalOffset, indexPhysicalOffset, sectionPhysicalEnd);

  if (!readPoints(ctx, dataPhysicalOffset, sectionPhysicalEnd)) {
    return false;
  }

  return true;
}
