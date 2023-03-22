// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#include "Common.h"
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

  struct RecordItem
  {
    RecordItem* next = nullptr;
  };

  struct PrototypeElement {
    PrototypeElement* next;
    enum struct Type : uint32_t {
      Unknown,
      ScaledInteger,
      Integer
    };

    Type type;
    float scale;
  };

  struct Points
  {
    PrototypeElement* prototype;
  };


  struct Element
  {
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
      CartesianX,
      CartesianY,
      CartesianZ,
      CartesianInvalidState,
      Images2D
    };

    union {
      CartesianBoundsData cartesianBounds;
      PrototypeElement pointRecProtoElem;
      Points points;
    };

    Kind kind = Kind::Unknown;
  };

  struct Context {
    Logger logger = nullptr;
    std::vector<Element> stack;
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
      return Element::Kind::CartesianX;
    }
    else if (key == "cartesianY") {
      return Element::Kind::CartesianY;
    }
    else if (key == "cartesianZ") {
      return Element::Kind::CartesianZ;
    }
    else if (key == "cartesianInvalidState") {
      return Element::Kind::CartesianInvalidState;
    }

    else if (key == "images2D") {
      return Element::Kind::Images2D;
    }

    return Element::Kind::Unknown;
  }

  bool parseFloat(float& dst, cd_xml_stringview_t* text)
  {
    std::string str(text->begin, text->end);
    try {
      dst = std::strtof(str.c_str(), nullptr);
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
    ctx.stack.emplace_back(Element{ .kind = elementKind(name) });
    switch (ctx.stack.back().kind) {
    case Element::Kind::CartesianBounds:
      ctx.stack.back().cartesianBounds = CartesianBoundsData{
        .xMin = std::numeric_limits<float>::max(), .xMax = -std::numeric_limits<float>::max(),
        .yMin = std::numeric_limits<float>::max(), .yMax = -std::numeric_limits<float>::max(),
        .zMin = std::numeric_limits<float>::max(), .zMax = -std::numeric_limits<float>::max()
      };
      break;
    case Element::Kind::Points:
      ctx.stack.back().points = Points{
        .prototype = nullptr
      };
      break;
    case Element::Kind::CartesianX:
    case Element::Kind::CartesianY:
    case Element::Kind::CartesianZ:
    case Element::Kind::CartesianInvalidState:
      ctx.stack.back().pointRecProtoElem = PrototypeElement{
        .type = PrototypeElement::Type::Unknown,
        .scale = 1.f
      };
      break;
    }

    return true;
  }

  bool xmlElementExit(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name)
  {
    Context& ctx = *reinterpret_cast<Context*>(userdata);
    assert(!ctx.stack.empty());
    assert(ctx.stack.back().kind == elementKind(name));

    size_t N = ctx.stack.size();

    switch (ctx.stack.back().kind) {
    case Element::Kind::CartesianBounds:
      ctx.logger(0, ">>> Parsed cartesian bounds [%.2f %.2f %.2f] x [%.2f %.2f %.2f]:",
                 ctx.stack.back().cartesianBounds.xMin, ctx.stack.back().cartesianBounds.yMin, ctx.stack.back().cartesianBounds.zMin,
                 ctx.stack.back().cartesianBounds.xMax, ctx.stack.back().cartesianBounds.yMax, ctx.stack.back().cartesianBounds.zMax);
      break;

    case Element::Kind::Points:
      ctx.logger(0, ">>> Parsed point prototype:");
      for (const PrototypeElement* elem = ctx.stack.back().points.prototype; elem; elem = elem->next) {
        ctx.logger(0, "    + type=%u", static_cast<uint32_t>(elem->type));
      }

      break;

    case Element::Kind::CartesianX:
    case Element::Kind::CartesianY:
    case Element::Kind::CartesianZ:
    case Element::Kind::CartesianInvalidState:
      if (3 <= N && ctx.stack[N - 2].kind == Element::Kind::Prototype && ctx.stack[N - 3].kind == Element::Kind::Points) {
        PrototypeElement* elem = new PrototypeElement(ctx.stack.back().pointRecProtoElem);
        elem->next = ctx.stack[N - 3].points.prototype;
        ctx.stack[N - 3].points.prototype = elem;
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

    std::string_view key(name->begin, name->end);

    switch (ctx.stack.back().kind) {
    case Element::Kind::CartesianX:
    case Element::Kind::CartesianY:
    case Element::Kind::CartesianZ:
    case Element::Kind::CartesianInvalidState:
      if (key == "type") {
        std::string_view value(val->begin, val->end);
        if (value == "ScaledInteger") {
          ctx.stack.back().pointRecProtoElem.type = PrototypeElement::Type::ScaledInteger;
        }
        else if (value == "Integer") {
          ctx.stack.back().pointRecProtoElem.type = PrototypeElement::Type::Integer;
        }
      }
      else if (key == "minimum") {

      }
      else if (key == "maximum") {

      }
      else if (key == "scale") {
        return parseFloat(ctx.stack.back().pointRecProtoElem.scale, val);
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

    if (2 <= N && ctx.stack[N - 2].kind == Element::Kind::CartesianBounds) {
      if (ctx.stack[N - 1].kind == Element::Kind::XMin) {
        return parseFloat(ctx.stack[N - 2].cartesianBounds.xMin, text);
      }
      else if (ctx.stack[N - 1].kind == Element::Kind::XMax) {
        return parseFloat(ctx.stack[N - 2].cartesianBounds.xMax, text);
      }
      else if (ctx.stack[N - 1].kind == Element::Kind::YMin) {
        return parseFloat(ctx.stack[N - 2].cartesianBounds.yMin, text);
      }
      else if (ctx.stack[N - 1].kind == Element::Kind::YMax) {
        return parseFloat(ctx.stack[N - 2].cartesianBounds.yMax, text);
      }
      else if (ctx.stack[N - 1].kind == Element::Kind::ZMin) {
        return parseFloat(ctx.stack[N - 2].cartesianBounds.zMin, text);
      }
      else if (ctx.stack[N - 1].kind == Element::Kind::ZMax) {
        return parseFloat(ctx.stack[N - 2].cartesianBounds.zMax, text);
      }
    }

    return true;
  }
}


bool parseE57Xml(Logger logger, const char* xmlBytes, size_t xmlLength)
{
  Context ctx{
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



  return true;
}