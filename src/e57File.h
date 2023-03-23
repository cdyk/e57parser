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
  View<Points> points{};
  Arena arena;
};