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

};

struct Points
{
  size_t fileOffset = 0;
  size_t pointCount = 0;
  View<Component> components{};
  size_t componentCount = 0;
};

struct E57File
{
  View<Points> points{};
  Arena arena;
};