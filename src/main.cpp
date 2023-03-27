// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#else

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#endif

#include <cstdlib>
#include <cstdio>
#include <functional>

#include "Common.h"
#include "e57File.h"

namespace {

  void logger(unsigned level, const char* msg, ...)
  {
    switch (level) {
    case 0: fprintf(stderr, "[I] "); break;
    case 1: fprintf(stderr, "[W] "); break;
    case 2: fprintf(stderr, "[E] "); break;
    }

    va_list argptr;
    va_start(argptr, msg);
    vfprintf(stderr, msg, argptr);
    va_end(argptr);
    fprintf(stderr, "\n");
  }

  using ProcessFileFunc = std::function<bool(const char* ptr, size_t size)>;

#ifdef _WIN32
  struct MemoryMappedFile
  {
    MemoryMappedFile(const char* path)
    {
      h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (h == INVALID_HANDLE_VALUE) {
        logger(2, "CreateFileA returned INVALID_HANDLE_VALUE");
        return;
      }
      DWORD hiSize;
      DWORD loSize = GetFileSize(h, &hiSize);
      size = (size_t(hiSize) << 32u) + loSize;

      m = CreateFileMappingA(h, 0, PAGE_READONLY, 0, 0, NULL);
      if (m == INVALID_HANDLE_VALUE) {
        logger(2, "CreateFileMappingA returned INVALID_HANDLE_VALUE");
        return;

      }
      ptr = static_cast<const char*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0));
      if (ptr == nullptr) {
        logger(2, "MapViewOfFile returned INVALID_HANDLE_VALUE");
        return;
      }
      good = true;
    }

    ~MemoryMappedFile()
    {
      if (ptr != nullptr) {
        UnmapViewOfFile(ptr);
        ptr = nullptr;
      }
      if (m != INVALID_HANDLE_VALUE) {
        CloseHandle(m);
        m = INVALID_HANDLE_VALUE;
      }
      if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
      }
    }

    HANDLE h = INVALID_HANDLE_VALUE;
    HANDLE m = INVALID_HANDLE_VALUE;
    const char* ptr = nullptr;
    size_t size = 0;
    bool good = false;
  };
#else
  struct MemoryMappedFile
  {
    MemoryMappedFile(const char* path, Logger logger)
    {
      fd = open(path, O_RDONLY);
      if (fd == -1) {
        logger(2, "%s: open failed: %s", path.c_str(), strerror(errno));
        return;
      }

      struct stat stat {};
      if (fstat(fd, &stat) != 0) {
        logger(2, "%s: fstat failed: %s", path.c_str(), strerror(errno));
        return;
      }
      size = stat.st_size;

#ifdef __linux__
      const char* ptr = static_cast<const char*>(mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0));
#else
      const char* ptr = static_cast<const char*>(mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
#endif
      if (ptr == MAP_FAILED) {
        logger(2, "%s: mmap failed: %s", path.c_str(), strerror(errno));
        return false;
      }

      if (madvise(ptr, stat.st_size, MADV_SEQUENTIAL) != 0) {
        logger(1, "%s: madvise(MADV_SEQUENTIAL) failed: %s", path.c_str(), strerror(errno));
        return false;
      }
      good = true;
    }

    ~MemoryMappedFile()
    {
      if (ptr != MAP_FAILED) {
        if (munmap(ptr, size) != 0) {
          logger(2, "munmap failed: %s", strerror(errno));
        }
        ptr = MAP_FAILED;
      }
      if (fd != -1) {
        close(fd);
        fd = -1;
      }
    }

    const char* ptr = MAP_FAILED;
    size_t size = 0;
    int fd = -1;
    bool good = false;
  };
#endif

}



int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <filename.e57>\n", argv[0]);
    return EXIT_FAILURE;
  }

  bool success = true;
  {
    MemoryMappedFile mappedFile(argv[1]);

    View<const char> bytes(mappedFile.ptr, mappedFile.size);
    E57File* e57 = openE57(logger, bytes);
    if (e57) {
      success = false;
    }
    else {
      for (size_t i = 0; i < e57->points.size; i++) {
        if (!parseE57CompressedVector(e57, logger, i)) {
          success = false;
          break;
        }
      }
    }
  }

  if (success) {
    logger(0, "Parsed '%s' successfully", argv[1]);
    return EXIT_SUCCESS;
  }
  else {
    logger(2, "Failed to parse %s", argv[1]);
    return EXIT_FAILURE;
  }
}