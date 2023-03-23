#pragma once
// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#include <cstdint>
#include <cstddef>

typedef void(*Logger)(unsigned level, const char* msg, ...);

struct E57File;

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

struct Arena
{
  Arena() = default;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  ~Arena() { clear(); }

  uint8_t* first = nullptr;
  uint8_t* curr = nullptr;
  size_t fill = 0;
  size_t size = 0;

  void* alloc(size_t bytes);
  void* dup(const void* src, size_t bytes);
  void clear();

  template<typename T> T* alloc() { return new(alloc(sizeof(T))) T(); }

  template<typename T> T* allocArray(size_t arrayLength)
  {
    T* p = static_cast<T*>(alloc(sizeof(T) * arrayLength));
    for (size_t i = 0; i < arrayLength; ++i) {
      new(p + i) T();
    }
    return p;
  }
};



template<typename T>
struct UninitializedListHeader
{
  T* first;
  T* last;

  void init()
  {
    first = last = nullptr;
  }

  // Number of items in list, linear complexity.
  size_t size()
  {
    size_t rv = 0;
    for (T* item = first; item; item = item->next) { rv++; }
    return rv;
  }

  void pushBack(T* item)
  {
    if (first == nullptr) {
      first = last = item;
    }
    else {
      last->next = item;
      last = item;
    }
  }
};

template<typename T>
struct ListHeader : public UninitializedListHeader<T>
{
  ListHeader() { UninitializedListHeader<T>::init(); }
};

template<typename T>
struct UninitializedView
{
  T* data;
  size_t size;

  void init() { data = nullptr; size = 0; }
  T& operator[](size_t ix) noexcept { assert(data && (ix < size)); return data[ix]; }
  const T& operator[](size_t ix) const noexcept { assert(data && (ix < size)); return data[ix]; }
};

template<typename T>
struct View : public UninitializedView<T>
{
  View() { UninitializedView<T>::init(); }
};

bool e57Parser(Logger logger, const char* path, const char* ptr, size_t size);
bool parseE57Xml(E57File* e57File, Logger logger, const char* xmlBytes, size_t xmlLength);
