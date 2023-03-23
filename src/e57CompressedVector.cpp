#include "Common.h"
#include "e57File.h"

#include <cassert>

bool parseE57CompressedVector(const E57File* e57, Logger logger, size_t pointsIndex)
{
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

  // Offset of first index packet
  size_t indexPhysicalOffset = readUint64LE(ptr);

  logger(2, "sectionLogicalLength=0x%zx dataPhysicalOffset=0x%zx indexPhysicalOffset=%zx",
         sectionLogicalLength, dataPhysicalOffset, indexPhysicalOffset);

  return true;
}
