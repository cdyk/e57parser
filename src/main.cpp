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

  bool processFile(const char* path, ProcessFileFunc f)
  {
    bool rv = false;
#ifdef _WIN32

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
      logger(2, "CreateFileA returned INVALID_HANDLE_VALUE");
      rv = false;
    }
    else {
      DWORD hiSize;
      DWORD loSize = GetFileSize(h, &hiSize);
      size_t fileSize = (size_t(hiSize) << 32u) + loSize;

      HANDLE m = CreateFileMappingA(h, 0, PAGE_READONLY, 0, 0, NULL);
      if (m == INVALID_HANDLE_VALUE) {
        logger(2, "CreateFileMappingA returned INVALID_HANDLE_VALUE");
        rv = false;
      }
      else {
        const char* ptr = static_cast<const char*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0));
        if (ptr == nullptr) {
          logger(2, "MapViewOfFile returned INVALID_HANDLE_VALUE");
          rv = false;
        }
        else {
          rv = f(ptr, fileSize);
          UnmapViewOfFile(ptr);
        }
        CloseHandle(m);
      }
      CloseHandle(h);
    }

#else

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
      logger(2, "%s: open failed: %s", path.c_str(), strerror(errno));
    }
    else {
      struct stat stat {};
      if (fstat(fd, &stat) != 0) {
        logger(2, "%s: fstat failed: %s", path.c_str(), strerror(errno));
      }
      else {

#ifdef __linux__
        const char* ptr = static_cast<const char*>(mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0));
#else
        const char* ptr = static_cast<const char*>(mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
#endif
        if (ptr == MAP_FAILED) {
          logger(2, "%s: mmap failed: %s", path.c_str(), strerror(errno));
        }
        else {
          if (madvise(ptr, stat.st_size, MADV_SEQUENTIAL) != 0) {
            logger(1, "%s: madvise(MADV_SEQUENTIAL) failed: %s", path.c_str(), strerror(errno));
          }
          rv = f(ptr, stat.st_size);
          if (munmap(ptr, stat.st_size) != 0) {
            logger(2, "%s: munmap failed: %s", path.c_str(), strerror(errno));
            rv = false;
          }
        }
      }
    }

#endif
    return rv;
  }




}



int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <filename.e57>\n", argv[0]);
    return EXIT_FAILURE;
  }

  if (!processFile(argv[1], [path = argv[1]](const char* ptr, size_t size)->bool { return e57Parser(logger, path, ptr, size); })) {
    logger(2, "Failed to parse '%s'", argv[1]);
    return EXIT_FAILURE;
  }
  logger(0, "Parsed '%s' successfully", argv[1]);
  return EXIT_SUCCESS;
}