#pragma once
#include <cstdint>
#include <cstddef>

typedef void(*Logger)(unsigned level, const char* msg, ...);


void* xmalloc(size_t size);
void* xcalloc(size_t count, size_t size);
void* xrealloc(void* ptr, size_t size);


struct BufferBase
{
protected:
  char* ptr = nullptr;

  ~BufferBase() { free(); }

  void free();

  void _accommodate(size_t typeSize, size_t count)
  {
    if (count == 0) return;
    if (ptr && count <= ((size_t*)ptr)[-1]) return;

    free();

    ptr = (char*)xmalloc(typeSize * count + sizeof(size_t)) + sizeof(size_t);
    ((size_t*)ptr)[-1] = count;
  }

};

template<typename T>
struct Buffer : public BufferBase
{
  T* data() { return (T*)ptr; }
  T& operator[](size_t ix) { return data()[ix]; }
  const T* data() const { return (T*)ptr; }
  const T& operator[](size_t ix) const { return data()[ix]; }
  void accommodate(size_t count) { _accommodate(sizeof(T), count); }
};


bool e57Parser(Logger logger, const char* path, const char* ptr, size_t size);
