#pragma once
#include "Common.h"

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
    Float,
    ScaledInteger,
    Integer,
    Count
  };

  Role role;
  Type type;

  union {

    struct {
      int32_t min;
      int32_t max;
    } integer;

    struct {
      int32_t min;
      int32_t max;
      double scale;
      double offset;
    } scaledInteger;

  };

  void initInteger();
  void initScaledInteger();
};

struct Points
{
  size_t fileOffset;
  size_t recordCount;
  UninitializedView<Component> components;

  void init() { fileOffset = 0; recordCount = 0; components.init(); }
};

struct E57File
{
  View<const char> bytes; // Non-owning view of raw file bytes
  View<Points> points{};
  Arena arena;

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