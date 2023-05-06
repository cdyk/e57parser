// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cstdarg>
#include <algorithm>

#include "Common.h"

void logTrace(Logger logger, _Printf_format_string_ const char* msg, ...) { va_list ap; va_start(ap, msg); logger(0, msg, ap); va_end(ap); }
void logDebug(Logger logger, _Printf_format_string_ const char* msg, ...) { va_list ap; va_start(ap, msg); logger(1, msg, ap); va_end(ap); }
void logInfo(Logger logger, _Printf_format_string_ const char* msg, ...) { va_list ap; va_start(ap, msg); logger(2, msg, ap); va_end(ap); }
void logWarning(Logger logger, _Printf_format_string_ const char* msg, ...) { va_list ap; va_start(ap, msg); logger(3, msg, ap); va_end(ap); }
void logError(Logger logger, _Printf_format_string_ const char* msg, ...) { va_list ap; va_start(ap, msg); logger(4, msg, ap); va_end(ap); }

void BufferBase::free()
{
  if (ptr) ::free(ptr - sizeof(size_t));
}

void* xmalloc(size_t size)
{
  auto rv = malloc(size);
  if (rv != nullptr) return rv;

  fprintf(stderr, "Failed to allocate memory.");
  exit(-1);
}

void* xcalloc(size_t count, size_t size)
{
  auto rv = calloc(count, size);
  if (rv != nullptr) return rv;

  fprintf(stderr, "Failed to allocate memory.");
  exit(-1);
}

void* xrealloc(void* ptr, size_t size)
{
  auto* rv = realloc(ptr, size);
  if (rv != nullptr) return rv;

  fprintf(stderr, "Failed to allocate memory.");
  exit(-1);
}

void* Arena::alloc(size_t bytes)
{
  const size_t pageSize = 1024 * 1024;

  if (bytes == 0) return nullptr;

  size_t padded = (bytes + 7) & ~7;

  if (size < fill + padded) {
    fill = sizeof(uint8_t*);
    size = std::max(pageSize, fill + padded);

    auto* page = (uint8_t*)xmalloc(size);
    *(uint8_t**)page = nullptr;

    if (first == nullptr) {
      first = page;
      curr = page;
    }
    else {
      *(uint8_t**)curr = page; // update next
      curr = page;
    }
  }

  assert(first != nullptr);
  assert(curr != nullptr);
  assert(*(uint8_t**)curr == nullptr);
  assert(fill + padded <= size);

  uint8_t* rv = curr + fill;
  fill += padded;
  return rv;
}

void* Arena::dup(const void* src, size_t bytes)
{
  auto* dst = alloc(bytes);
  std::memcpy(dst, src, bytes);
  return dst;
}


void Arena::clear()
{
  auto* c = first;
  while (c != nullptr) {
    auto* n = *(uint8_t**)c;
    free(c);
    c = n;
  }
  first = nullptr;
  curr = nullptr;
  fill = 0;
  size = 0;
}