// cd_xml_h - simple and compact XML parser and writer in a single header file.
//
//   Author:        Christopher Dyken
//   Version:       0.1a
//   License:       MIT
//   Language:      C99
//   Repository:    https://github.com/cdyk/cdutils
//
// Introduction:
// =============
//
//   This is a simple and compact XML parser with a limited feature set. It is
//   intended to be useful in the case where one just needs to read an XML and
//   pull out a few values, or to build and write out a few values as XML.
//
//   The parsed XML is represented in-memory as a simple DOM that can either
//   be directly traversed or indirectly via visitor functions.
//
//   The DOM can also be built programmatically.
//
//   Features:
//   - Parses ASCII/UTF-8 XML files.
//   - Decodes &quot;, &amp;, &apos;, &lt; &gt; &#nnnn;, and &#xhhhh;
//   - Resolves namespaces.
//
//   Limitations:
//   - Currently, element and attribute names must be pure ASCII. Text and
//     attribute values can be UTF-8 though.
//   - No fancy stuff 
//   - Doc's with multiple default namespaces won't serialize properly. To fix
//     add unique prefixes to all namespaces except a single global default.
//
//
// How to use:
// ===========
//
//   The library is built by defining CD_XML_IMPLEMENTATION and including this
//   file in one file in your project.
//
//
// To parse an XML file
// -------------------- 
//   
//     const char* xml = "..."
//     cd_xml_doc_t* doc = NULL;
//     rv = cd_xml_init_and_parse(&doc,
//                                xml,
//                                strlen(xml),
//                                CD_XML_FLAGS_NONE);
//
//   Here, cd_xml_init_and_parse takes a char*-pointer and a size to a memory
//   region that contains the XML, i.e., no need for zero-termination.
//
//   The doc structure stores mostly just pointers to the original XML text,
//   and thus that chunk of memory needs to stay alive as long as the doc.
//
//   Otherwise, one can pass CD_XML_FLAGS_COPY_STRINGS, and in this case all
//   relevant parts of the XML is copied, and it is safe to free the input
//   buffer after the parser returns.
//
//   The parser returns a status code that is either CD_XML_STATUS_SUCCESS (0)
//   if everything went well, otherwise there is an error code for the first
//   error it encountered.
//
//
// To serialize a doc to XML:
// --------------------------
//
//   The XML is serialized via a callback function with the signature
//
//     bool cd_xml_output_func(void* userdata, const char* ptr, size_t bytes) {
//       fprintf(stderr, "%.*s", (int)bytes, ptr);
//       return true;
//    }
//
//   here with a body that outputs everything to stderr. Userdata is an opaque
//   value passed from the client application.
//
//   The serialization is invoked with
//
//     cd_xml_write(doc, output_func, clientdata, true);
//
//   where the last argument is whether or not the XML should be pretty-
//   printed. Pretty adds hierarchical indentation, while non-pretty
//   outputs everything on a single line.
//
//
// To traverse the doc via visitors:
// ---------------------------------
//
//   The entire doc can be traversed using callback functions with these
//   signatures:
//
//     bool visit_elem_enter(void* userdata,
//                           cd_xml_doc_t* doc,
//                           cd_xml_ns_ix_t namespace_ix,
//                            cd_xml_stringview_t* name);
//
//     bool visit_elem_exit(void* userdata,
//                          cd_xml_doc_t* doc,
//                          cd_xml_ns_ix_t namespace_ix,
//                          cd_xml_stringview_t* name);
//
//     bool visit_attribute(void* userdata,
//                          cd_xml_doc_t* doc,
//                          cd_xml_ns_ix_t namespace_ix,
//                          cd_xml_stringview_t* name,
//                          cd_xml_stringview_t* value);
//
//     bool visit_text(void* userdata,
//                     cd_xml_doc_t* doc,
//                     cd_xml_stringview_t* text);
//
//   The hierarchy is traversed depth-first, visit_elem_enter is invoked first
//   followed by all attributes visited with visit_attribute. Then all children
//   are processed in order (children can either be elements or text), and
//   finally visit_elem_exit is invoked when an element is done.
//
//   The traversal is invoked with:
//
//     cd_xml_apply_visitor(doc, clientdata,
//                          visit_elem_enter,
//                          visit_elem_exit,
//                          visit_attribute,
//                          visit_text);
//
//   which is just the doc, clientdata and callbacks. Everything except the doc
//   may be NULL.
//
//
// To create XML via API
// ---------------------
//
//  First we create some stringviews that we can pass pointers to. The
//  cd_xml_strv is just a convenience function to create a view of a C-
//  string.
//
//    cd_xml_stringview_t foo_str = cd_xml_strv("foo");
//    cd_xml_stringview_t bar_str = cd_xml_strv("bar");
//    cd_xml_stringview_t baz_str = cd_xml_strv("baz");
//    cd_xml_stringview_t quux_str = cd_xml_strv("quux");
//
//  Then we initialize and build the doc
//
//    cd_xml_doc_t* doc = cd_xml_init();
//    cd_xml_node_ix_t foo = cd_xml_add_element(doc,
//                                              cd_xml_no_ix,
//                                              &foo_str,
//                                              cd_xml_no_ix,
//                                              CD_XML_FLAGS_COPY_STRINGS);
//    cd_xml_node_ix_t bar = cd_xml_add_element(doc,
//                                              cd_xml_no_ix,
//                                              &bar_str,
//                                              foo,
//                                              CD_XML_FLAGS_COPY_STRINGS);
//    cd_xml_add_attribute(doc,
//                         cd_xml_no_ix,
//                         &baz_str,
//                         &quux_str,
//                         bar,
//                         CD_XML_FLAGS_COPY_STRINGS);
//    cd_xml_add_text(doc, &quux_str, foo, CD_XML_FLAGS_COPY_STRINGS);
//
//  Here, we create a root element 'foo' and attach a child element 'bar',
//  which also gets an attribute 'baz' with the value 'quux'. This element
//  is followed by some text with 'quux'. That is:
//
//    <?xml version="1.0" encoding="UTF-8"?>
//    <foo>
//      <bar baz="quux"/>
//      quux
//    </foo>
//
//
// To traverse the doc directly:
// -----------------------------
//
//   The doc-structure is quite simple and can be directly accessed. Of interest
//   is the three arrays namespaces, nodes and attributes.
//
//   Namespaces are just an array of prefix-uri mappings. cd_xml_ns_ix indexes
//   into this array. If a namespace has an empty prefix, it is a default
//   namespace.
//
//   Nodes are an array of the hierarchical elements of the XML. Nodes are
//   either an element (which corresponds to a <tag> in XML and can have 
//   attributes and children) or text (which is the text that is between
//   the tags). Usually text is located as single leaf nodes, while the rest
//   of the nodes are elements, but not always. Nodes have the index of
//   next_sibling, which allows traversal between sibling nodes.
//
//   Elements hold an index to the first child as well as to the first attribute.
//
//   Attributes are key-value pairs attached to elements.
//
// Revision history:
// =================
//
//   - 0.1a Initial version.
//

#ifndef CD_XML_H
#define CD_XML_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef uint32_t cd_xml_ns_ix_t;

typedef uint32_t cd_xml_att_ix_t;

typedef uint32_t cd_xml_node_ix_t;

static const uint32_t cd_xml_no_ix = (uint32_t)-1;


// Stretchy-buf helpers
#define cd_xml__sb_base(a) ((unsigned*)(a)-2)
#define cd_xml__sb_size(a) (cd_xml__sb_base(a)[0])

// Get count of a stretchy-buf variable
#define cd_xml_sb_size(a) ((a)?cd_xml__sb_size(a):0u)

// A non-owned view of a string.
typedef struct {
    const char* begin;
    const char* end;
} cd_xml_stringview_t;

// Struct that represents an XML attribute.
typedef struct {
    cd_xml_stringview_t name;                               // Attribute name.
    cd_xml_stringview_t value;                              // Attribute value.
    cd_xml_ns_ix_t namespace_ix;                            // Index of attribute name namespace.
    cd_xml_att_ix_t next_attribute;                         // Index of next attribute of element.
} cd_xml_attribute_t;

// Specifies type of node.
typedef enum {
    CD_XML_NODE_ELEMENT,                                    // Node is an element.
    CD_XML_NODE_TEXT                                        // Node is text.
} cd_xml_node_kind_t;

// Specifies behaviour
typedef enum
{
    CD_XML_FLAGS_NONE           = 0,                        // None
    CD_XML_FLAGS_COPY_STRINGS   = 1                         // Make copies of all strings passed to library.
} cd_xml_flags_t;

// Specifies result of parsing
typedef enum
{
    CD_XML_STATUS_SUCCESS = 0,                              // Successful parsing.
    CD_XML_STATUS_POINTER_NOT_NULL,                         // doc-pointer passed to parser was not NULL.
    CD_XML_STATUS_UNKNOWN_NAMESPACE_PREFIX,                 // Element or attribute with namespace prefix that hasn't been defined.
    CD_XML_STATUS_UNSUPPORTED_VERSION,                      // XML version is not 1.0.
    CD_XML_STATUS_UNSUPPORTED_ENCODING,                     // XML encoding is not ASCII or UTF-8
    CD_XML_STATUS_MALFORMED_UTF8,                           // Illegal UTF-8 encoding encountered.
    CD_XML_STATUS_MALFORMED_ATTRIBUTE,                      // Error while parsing an attribute.
    CD_XML_STATUS_PREMATURE_EOF,                            // Encountered end-of-buffer before parsing was done.
    CD_XML_STATUS_MALFORMED_DECLARATION,                    // Error in the initial XML declaration.
    CD_XML_STATUS_UNEXPECTED_TOKEN,                         // Encountered unexpected token.
    CD_XML_STATUS_MALFORMED_ENTITY                          // Error while parsing an entity.
} cd_xml_parse_status_t;

// Holds data of an element
typedef struct {                                            // Element data
    cd_xml_stringview_t name;                               // Element name.
    cd_xml_ns_ix_t      namespace_ix;                       // Element index, cd_xml_no_ix for no namespace.
    cd_xml_node_ix_t    first_child;                        // Pointer to first child node of this element.
    cd_xml_node_ix_t    last_child;                         // Pointer to last child node of this element.
    cd_xml_att_ix_t     first_attribute;                    // Pointer to first attribute of this element.
    cd_xml_att_ix_t     last_attribute;                     // Pointer to last attribute of this element.
} node_element_t;

// Holds data of text 
typedef struct {                                            // Text data
    cd_xml_stringview_t content;                           // Text contents
} node_text_t;

// Holds data of a node, that is, an element or text.
typedef struct {
    union {
        node_element_t          element;                    // Data if node is element.
        node_text_t             text;                       // Data if node is text.
    }                           data;
    cd_xml_node_ix_t            next_sibling;               // Next node sibling of same parent.
    cd_xml_node_kind_t          kind;                       // Kind of node, either CD_XML_NODE_ELEMENT or CD_XML_NODE_TEXT.
} cd_xml_node_t;

// Represents a namespace
typedef struct  {
    cd_xml_stringview_t         prefix;                     // Prefix of namespace, empty for default namespace.
    cd_xml_stringview_t         uri;                        // URI of namespace.
} cd_xml_ns_t;

// Header of memory allocated, stored in a linked list out of cd_xml_doc._t.allocated_buffers.
typedef struct cd_xml_buf_struct {
    struct cd_xml_buf_struct*   next;                       // Next allocated buffer or NULL.
    char                        payload;                    // Offset of payload data.
} cd_xml_buf_t;

// XML DOM representation
typedef struct {
    cd_xml_ns_t*                namespaces;                 // Array of namespaces, stretchy buf, count using cd_xml_sb_size.
    cd_xml_node_t*              nodes;                      // Array of nodes, stretchy buf, count using cd_xml_sb_size.
    cd_xml_attribute_t*         attributes;                 // Array of attributes, stretchy buf, count usng cd_xml_sb_size.
    cd_xml_buf_t*               allocated_buffers;          // Backing for modifieds strings.
} cd_xml_doc_t;

// Callback function for consuming output from writer
typedef bool (*cd_xml_output_func)(void* userdata, const char* ptr, size_t bytes);

// Visitor callback functions

typedef bool(*cd_xml_visit_elem_enter)(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name);
typedef bool(*cd_xml_visit_elem_exit)(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name);
typedef bool(*cd_xml_visit_attribute)(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name, cd_xml_stringview_t* value);
typedef bool(*cd_xml_visit_text)(void* userdata, cd_xml_doc_t* doc, cd_xml_stringview_t* text);


// Initialize a doc for building hierarchy via API
cd_xml_doc_t* cd_xml_init(void);

// Free a doc and its resources.
void cd_xml_free(cd_xml_doc_t** doc);

// Register a new namespace.
//
// returns an index that can be used when creating elements and attributes.
cd_xml_att_ix_t cd_xml_add_namespace(cd_xml_doc_t*          doc,        // XML doc
                                     cd_xml_stringview_t*   prefix,     // Prefix to use, empty string for default namespace.
                                     cd_xml_stringview_t*   uri,        // Uri for namespace
                                     cd_xml_flags_t         flags);

// Create a new element.
//
// Root node must be the first element created of the doc.
//
// Returns an index that can be used as parent for other elements or for attaching attributes.
cd_xml_node_ix_t cd_xml_add_element(cd_xml_doc_t*           doc,        // XML doc
                                    cd_xml_ns_ix_t          ns,         // Namespace index, cd_xml_no_ix if irrelevant.
                                    cd_xml_stringview_t*    name,       // Name of element, must be non-empty.
                                    cd_xml_node_ix_t        parent,     // Index of parent element, root element has parent cd_xml_no_ix.
                                    cd_xml_flags_t          flags);

// Add an attribute to an existing element.
//
// Returns an index that you probably have no use for.
cd_xml_att_ix_t cd_xml_add_attribute(cd_xml_doc_t*          doc,        // XML doc.
                                     cd_xml_ns_ix_t         ns,         // Namespace index, cd_xml_no_ix if irrelevant.
                                     cd_xml_stringview_t*   name,       // Name of attribute, must be non-empty.
                                     cd_xml_stringview_t*   value,      // Value of attribute.
                                     cd_xml_node_ix_t       element,    // Element on which to add this attribute.
                                     cd_xml_flags_t         flags);

// Add text
//
// Returns an index that you probably have no use for.
cd_xml_node_ix_t cd_xml_add_text(cd_xml_doc_t*        doc,              // XML doc.
                                 cd_xml_stringview_t* content,          // Text to add
                                 cd_xml_node_ix_t     parent,           // Element to which the text is a child
                                 cd_xml_flags_t       flags);

// Parse XML and build a doc
//
// Returns CD_XML_STATUS_SUCCESS if everything went well.
cd_xml_parse_status_t cd_xml_init_and_parse(cd_xml_doc_t**  doc,        // Pointer to a doc-pointer to NULL
                                            const char*     data,       // Pointer to XML data
                                            size_t          size,       // Size of XML data
                                            cd_xml_flags_t  flags);

// Serialzie doc as XML
//
// Return true if everything went well.
bool cd_xml_write(cd_xml_doc_t*         doc,                            // XML doc.
                  cd_xml_output_func    output_func,                    // output callback, returns true if everything is OK.
                  void*                 userdata,                       // userdata passed to output callback.
                  bool                  pretty);

// Runs a set of visitor callbacks on the doc
//
// Returns true if everything went well.
bool cd_xml_apply_visitor(cd_xml_doc_t*           doc,                  // XML doc.
                          void*                   userdata,             // Userdata passed to callbacks.
                          cd_xml_visit_elem_enter elem_enter,           // Callback when entering an element.
                          cd_xml_visit_elem_exit  elem_exit,            // Callback when finished with an element.
                          cd_xml_visit_attribute  attribute,            // Callback when traversing an element's attributes.
                          cd_xml_visit_text       text);                // Callback when traversing a text node.

// Helper func to create stringviews from C-strings
inline cd_xml_stringview_t cd_xml_strv(const char* str)
{
    cd_xml_stringview_t rv;
    rv.begin = str;
    rv.end = str + strlen(str);
    return rv;
}




#ifdef __cplusplus
}   // extern "C"
#endif

#endif  // CD_XML_H
