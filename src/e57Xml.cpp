// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#include "Common.h"
#include "e57File.h"
#include "cd_xml.h"

#include <cassert>
#include <vector>
#include <string>

namespace {
  
  const char* spaces = "                                                                  ";

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
        Points points;
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

  bool parseNumber(int64_t& dst, const cd_xml_stringview_t* text)
  {
    const std::string str(text->begin, text->end);
    try {
      dst = std::strtoll(str.c_str(), nullptr, 10);
      return true;
    }
    catch (...) {
      return false;
    }
    return true;
  }

  bool parseNumber(size_t& dst, const cd_xml_stringview_t* text)
  {
    const std::string str(text->begin, text->end);
    try {
      dst = std::strtoull(str.c_str(), nullptr, 10);
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
    ctx.logger(0, "%.*s%.*s:", int(ctx.stack.size()), spaces, int(name->end - name->begin), name->begin);

    Element& elem = *ctx.stack.emplace_back(ctx.arena.alloc<Element>());

    std::string_view key(name->begin, name->end);
    if (key == "cartesianBounds") {             elem.kind = Element::Kind::CartesianBounds; }
    else if (key == "points") {                 elem.kind = Element::Kind::Points; }
    else if (key == "e57Root") {                elem.kind = Element::Kind::E57Root; }
    else if (key == "data3D") {                 elem.kind = Element::Kind::Data3D; }
    else if (key == "vectorChild") {            elem.kind = Element::Kind::VectorChild; }
    else if (key == "name") {                   elem.kind = Element::Kind::Name; }
    else if (key == "xMinimum") {               elem.kind = Element::Kind::XMin; }
    else if (key == "xMaximum") {               elem.kind = Element::Kind::XMax; }
    else if (key == "yMinimum") {               elem.kind = Element::Kind::YMin; }
    else if (key == "yMaximum") {               elem.kind = Element::Kind::YMax; }
    else if (key == "zMinimum") {               elem.kind = Element::Kind::ZMin; }
    else if (key == "zMaximum") {               elem.kind = Element::Kind::ZMax; }
    else if (key == "prototype") {              elem.kind = Element::Kind::Prototype; }
    else if (key == "images2D") {               elem.kind = Element::Kind::Images2D; }
    else if (key == "cartesianX") {             elem.kind = Element::Kind::Component; elem.component.role = Component::Role::CartesianX; }
    else if (key == "cartesianY") {             elem.kind = Element::Kind::Component; elem.component.role = Component::Role::CartesianY; }
    else if (key == "cartesianZ") {             elem.kind = Element::Kind::Component; elem.component.role = Component::Role::CartesianZ; }
    else if (key == "sphericalRange") {         elem.kind = Element::Kind::Component; elem.component.role = Component::Role::SphericalRange; }
    else if (key == "sphericalAzimuth") {       elem.kind = Element::Kind::Component; elem.component.role = Component::Role::SphericalAzimuth; }
    else if (key == "sphericalElevation") {     elem.kind = Element::Kind::Component; elem.component.role = Component::Role::SphericalElevation; }
    else if (key == "rowIndex") {               elem.kind = Element::Kind::Component; elem.component.role = Component::Role::RowIndex; }
    else if (key == "columnIndex") {            elem.kind = Element::Kind::Component; elem.component.role = Component::Role::ColumnIndex; }
    else if (key == "returnCount") {            elem.kind = Element::Kind::Component; elem.component.role = Component::Role::ReturnCount; }
    else if (key == "returnIndex") {            elem.kind = Element::Kind::Component; elem.component.role = Component::Role::ReturnIndex; }
    else if (key == "timeStamp") {              elem.kind = Element::Kind::Component; elem.component.role = Component::Role::TimeStamp; }
    else if (key == "intensity") {              elem.kind = Element::Kind::Component; elem.component.role = Component::Role::Intensity; }
    else if (key == "colorRed") {               elem.kind = Element::Kind::Component; elem.component.role = Component::Role::ColorRed; }
    else if (key == "colorGreen") {             elem.kind = Element::Kind::Component; elem.component.role = Component::Role::ColorGreen; }
    else if (key == "colorBlue") {              elem.kind = Element::Kind::Component; elem.component.role = Component::Role::ColorBlue; }
    else if (key == "cartesianInvalidState") {  elem.kind = Element::Kind::Component; elem.component.role = Component::Role::CartesianInvalidState; }
    else if (key == "sphericalInvalidState") {  elem.kind = Element::Kind::Component; elem.component.role = Component::Role::SphericalInvalidState; }
    else if (key == "isTimeStampInvalid") {     elem.kind = Element::Kind::Component; elem.component.role = Component::Role::IsTimeStampInvalid; }
    else if (key == "isColorInvalid") {         elem.kind = Element::Kind::Component; elem.component.role = Component::Role::IsColorInvalid; }
    else { elem.kind = Element::Kind::Unknown; }


    switch (elem.kind) {

    case Element::Kind::Points:
      elem.points.components.init();
      elem.points.points.init();
      break;

    case Element::Kind::CartesianBounds:
      elem.cartesianBounds = CartesianBoundsData{
        .xMin = std::numeric_limits<float>::max(), .xMax = -std::numeric_limits<float>::max(),
        .yMin = std::numeric_limits<float>::max(), .yMax = -std::numeric_limits<float>::max(),
        .zMin = std::numeric_limits<float>::max(), .zMax = -std::numeric_limits<float>::max()
      };
      break;

    case Element::Kind::Component:
      elem.component.type = Component::Type::Count;
      break;

    default:
      break;
    }

    return true;
  }

  bool xmlElementExit(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);
    size_t N = ctx.stack.size();
    assert(N != 0);

    Element* elem = ctx.stack.back();

    switch (elem->kind) {
    case Element::Kind::CartesianBounds:
      ctx.logger(0, ">>> Parsed cartesian bounds [%.2f %.2f %.2f] x [%.2f %.2f %.2f]:",
                 elem->cartesianBounds.xMin, elem->cartesianBounds.yMin, elem->cartesianBounds.zMin,
                 elem->cartesianBounds.xMax, elem->cartesianBounds.yMax, elem->cartesianBounds.zMax);
      break;

    case Element::Kind::Points:
      ctx.points.pushBack(elem);
      break;

    case Element::Kind::Component:
      if (3 <= N && ctx.stack[N - 2]->kind == Element::Kind::Prototype && ctx.stack[N - 3]->kind == Element::Kind::Points) {
        Element* points = ctx.stack[N - 3];
        points->points.components.pushBack(elem);
      }
      else {
        ctx.logger(2, "Unexpected %s", elementKindString[static_cast<size_t>(elem->kind)]);
        return false;
      }
      break;
    }

    //ctx.logger(0, "< %.*s", int(name->end - name->begin), name->begin);
    ctx.stack.pop_back();
    return true;
  }

  bool xmlAttributeComponent(Context& ctx, Component& component, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name, cd_xml_stringview_t* val)
  {
    std::string_view key(name->begin, name->end);
    std::string_view value(val->begin, val->end);
    if (key == "type") {
      if (value == "ScaledInteger") {
        component.initInteger(Component::Type::ScaledInteger);
      }
      else if (value == "Integer") {
        component.initInteger(Component::Type::Integer);
      }
      else if (value == "Float") {
        component.initReal(Component::Type::Double);
      }
      else {
        ctx.logger(2, "Unexpected component type %.*s", int(value.size()), value.data());
        return false;
      }
    }

    else if (key == "minimum") {
      switch (component.type) {

      case Component::Type::Integer:
      case Component::Type::ScaledInteger:
        return parseNumber(component.integer.min, val);

      case Component::Type::Float:
      case Component::Type::Double:
        return parseNumber(component.real.min, val);

      default:
        ctx.logger(2, "Attribute 'minimum' not valid for component type %u", uint32_t(component.type));
        return false;
      }
    }

    else if (key == "maximum") {
      switch (component.type) {
      case Component::Type::Integer:
      case Component::Type::ScaledInteger:
        return parseNumber(component.integer.max, val);
      case Component::Type::Float:
      case Component::Type::Double:
        return parseNumber(component.real.max, val);
      default:
        ctx.logger(2, "Attribute 'maximum' not valid for component type %u", uint32_t(component.type));
        return false;
      }
    }

    else if (key == "precision") {
      switch (component.type) {
      case Component::Type::Float:
      case Component::Type::Double: {
        if (value == "singe") {
          component.type = Component::Type::Float;
        }
        else if (value == "double") {
          component.type = Component::Type::Double;
        }
        else {
          ctx.logger(2, "Unrecognized 'precision' value '%.*s'", int(value.length()), value.data());
          return false;
        }
        break;
      }
      default:
        ctx.logger(2, "Attribute 'precision' not valid for component type %u", uint32_t(component.type));
        return false;
      }
    }

    else if (key == "scale") {
      if (component.type == Component::Type::ScaledInteger) {
        return parseNumber(component.integer.scale, val);
      }
      else {
        ctx.logger(2, "Attribute 'scale' not valid for component type %u", uint32_t(component.type));
        return false;
      }
    }

    else if (key == "offset") {
      if (component.type == Component::Type::ScaledInteger) {
        return parseNumber(component.integer.offset, val);
      }
      else {
        ctx.logger(2, "Attribute 'offset' not valid for component type %u", uint32_t(component.type));
        return false;
      }
    }
    return true;
  }

  bool xmlAttributePoints(Context& ctx, Points& points, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name, cd_xml_stringview_t* val)
  {
    std::string_view key(name->begin, name->end);
    if (key == "type") {
      if (std::string_view(val->begin, val->end) == "CompressedVector") { return true; }
    }
    if (key == "fileOffset") {
      return parseNumber(points.fileOffset, val);
    }
    else if (key == "recordCount") {
      return parseNumber(points.recordCount, val);
    }
    ctx.logger(2, "In <points>, unexpected attribute %.*s='%.*s'",
               int(name->end - name->begin), name->begin,
               int(val->end - val->begin), val->begin);
    return false;
  }


  bool xmlAttribute(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name, cd_xml_stringview_t* val)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);
    assert(!ctx.stack.empty());
    Element* element = ctx.stack.back();

    std::string_view key(name->begin, name->end);
    switch (element->kind) {
    case Element::Kind::Component:
      return xmlAttributeComponent(ctx, element->component, doc, namespace_ix, name, val);
    case Element::Kind::Points:
      return xmlAttributePoints(ctx, element->points.points, doc, namespace_ix, name, val);
    default:
      break;
    }
    return true;
  }


  bool xmlText(void* userdata, cd_xml_doc_t* doc, cd_xml_stringview_t* text)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);
    ctx.logger(0, "%.*sText %.*s", int(ctx.stack.size()), spaces, int(text->end - text->begin), text->begin);

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

  ctx.e57File->points.size = ctx.points.size();
  ctx.e57File->points.data = ctx.e57File->arena.allocArray<Points>(ctx.e57File->points.size);

  size_t pointIx = 0;
  for (Element* srcPoints = ctx.points.first; srcPoints; srcPoints = srcPoints->next) {
    assert(srcPoints->kind == Element::Kind::Points);

    Points& dstPoints = ctx.e57File->points[pointIx++];
    dstPoints = srcPoints->points.points;
    dstPoints.components.size = srcPoints->points.components.size();
    dstPoints.components.data = ctx.e57File->arena.allocArray<Component>(dstPoints.components.size);

    size_t compIx = 0;
    for (const Element* srcComp = srcPoints->points.components.first; srcComp; srcComp = srcComp->next) {
      assert(srcComp->kind == Element::Kind::Component);

      Component& dstComp = dstPoints.components[compIx++];
      dstComp = srcComp->component;

      switch (dstComp.type) {

      case Component::Type::Float:
      case Component::Type::Double:
        if (dstComp.real.max < dstComp.real.min) {
          ctx.logger(2, "Float/double component min is larger than max");
          return false;
        }
        break;

      case Component::Type::Integer:
      case Component::Type::ScaledInteger: {
        if (dstComp.integer.max < dstComp.integer.min) {
          ctx.logger(2, "Integer/scaled integer component min is larger than max");
          return false;
        }
        int64_t diff = dstComp.integer.max - dstComp.integer.min;
        dstComp.integer.bitWidth = std::bit_width(static_cast<uint64_t>(diff));
        break;
      }
      default:
        ctx.logger(2, "Illegal component type");
        return false;
      }



    }
  }

  ctx.logger(0, "Parsed points");
  for (size_t j = 0; j < ctx.e57File->points.size; j++) {
    const Points& points = ctx.e57File->points[j];
    ctx.logger(0, "%zu: fileOffset=%zu recordCount=%zu", j, points.fileOffset, points.recordCount);
    for (size_t i = 0; i < points.components.size; i++) {
      const Component& comp = points.components[i];
      switch (comp.type) {
      case Component::Type::Integer:
        ctx.logger(0, "   %zu: integer min=%d max=%d", i, comp.integer.min, comp.integer.max);
        break;
      case Component::Type::ScaledInteger:
        ctx.logger(0, "   %zu: scaled integer min=%d max=%d scale=%f offset=%f", i, comp.integer.min, comp.integer.max, comp.integer.scale, comp.integer.offset);
        break;
      case Component::Type::Float:
        ctx.logger(0, "   %zu: float min=%f max=%f", i, comp.real.min, comp.real.max);
        break;
      case Component::Type::Double:
        ctx.logger(0, "   %zu: double min=%f max=%f", i, comp.real.min, comp.real.max);
        break;
      default:
        assert(false);
        break;
      }
    }
  }

  return true;
}