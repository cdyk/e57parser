#pragma once
#include "Common.h"

// Readback callback. 
//
// Returns a view of the file. The returned view will ony be accessed before the next
// invocation of the callback, that is, the callback can reuse an internal buffer.
typedef View<const char>(*ReadCallback)(void* callbackData, uint64_t offset, uint64_t size);

struct Component
{
  enum struct Role : uint32_t {
    CartesianX,
    CartesianY,
    CartesianZ,
    SphericalRange,
    SphericalAzimuth,
    SphericalElevation,
    RowIndex,
    ColumnIndex,
    ReturnCount,
    ReturnIndex,
    TimeStamp,
    Intensity,
    ColorRed,
    ColorGreen,
    ColorBlue,
    CartesianInvalidState,
    SphericalInvalidState,
    IsTimeStampInvalid,
    IsIntensityInvalid,
    IsColorInvalid,
    Count
  };

  enum struct Type : uint32_t {
    None,
    Float,
    Double,
    Integer,
    ScaledInteger,
    Count
  };

  Role role;
  Type type;

  union {

    // Integer, scaled integer
    struct {
      int64_t min;
      int64_t max;
      double scale;
      double offset;
      uint8_t bitWidth;
    } integer;

    // Float, double
    struct {
      double min;
      double max;
    } real;

  };

  void initInteger(Type type);
  void initReal(Type type);
};

struct Points
{
  uint64_t fileOffset;
  uint64_t recordCount;
  UninitializedView<Component> components;

  void init() { fileOffset = 0; recordCount = 0; components.init(); }
};

struct E57File
{
  ReadCallback fileRead = nullptr;
  void* fileReadData = nullptr;
  uint64_t fileSize = 0;

  View<Points> points{};
  Arena arena;

  bool ready = false;

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




bool openE57(E57File& e57, Logger logger, ReadCallback fileRead, void* fileReadData, uint64_t fileSize);

bool readE57Bytes(const E57File* e57, Logger logger, void* dst, uint64_t& physicalOffset, uint64_t bytesToRead);
bool parseE57Xml(E57File* e57File, Logger logger, const char* xmlBytes, size_t xmlLength);
bool parseE57CompressedVector(const E57File* e57File, Logger logger, size_t pointsIndex);
