#include <cstdlib>
#include <cstdio>

#include "Common.h"

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
