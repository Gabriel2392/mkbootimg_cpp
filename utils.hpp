#pragma once

#include "TinySHA1.hpp"
#include <algorithm>
#include <iostream>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <stdexcept>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

namespace utils {

inline void WriteS32(std::ostream &stream, const std::string &value) {
  std::array<char, 32> bytes{};
  size_t len = std::min(value.size(), static_cast<size_t>(32));
  std::copy_n(value.data(), len, bytes.data());
  stream.write(bytes.data(), bytes.size());
}

inline void WriteU32(std::ostream &stream, uint32_t value) {
  const std::array<uint8_t, 4> bytes{
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF)};
  stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

inline void WriteU64(std::ostream &stream, uint64_t value) {
  const std::array<uint8_t, 8> bytes{
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF),
      static_cast<uint8_t>((value >> 32) & 0xFF),
      static_cast<uint8_t>((value >> 40) & 0xFF),
      static_cast<uint8_t>((value >> 48) & 0xFF),
      static_cast<uint8_t>((value >> 56) & 0xFF)};
  stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

inline std::string UToS(uint32_t value) {
  std::string result;
  result.reserve(4);
  result.push_back(static_cast<char>((value >> 24) & 0xFF));
  result.push_back(static_cast<char>((value >> 16) & 0xFF));
  result.push_back(static_cast<char>((value >> 8) & 0xFF));
  result.push_back(static_cast<char>(value & 0xFF));
  return result;
}

struct FileWrapper {
  std::unique_ptr<std::istream> stream;
  size_t size = 0;
  explicit operator bool() const { return stream && stream->good(); }
};

inline std::optional<FileWrapper> OpenFile(const std::filesystem::path &path) {
  auto file =
      std::make_unique<std::ifstream>(path, std::ios::binary | std::ios::ate);
  if (!*file)
    return std::nullopt;

  std::streampos size = file->tellg();
  if (size == std::streampos(-1)) {
      return std::nullopt;
  }
  file->seekg(0);
  if (!*file) {
      return std::nullopt;
  }
  return FileWrapper{std::move(file), static_cast<size_t>(size)};
}

inline std::vector<uint8_t> ReadFileContents(FileWrapper &file) {
  std::vector<uint8_t> buffer(file.size);
  if (file.size > 0 && !file.stream->read(reinterpret_cast<char *>(buffer.data()), file.size)) {
    buffer.clear();
    buffer.shrink_to_fit();
  }
  return buffer;
}

inline size_t GetFileSize(FileWrapper &file) { return file.size; }

inline size_t GetFileSize(std::optional<FileWrapper> &file) {
  return file ? GetFileSize(*file) : 0;
}

inline size_t GetFileSize(const std::filesystem::path &path) {
  std::error_code ec;
  auto size = std::filesystem::file_size(path, ec);
  return ec ? 0 : size;
}

inline void PadFile(std::ostream &out, size_t padding) {
  if (padding == 0)
    return;
  std::streampos pos = out.tellp();
   if (pos == std::streampos(-1)) return; // Error check
  size_t current_pos = static_cast<size_t>(pos);
  size_t pad = (padding - (current_pos % padding)) % padding;
  if (pad > 0) {
    constexpr size_t buffer_size = 1024;
    std::array<char, buffer_size> zeros{}; // Already zero-initialized
    size_t remaining = pad;
    while (remaining > 0) {
        size_t to_write = std::min(remaining, buffer_size);
        out.write(zeros.data(), to_write);
        remaining -= to_write;
    }
  }
}

inline uint32_t GetNumberOfPages(uint32_t image_size, uint32_t page_size) {
    if (page_size == 0) return 0; // Avoid division by zero
    return (image_size + page_size - 1) / page_size;
}

class AsciizString {
  size_t max_length;

public:
  explicit AsciizString(size_t max_len) : max_length(max_len) {}

  inline std::optional<std::vector<char>> operator()(const std::string &s) const;
};

inline std::optional<std::vector<char>>
AsciizString::operator()(const std::string &s) const {
  if (s.size() >= max_length) // Need space for null terminator
    return std::nullopt;
  std::vector<char> result(max_length, '\0');
  std::copy(s.begin(), s.end(), result.begin());
  // Null terminator is already set by vector initialization or copy if s.size() < max_length -1
  // If s.size() == max_length - 1, the last element result[max_length-1] needs to be '\0', which it is.
  return result;
}


namespace { // Anonymous namespace for internal linkage helper
inline uint32_t ParseOSPatchLevel(const std::string &s) {
  static const std::regex patch_re(R"(^(\d{4})-(\d{2})(?:-(\d{2}))?)");
  std::smatch match;
  if (std::regex_search(s, match, patch_re)) {
    try {
      int year = std::stoi(match[1]);
      int month = std::stoi(match[2]);

      // Basic validation
      if (month < 1 || month > 12) return 0;

      int y = year - 2000;
      if (y < 0 || y >= 128) return 0; // Fits in 7 bits

      // Result: 7 bits for year (offset 2000), 4 bits for month
      return (static_cast<uint32_t>(y) << 4) | static_cast<uint32_t>(month);
    } catch (const std::invalid_argument &) {
    } catch (const std::out_of_range &) { }
  }
  return 0;
}
} // anonymous namespace


struct OSVersion {
  uint32_t version = 0;
  uint32_t patch_level = 0;
  std::string version_str = "";
  std::string patch_level_str = "";

  static inline void Parse(OSVersion &os_version);
};


inline void OSVersion::Parse(OSVersion &os_version) {
  static const std::regex version_re(R"((\d{1,3})(?:\.(\d{1,3})(?:\.(\d{1,3}))?)?)");
  std::smatch match;
  os_version.version = 0; // Reset before parsing
  os_version.patch_level = ParseOSPatchLevel(os_version.patch_level_str);

  if (std::regex_search(os_version.version_str, match, version_re)) {
    try {
      uint32_t a = std::stoul(match[1]);
      uint32_t b = match[2].matched ? std::stoul(match[2]) : 0;
      uint32_t c = match[3].matched ? std::stoul(match[3]) : 0;

      // Ensure components fit within 7 bits each (0-127)
      if (a < 128 && b < 128 && c < 128) {
        // Result: 7 bits for A, 7 bits for B, 7 bits for C
        os_version.version = (a << 14) | (b << 7) | c;
      }
    } catch (const std::invalid_argument &) {
    } catch (const std::out_of_range &) { }
  }
}


} // namespace utils

namespace errors {

    class FileWriteError : public std::runtime_error {
    private:
        std::string context_info_;

        static std::string build_message(const std::string& context) {
            return "Error while writing " + context +
                " (out of space or file out of scope)";
        }

    public:
        explicit FileWriteError(const std::string& context)
            : std::runtime_error(build_message(context)), context_info_(context) {}

        explicit FileWriteError(const char* context)
            : std::runtime_error(build_message(context)), context_info_(context) {}

        const std::string& get_context() const noexcept { return context_info_; }
    };

} // namespace errors