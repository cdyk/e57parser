// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#include "Common.h"
#include "e57File.h"
#include "cd_xml.h"

#include <cassert>
#include <vector>
#include <string>

namespace {

  struct CartesianBoundsData {
    float xMin;
    float xMax;
    float yMin;
    float yMax;
    float zMin;
    float zMax;
  };

  struct Element
  {
    struct Element* next = nullptr;
    enum struct Kind : uint32_t {
      Unknown,
      E57Root,
      Data3D,
      VectorChild,
      Name,
      CartesianBounds,
      XMin,
      XMax,
      YMin,
      YMax,
      ZMin,
      ZMax,
      Points,
      Prototype,
      Component,
      Images2D,
      Count
    };

    union {
      CartesianBoundsData cartesianBounds;
      Component component;
      struct {
        UninitializedListHeader<Element> components;
      } points;
    };

    Kind kind = Kind::Unknown;
  };

  const char* elementKindString[] = {
     "Unknown",
      "E57Root",
      "Data3D",
      "VectorChild",
      "Name",
      "CartesianBounds",
      "XMin",
      "XMax",
      "YMin",
      "YMax",
      "ZMin",
      "ZMax",
      "Points",
      "Prototype",
      "Component",
      "Images2D"
  };
  static_assert(sizeof(elementKindString) == sizeof(elementKindString[0]) * static_cast<size_t>(Element::Kind::Count));


  struct Context {
    E57File* e57File = nullptr;
    Logger logger = nullptr;
    std::vector<Element*> stack;

    ListHeader<Element> points;
    Arena arena;
  };

  Element::Kind elementKind(cd_xml_stringview_t* name)
  {
    std::string key(name->begin, name->end);

    if (key == "e57Root") {
      return Element::Kind::E57Root;
    }
    else if (key == "data3D") {
      return Element::Kind::Data3D;
    }
    if (key == "vectorChild") {
      return Element::Kind::VectorChild;
    }
    else if (key == "name") {
      return Element::Kind::Name;
    }
    else if (key == "cartesianBounds") {
      return Element::Kind::CartesianBounds;
    }
    else if (key == "xMinimum") {
      return Element::Kind::XMin;
    }
    else if (key == "xMaximum") {
      return Element::Kind::XMax;
    }
    else if (key == "yMinimum") {
      return Element::Kind::YMin;
    }
    else if (key == "yMaximum") {
      return Element::Kind::YMax;
    }
    else if (key == "zMinimum") {
      return Element::Kind::ZMin;
    }
    else if (key == "zMaximum") {
      return Element::Kind::ZMax;
    }
    else if (key == "points") {
      return Element::Kind::Points;
    }
    else if (key == "prototype") {
      return Element::Kind::Prototype;
    }

    else if (key == "cartesianX") {
      return Element::Kind::Component;
    }
    else if (key == "cartesianY") {
      return Element::Kind::Component;
    }
    else if (key == "cartesianZ") {
      return Element::Kind::Component;
    }
    else if (key == "cartesianInvalidState") {
      return Element::Kind::Component;
    }

    else if (key == "images2D") {
      return Element::Kind::Images2D;
    }

    return Element::Kind::Unknown;
  }

  bool parseNumber(float& dst, const cd_xml_stringview_t* text)
  {
   const std::string str(text->begin, text->end);
    try {
      dst = std::strtof(str.c_str(), nullptr);
      return true;
    }
    catch (...) {
      return false;
    }
    return true;
  }

  bool parseNumber(double& dst, const cd_xml_stringview_t* text)
  {
    const std::string str(text->begin, text->end);
    try {
      dst = std::strtod(str.c_str(), nullptr);
      return true;
    }
    catch (...) {
      return false;
    }
    return true;
  }

  bool parseNumber(int32_t& dst, const cd_xml_stringview_t* text)
  {
    const std::string str(text->begin, text->end);
    try {
      dst = std::strtol(str.c_str(), nullptr, 10);
      return true;
    }
    catch (...) {
      return false;
    }
    return true;
  }


  bool xmlElementEnter(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);

    ctx.logger(0, "%.*s%.*s:", int(ctx.stack.size()), "                                                 ", int(name->end - name->begin), name->begin);

    ctx.stack.emplace_back(ctx.arena.alloc<Element>());
    ctx.stack.back()->kind = elementKind(name);

    switch (ctx.stack.back()->kind) {
    case Element::Kind::CartesianBounds:
      ctx.stack.back()->cartesianBounds = CartesianBoundsData{
        .xMin = std::numeric_limits<float>::max(), .xMax = -std::numeric_limits<float>::max(),
        .yMin = std::numeric_limits<float>::max(), .yMax = -std::numeric_limits<float>::max(),
        .zMin = std::numeric_limits<float>::max(), .zMax = -std::numeric_limits<float>::max()
      };
      break;
    case Element::Kind::Points:
      ctx.stack.back()->points = {
        .components = {}
      };
      break;
    case Element::Kind::Component:

      Component::Role role = Component::Role::Count;
      std::string_view key(name->begin, name->end);
      if(key == "CartesianX") { role = Component::Role::Count; }
      else if (key == "CartesianY") { role = Component::Role::Count; }
      else if (key == "CartesianZ") { role = Component::Role::Count; }
      else if (key == "SphericalRange") { role = Component::Role::Count; }
      else if (key == "SphericalAzimuth") { role = Component::Role::Count; }
      else if (key == "SphericalElevation") { role = Component::Role::Count; }
      else if (key == "RowIndex") { role = Component::Role::Count; }
      else if (key == "ColumnIndex") { role = Component::Role::Count; }
      else if (key == "ReturnCount") { role = Component::Role::Count; }
      else if (key == "ReturnIndex") { role = Component::Role::Count; }
      else if (key == "TimeStamp") { role = Component::Role::Count; }
      else if (key == "Intensity") { role = Component::Role::Count; }
      else if (key == "ColorRed") { role = Component::Role::Count; }
      else if (key == "ColorGreen") { role = Component::Role::Count; }
      else if (key == "ColorBlue") { role = Component::Role::Count; }
      else if (key == "CartesianInvalidState") { role = Component::Role::Count; }
      else if (key == "SphericalInvalidState") { role = Component::Role::Count; }
      else if (key == "IsTimeStampInvalid") { role = Component::Role::Count; }
      else if (key == "IsIntensityInvalid") { role = Component::Role::Count; }
      else if (key == "IsColorInvalid") { role = Component::Role::Count; }
      ctx.stack.back()->component = {
        .role = role,
        .type = Component::Type::Count,
      };
      break;
    }

    return true;
  }

  bool xmlElementExit(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);
    assert(!ctx.stack.empty());
    assert(ctx.stack.back()->kind == elementKind(name));
    Element* element = ctx.stack.back();

    size_t N = ctx.stack.size();

    switch (element->kind) {
    case Element::Kind::CartesianBounds:
      ctx.logger(0, ">>> Parsed cartesian bounds [%.2f %.2f %.2f] x [%.2f %.2f %.2f]:",
                 ctx.stack.back()->cartesianBounds.xMin, ctx.stack.back()->cartesianBounds.yMin, ctx.stack.back()->cartesianBounds.zMin,
                 ctx.stack.back()->cartesianBounds.xMax, ctx.stack.back()->cartesianBounds.yMax, ctx.stack.back()->cartesianBounds.zMax);
      break;

    case Element::Kind::Points:
      ctx.points.pushBack(element);
      break;

    case Element::Kind::Component:
      if (3 <= N && ctx.stack[N - 2]->kind == Element::Kind::Prototype && ctx.stack[N - 3]->kind == Element::Kind::Points) {
        Element* points = ctx.stack[N - 3];
        points->points.components.pushBack(element);
      }
      else {
        ctx.logger(2, "Unexpected %s", elementKindString[static_cast<size_t>(element->kind)]);
        return false;
      }
      break;
    }

    //ctx.logger(0, "< %.*s", int(name->end - name->begin), name->begin);
    ctx.stack.pop_back();
    return true;
  }

  bool xmlAttribute(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name, cd_xml_stringview_t* val)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);
    assert(!ctx.stack.empty());
    Element* element = ctx.stack.back();

    std::string_view key(name->begin, name->end);

    switch (element->kind) {
    case Element::Kind::Component:
      if (key == "type") {
        std::string_view value(val->begin, val->end);
        if (value == "ScaledInteger") {
          element->component.type = Component::Type::ScaledInteger;
          element->component.scaledInteger.min = std::numeric_limits<int32_t>::max();
          element->component.scaledInteger.max = std::numeric_limits<int32_t>::min();
          element->component.scaledInteger.scale = 1.0;
          element->component.scaledInteger.offset = 0.0;
        }
        else if (value == "Integer") {
          element->component.type = Component::Type::Integer;
          element->component.integer.min = std::numeric_limits<int32_t>::max();
          element->component.integer.max = std::numeric_limits<int32_t>::min();
        }
        else {
          ctx.logger(2, "Unexpected component type %.*s", int(value.size()), value.data());
          return false;
        }
      }

      else if (key == "minimum") {
        switch (element->component.type) {
        case Component::Type::ScaledInteger:  return parseNumber(element->component.scaledInteger.min, val);
        case Component::Type::Integer:        return parseNumber(element->component.integer.min, val);
        default:
          ctx.logger(2, "Attribute 'minimum' not valid for component type %u", uint32_t(element->component.type));
          return false;
        }
      }

      else if (key == "maximum") {
        switch (element->component.type) {
        case Component::Type::ScaledInteger:  return parseNumber(element->component.scaledInteger.max, val);
        case Component::Type::Integer:        return parseNumber(element->component.integer.max, val);
        default:
          ctx.logger(2, "Attribute 'maximum' not valid for component type %u", uint32_t(element->component.type));
          return false;
        }
      }

      else if (key == "scale") {
        switch (element->component.type) {
        case Component::Type::ScaledInteger:  return parseNumber(element->component.scaledInteger.scale, val);
        default:
          ctx.logger(2, "Attribute 'scale' not valid for component type %u", uint32_t(element->component.type));
          return false;
        }
      }

      else if (key == "offset") {
        switch (element->component.type) {
        case Component::Type::ScaledInteger:  return parseNumber(element->component.scaledInteger.offset, val);
        default:
          ctx.logger(2, "Attribute 'offset' not valid for component type %u", uint32_t(element->component.type));
          return false;
        }
      }

      break;

    default:
      break;
    }


    return true;
  }


  bool xmlText(void* userdata, cd_xml_doc_t* doc, cd_xml_stringview_t* text)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);
    ctx.logger(0, "%.*sText %.*s", int(ctx.stack.size()), "                                                 ", int(text->end - text->begin), text->begin);

    size_t N = ctx.stack.size();

    if (2 <= N && ctx.stack[N - 2]->kind == Element::Kind::CartesianBounds) {
      if (ctx.stack[N - 1]->kind == Element::Kind::XMin) {
        return parseNumber(ctx.stack[N - 2]->cartesianBounds.xMin, text);
      }
      else if (ctx.stack[N - 1]->kind == Element::Kind::XMax) {
        return parseNumber(ctx.stack[N - 2]->cartesianBounds.xMax, text);
      }
      else if (ctx.stack[N - 1]->kind == Element::Kind::YMin) {
        return parseNumber(ctx.stack[N - 2]->cartesianBounds.yMin, text);
      }
      else if (ctx.stack[N - 1]->kind == Element::Kind::YMax) {
        return parseNumber(ctx.stack[N - 2]->cartesianBounds.yMax, text);
      }
      else if (ctx.stack[N - 1]->kind == Element::Kind::ZMin) {
        return parseNumber(ctx.stack[N - 2]->cartesianBounds.zMin, text);
      }
      else if (ctx.stack[N - 1]->kind == Element::Kind::ZMax) {
        return parseNumber(ctx.stack[N - 2]->cartesianBounds.zMax, text);
      }
    }

    return true;
  }
}


bool parseE57Xml(E57File* e57File, Logger logger, const char* xmlBytes, size_t xmlLength)
{
  Context ctx{
    .e57File = e57File,
    .logger = logger
  };


  cd_xml_doc_t* doc = nullptr;
  if (cd_xml_parse_status_t status = cd_xml_init_and_parse(&doc, xmlBytes, xmlLength, CD_XML_FLAGS_NONE); status != CD_XML_STATUS_SUCCESS)
  {
    const char* what = nullptr;
    switch (status)
    {
    case CD_XML_STATUS_POINTER_NOT_NULL:          what = "Doc-pointer passed to parser was not NULL."; break;
    case CD_XML_STATUS_UNKNOWN_NAMESPACE_PREFIX:  what = "Element or attribute with namespace prefix that hasn't been defined."; break;
    case CD_XML_STATUS_UNSUPPORTED_VERSION:       what = "XML version is not 1.0."; break;
    case CD_XML_STATUS_UNSUPPORTED_ENCODING:      what = "XML encoding is not ASCII or UTF-8"; break;
    case CD_XML_STATUS_MALFORMED_UTF8:            what = "Illegal UTF-8 encoding encountered."; break;
    case CD_XML_STATUS_MALFORMED_ATTRIBUTE:       what = "Error while parsing an attribute."; break;
    case CD_XML_STATUS_PREMATURE_EOF:             what = "Encountered end-of-buffer before parsing was done."; break;
    case CD_XML_STATUS_MALFORMED_DECLARATION:     what = "Error in the initial XML declaration."; break;
    case CD_XML_STATUS_UNEXPECTED_TOKEN:          what = "Encountered unexpected token."; break;
    case CD_XML_STATUS_MALFORMED_ENTITY:          what = "Error while parsing an entity."; break;
    default:  assert(false && "Invalid status enum");    break;
    }

    ctx.logger(2, "Failed to parse xml: %s", what);
    return false;
  }

  if (!cd_xml_apply_visitor(doc, &ctx, xmlElementEnter, xmlElementExit, xmlAttribute, xmlText)) {
    return false;
  }
  ctx.logger(0, "XML parsed successfully");

  for (Element* points = ctx.points.first; points; points = points->next) {
    ctx.logger(0, ">>> Parsed point prototype:");
    for (const Element* elem = points->points.components.first; elem; elem = elem->next) {
      assert(elem->kind == Element::Kind::Component);
      ctx.logger(0, "    + role=%u type=%u", static_cast<uint32_t>(elem->component.role), static_cast<uint32_t>(elem->component.type));
    }
  }





  return true;
}