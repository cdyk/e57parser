// This file is part of e57parser copyright 2023 Christopher Dyken
// Released under the MIT license, please see LICENSE file for details.

// Don't complain about fopen
#define _CRT_SECURE_NO_WARNINGS

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
#include <cstring>
#include <string>
#include <functional>
#include <cinttypes>

#include "Common.h"
#include "e57File.h"

namespace {

  size_t logLevel = 2;

  void logger(size_t level, const char* msg, va_list arg)
  {
    if (level < logLevel) {
      return;
    }
    static thread_local char buffer[512] = { "[*] " };

    if (5 <= level) {
      assert(false && "Invalid loglevel");
      return;
    }

    const char levels[5] = {'T', 'D', 'I', 'W', 'E'};
    buffer[1] = levels[level];

    constexpr size_t bufferSize = sizeof(buffer) - 4;
    int len = vsnprintf(buffer + 4, bufferSize, msg, arg);
    if (0 <= len && size_t(len) + 2 <= bufferSize) {
      buffer[4 + len] = '\n';
      fwrite(buffer, 1, 4 + len + 1, stderr);
    }
  }

  using ProcessFileFunc = std::function<bool(const char* ptr, size_t size)>;

#ifdef _WIN32
  struct MemoryMappedFile
  {
    MemoryMappedFile(const char* path)
    {
      h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (h == INVALID_HANDLE_VALUE) {
        logError(logger, "CreateFileA returned INVALID_HANDLE_VALUE");
        return;
      }
      DWORD hiSize;
      DWORD loSize = GetFileSize(h, &hiSize);
      size = (size_t(hiSize) << 32u) + loSize;

      m = CreateFileMappingA(h, 0, PAGE_READONLY, 0, 0, NULL);
      if (m == INVALID_HANDLE_VALUE) {
        logError(logger, "CreateFileMappingA returned INVALID_HANDLE_VALUE");
        return;

      }
      assert(m);

      ptr = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
      if (ptr == nullptr) {
        logError(logger, "MapViewOfFile returned INVALID_HANDLE_VALUE");
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
    void* ptr = nullptr;
    size_t size = 0;
    bool good = false;
  };
#else
  struct MemoryMappedFile
  {
    MemoryMappedFile(const char* path)
    {
      fd = open(path, O_RDONLY);
      if (fd == -1) {
        logError(logger, "%s: open failed: %s", path, strerror(errno));
        return;
      }

      struct stat stat {};
      if (fstat(fd, &stat) != 0) {
        logError(logger, "%s: fstat failed: %s", path, strerror(errno));
        return;
      }
      size = stat.st_size;

#ifdef __linux__
      ptr = mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
#else
      ptr = mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
#endif
      if (ptr == MAP_FAILED) {
        logError(logger, "%s: mmap failed: %s", path, strerror(errno));
        return;
      }

      if (madvise(ptr, stat.st_size, MADV_SEQUENTIAL) != 0) {
        logError(logger, "%s: madvise(MADV_SEQUENTIAL) failed: %s", path, strerror(errno));
        return;
      }
      good = true;
    }

    ~MemoryMappedFile()
    {
      if (ptr != MAP_FAILED) {
        if (munmap(ptr, size) != 0) {
          logError(logger, "munmap failed: %s", strerror(errno));
        }
        ptr = MAP_FAILED;
      }
      if (fd != -1) {
        close(fd);
        fd = -1;
      }
    }

    void* ptr = nullptr;
    size_t size = 0;
    int fd = -1;
    bool good = false;
  };
#endif



  View<const char> memoryMappedFileCallback(void* callbackData, uint64_t offset, uint64_t size)
  {
    const MemoryMappedFile* mappedFile = static_cast<const MemoryMappedFile*>(callbackData);
    if (!mappedFile->good || mappedFile->size < offset || mappedFile->size < offset + size) {
      return View<const char>(nullptr, 0);
    }
    return View<const char>((const char*)mappedFile->ptr + static_cast<size_t>(offset), size);
  }

 

  struct PtsWriter
  {
    std::vector<ComponentWriteDesc> writeDescs;
    Buffer<char> buffer;
    size_t pointCapacity = 5;
    FILE* file = nullptr;

    bool addComponent(const Points& pts, size_t index, Component::Role role)
    {
      for (size_t i = 0; pts.components.size; i++) {
        if (pts.components[i].role == role) {
          writeDescs.push_back({
            .offset = index * sizeof(float),
            .stride = 3 * sizeof(float),
            .type = ComponentWriteDesc::Type::Float,
            .stream = static_cast<uint32_t>(i) });
          return true;
        }
      }
      return false;
    }

    bool init(const char* path, const Points& pts)
    {
      file = std::fopen(path, "w");
      if (!file) {
        logError(logger, "Failed to open '%s' for writing\n", path);
        return false;
      }
      fprintf(file, "%" PRIu64 "\n", pts.recordCount);

      if (!addComponent(pts, 0, Component::Role::CartesianX)) {
        logError(logger, "No cartesian X component");
        return false;
      }
      if (!addComponent(pts, 1, Component::Role::CartesianY)) {
        logError(logger, "No cartesian Y component");
        return false;
      }
      if (!addComponent(pts, 2, Component::Role::CartesianZ)) {
        logError(logger, "No cartesian Z component");
        return false;
      }
      assert(writeDescs.size() == 3);

      buffer.accommodate(pointCapacity * 3 * sizeof(float));
      return true;
    }

    bool destroy()
    {
      if (file) {
        std::fclose(file);
        file = nullptr;
      }
      return true;
    }

    static bool consumeCallback(void* data, size_t pointCount)
    {
      PtsWriter* that = reinterpret_cast<PtsWriter*>(data);
      const float* ptr = reinterpret_cast<const float*>(that->buffer.data());
      for (size_t i = 0; i < pointCount; i++) {
        fprintf(that->file, "%f %f %f\n", ptr[3 * i + 0], ptr[3 * i + 1], ptr[3 * i + 2]);
      }
      return true;
    }
  };


  void printHelp(const char* path)
  {
    fprintf(stderr, R"help(
Usage: %s [options] <filename>.e57

Reads the specified E57-file and performs the operations specified by the
command line options. All options can occur multiple times where that makes
sense.

Options:
  --help                       This help text.
  --info                       Output info about the E57 file contents.
  --loglevel=<uint>            Specifies amount of logging, 0=trace,
                               1=debug, 2=info, 3=warnings, 4=errors,
                               5=silent.
  --pointset=<uint>            Selects which point set to process, defaults
                               to 0.
  --include-invalid=<bool>     If enabled, also output points where
                               InvalidState is set. Defaults to false.
  --output-xml=<filename.xml>  Write the embedded XML to a file.
  --output-pts=<filename.pts>  Write the selected point set to file as pts.

Post bug reports or questions at https://github.com/cdyk/e57parser
)help", path);
  }

  bool parseBool(bool& output, const char* ptr, size_t offset)
  {
    std::string lowcasearg;
    for (; ptr[offset] != '\0'; offset++) {
      lowcasearg.push_back(std::tolower(ptr[offset]));
    }
    if (lowcasearg == "true" || lowcasearg == "1" || lowcasearg == "yes") {
      output = true;
      return true;
    }
    else if (lowcasearg == "false" || lowcasearg == "0" || lowcasearg == "no") {
      output = false;
      return true;
    }
    else {
      logError(logger, "%.*s: invalid bool value '%s'", std::max(1, int(offset) - 1), ptr, ptr + offset);
      return false;
    }
  }

  bool parseUint(size_t& output, const char* ptr, size_t offset)
  {
    if (ptr[offset] == '\0') {
      goto fail;
    }
    output = 0;
    do {
      if ((ptr[offset] < '0') || ('9' < ptr[offset])) {
        goto fail;
      }
      output = 10 * output + static_cast<size_t>(ptr[offset] - '0');
    }
    while (ptr[++offset] != '\0');
    return true;

  fail:
    logError(logger, "%.*s: invalid unsigned int value '%s'", std::max(1, int(offset) - 1), ptr, ptr + offset);
    return false;
  }


}




int main(int argc, char** argv)
{
  static const std::string option_help            = "--help";
  static const std::string option_info            = "--info";
  static const std::string option_loglevel        = "--loglevel=";
  static const std::string option_pointset        = "--pointset=";
  static const std::string option_include_invalid = "--include-invalid=";
  static const std::string option_output_xml      = "--output-xml=";
  static const std::string option_output_pts      = "--output-pts=";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], option_help.c_str()) == 0) {
      printHelp(argv[0]);
      return EXIT_SUCCESS;
    }
    else if (strncmp(argv[i], option_loglevel.c_str(), option_loglevel.length()) == 0) {
      size_t newlevel = 0;
      if (!parseUint(newlevel, argv[i], option_loglevel.length())) {
        return EXIT_FAILURE;
      }
      if (4 < newlevel) {
        logError(logger, "Invalid loglevel %zu", newlevel);
        return EXIT_FAILURE;
      }
      logLevel = newlevel;
    }
  }

  if (argc < 2 || argv[argc - 1][0] == '-') {
    printHelp(argv[0]);
    return EXIT_FAILURE;
  }


  bool success = true;
  const char* inpath = argv[argc - 1];
  {    
    MemoryMappedFile mappedFile(inpath);

    E57File e57;
    if(!openE57(e57, logger, memoryMappedFileCallback, &mappedFile, mappedFile.size)) {
      success = false;
    }
    else {
      logDebug(logger, "Opened '%s'", inpath);

      bool includeInvalid = false;
      size_t pointSet = 0;

      for (int i = 1; success && i + 1 < argc; i++) {

        // Help and loglevel handled further up.
        if (strcmp(argv[i], option_help.c_str()) == 0) {}
        else if (strncmp(argv[i], option_loglevel.c_str(), option_loglevel.length()) == 0) {}

        // Output info about the e57 file
        else if (strcmp(argv[i], option_info.c_str()) == 0) {
          logInfo(logger, "path=%s", inpath);
          logInfo(logger, "version=%" PRIu32 ".%" PRIu32 ", length=%" PRIu64 ", xmlOffset=%" PRIu64 ", xmlLength=%" PRIu64 ", pageSize=%" PRIu64,
                   e57.header.major, e57.header.minor,
                   e57.header.filePhysicalLength,
                   e57.header.xmlPhysicalOffset, e57.header.xmlLogicalLength,
                   e57.header.pageSize);

          for (size_t j = 0; j < e57.points.size; j++) {
            const Points& points = e57.points[j];
            logInfo(logger, "pointset %zu: fileOffset=%" PRIu64 " recordCount=%" PRIu64, j, points.fileOffset, points.recordCount);
            for (size_t i = 0; i < points.components.size; i++) {
              const Component& comp = points.components[i];
              switch (comp.type) {
              case Component::Type::Integer:
                logInfo(logger, "   attribute %zu: integer min=%" PRId64 " max = %" PRId64, i, comp.integer.min, comp.integer.max);
                break;
              case Component::Type::ScaledInteger:
                logInfo(logger, "   attribute %zu: scaled integer min=%" PRId64 " max=%" PRId64 " scale=%f offset=%f", i, comp.integer.min, comp.integer.max, comp.integer.scale, comp.integer.offset);
                break;
              case Component::Type::Float:
                logInfo(logger, "   attribute %zu: float min=%f max=%f", i, comp.real.min, comp.real.max);
                break;
              case Component::Type::Double:
                logInfo(logger, "   attribute %zu: double min=%f max=%f", i, comp.real.min, comp.real.max);
                break;
              default:
                assert(false);
                break;
              }
            }
          }
        }

        // Specify point set
        else if (strncmp(argv[i], option_pointset.c_str(), option_pointset.length()) == 0) {
          if (!parseUint(pointSet, argv[i], option_pointset.length())) {
            success = false;
          }
          else if (e57.points.size <= pointSet) {
            logError(logger, "specified point set index %zu is greater than the number of point sets (=%zu)", pointSet, e57.points.size);
            success = false;
          }
        }

        // Enable or disable inclusion of invalid points
        else if (strncmp(argv[i], option_include_invalid.c_str(), option_include_invalid.length()) == 0) {
          if (!parseBool(includeInvalid, argv[i], option_include_invalid.length())) {
            success = false;
          }
        }

        // Output embedded xml
        else if (strncmp(argv[i], option_output_xml.c_str(), option_output_xml.length()) == 0) {
          const char* path = argv[i] + option_output_xml.length();

          Buffer<char> xml;
          xml.accommodate(e57.header.xmlLogicalLength);
          uint64_t xmlPhysicalOffset = e57.header.xmlPhysicalOffset;
          if (!readE57Bytes(&e57, logger, xml.data(), xmlPhysicalOffset, e57.header.xmlLogicalLength)) {
            success = false;
          }
          else {
            FILE* file = std::fopen(path, "w");
            if (!file) {
              logError(logger, "Failed to open '%s' for writing\n", path);
              success = false;
            }
            else {
              std::fwrite(xml.data(), 1, e57.header.xmlLogicalLength, file);
              std::fclose(file);
              logDebug(logger, "Wrote XML to %s", path);
            }
          }
        }

        // Output point set as pts
        else if (strncmp(argv[i], option_output_pts.c_str(), option_output_pts.length()) == 0) {
          const char* path = argv[i] + option_output_pts.length();

          if (e57.points.size <= pointSet) {
            logError(logger, "specified point set index %zu is greater than the number of point sets (=%zu)", pointSet, e57.points.size);
            success = false;
          }
          else {
            const Points& pts = e57.points[pointSet];

            PtsWriter writer;
            if (!writer.init(path, pts)) {
              success = false;
            }
            else {
              ReadPointsArgs readPointsArgs{
                .buffer = View<char>(writer.buffer.data(), writer.buffer.size()),
                .writeDesc = View<const ComponentWriteDesc>(writer.writeDescs.data(), writer.writeDescs.size()),
                .consumeCallback = PtsWriter::consumeCallback,
                .consumeCallbackData = &writer,
                .pointCapacity = writer.pointCapacity,
                .pointSetIndex = pointSet
              };

              if (!readE57Points(&e57, logger, readPointsArgs)) {
                success = false;
              }
            }
          }
        }
        else {
          logError(logger, "Unrecoginzed command line option '%s'", argv[i]);
          success = false;
        }
      }
    }
  }

  if (success) {
    logDebug(logger, "Parsed '%s' successfully", inpath);
    return EXIT_SUCCESS;
  }
  else {
    logError(logger, "Failed to parse %s", inpath);
    return EXIT_FAILURE;
  }
}