#include "utils.h"
#include <fstream>
#include <iomanip>
#include <regex>
#include <system_error>

namespace utils {

void WriteS32(std::ostream &stream, const std::string &value) {
    std::array<char, 32> bytes{}; 

    size_t len = std::min(value.size(), static_cast<size_t>(32));
    std::copy_n(value.data(), len, bytes.data());
    stream.write(bytes.data(), bytes.size());
}

void WriteU32(std::ostream &stream, uint32_t value) {
  const std::array<uint8_t, 4> bytes{
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF)};
  stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void WriteU64(std::ostream &stream, uint64_t value) {
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

std::optional<FileWrapper> OpenFile(const std::filesystem::path &path) {
  auto file =
      std::make_unique<std::ifstream>(path, std::ios::binary | std::ios::ate);
  if (!file->good())
    return std::nullopt;

  size_t size = file->tellg();
  file->seekg(0);
  return FileWrapper{std::move(file), size};
}

std::vector<uint8_t> ReadFileContents(FileWrapper &file) {
  std::vector<uint8_t> buffer(file.size);
  if (!file.stream->read(reinterpret_cast<char *>(buffer.data()), file.size)) {
    return {};
  }
  return buffer;
}

size_t GetFileSize(FileWrapper &file) { return file.size; }
size_t GetFileSize(std::optional<FileWrapper>& file) {
  if (file) return GetFileSize(*file);
  return 0;
}

size_t GetFileSize(const std::filesystem::path& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : size;
}

void PadFile(std::ostream &out, size_t padding) {
  if (padding == 0)
    return;
  size_t pos = out.tellp();
  size_t pad = (padding - (pos % padding)) % padding;
  out.write(std::string(pad, '\0').c_str(), pad);
}

std::optional<std::vector<char>>
AsciizString::operator()(const std::string &s) const {
  if (s.size() + 1 > max_length)
    return std::nullopt;
  std::vector<char> result(s.begin(), s.end());
  result.push_back('\0');
  result.resize(max_length, '\0');
  return result;
}

std::optional<OSVersion> OSVersion::Parse(const std::string &s) {
  std::regex version_re(R"((\d{1,3})(?:\.(\d{1,3})(?:\.(\d{1,3}))?)?)");
  std::smatch match;
  if (std::regex_search(s, match, version_re)) {
    try {
      uint32_t a = std::stoi(match[1]);
      uint32_t b = match[2].matched ? std::stoi(match[2]) : 0;
      uint32_t c = match[3].matched ? std::stoi(match[3]) : 0;

      if (a >= 128 || b >= 128 || c >= 128)
        return std::nullopt;
      return OSVersion{(a << 14) | (b << 7) | c, 0};
    } catch (...) {
    }
  }
  return std::nullopt;
}

} // namespace utils
