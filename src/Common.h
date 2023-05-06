#pragma once
// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cassert>

typedef void(*Logger)(size_t level, const char* msg, va_list arg);

#if defined(_MSC_VER)
void logTrace(Logger logger, _Printf_format_string_ const char* msg, ...);
void logDebug(Logger logger, _Printf_format_string_ const char* msg, ...);
void logInfo(Logger logger, _Printf_format_string_ const char* msg, ...);
void logWarning(Logger logger, _Printf_format_string_ const char* msg, ...);
void logError(Logger logger, _Printf_format_string_ const char* msg, ...);

#elif defined(__clang__)
__attribute__((__format__(__printf__, 2, 3))) void logTrace(Logger logger, const char* msg, ...);
__attribute__((__format__(__printf__, 2, 3))) void logDebug(Logger logger, const char* msg, ...);
__attribute__((__format__(__printf__, 2, 3))) void logInfo(Logger logger, const char* msg, ...);
__attribute__((__format__(__printf__, 2, 3))) void logWarning(Logger logger, const char* msg, ...);
__attribute__((__format__(__printf__, 2, 3))) void logError(Logger logger, const char* msg, ...);

#elif defined(__GNUC__)
void logTrace(Logger logger, const char* msg, ...) __attribute__((format(printf, 2, 3)));
void logDebug(Logger logger, const char* msg, ...) __attribute__((format(printf, 2, 3)));
void logInfo(Logger logger, const char* msg, ...) __attribute__((format(printf, 2, 3)));
void logWarning(Logger logger, const char* msg, ...) __attribute__((format(printf, 2, 3)));
void logError(Logger logger, const char* msg, ...) __attribute__((format(printf, 2, 3)));

#else
void logTrace(Logger logger, const char* msg, ...);
void logDebug(Logger logger, const char* msg, ...);
void logInfo(Logger logger, const char* msg, ...);
void logWarning(Logger logger, const char* msg, ...);
void logError(Logger logger, const char* msg, ...);

#endif


struct E57File;

void* xmalloc(size_t size);
void* xcalloc(size_t count, size_t size);
void* xrealloc(void* ptr, size_t size);

inline uint16_t getUint16LE(const void* ptr)
{
  const uint8_t* q = reinterpret_cast<const uint8_t*>(ptr);
  return uint16_t(size_t(q[1]) << 8 | size_t(q[0]));
}

inline uint32_t readUint32LE(const char*& curr)
{
  const uint8_t* q = reinterpret_cast<const uint8_t*>(curr);
  uint32_t rv = uint32_t(q[3]) << 24 | uint32_t(q[2]) << 16 | uint32_t(q[1]) << 8 | uint32_t(q[0]);
  curr += sizeof(uint32_t);
  return rv;
}

inline uint64_t readUint64LE(const char*& curr)
{
  const uint8_t* q = reinterpret_cast<const uint8_t*>(curr);
  uint64_t rv =
    uint64_t(q[7]) << 56 | uint64_t(q[6]) << 48 | uint64_t(q[5]) << 40 | uint64_t(q[4]) << 32 |
    uint64_t(q[3]) << 24 | uint64_t(q[2]) << 16 | uint64_t(q[1]) << 8 | uint64_t(q[0]);
  curr += sizeof(uint64_t);
  return rv;
}


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

  size_t _size() const { return ptr ? ((size_t*)ptr)[-1] : 0; }

};

template<typename T>
struct Buffer : public BufferBase
{
  T* data() { return (T*)ptr; }
  T& operator[](size_t ix) { return data()[ix]; }
  const T* data() const { return (T*)ptr; }
  const T& operator[](size_t ix) const { return data()[ix]; }
  void accommodate(size_t count) { _accommodate(sizeof(T), count); }
  size_t size() const { return _size() / sizeof(T); }

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
  View(T* data_, size_t size_) { UninitializedView<T>::data = data_; UninitializedView<T>::size = size_; }
};
