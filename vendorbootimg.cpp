#include "vendorbootimg.h"
#include <fstream>
#include <iostream>

namespace {
constexpr std::string_view VENDOR_BOOT_MAGIC = "VNDRBOOT";
constexpr size_t VENDOR_BOOT_MAGIC_SIZE = 8;
constexpr size_t VENDOR_RAMDISK_NAME_SIZE = 32;
constexpr size_t VENDOR_RAMDISK_TABLE_ENTRY_V4_SIZE = 108;
} // namespace

bool VendorBootBuilder::Build() {
  std::ofstream out(args.output, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open vendor boot output: " << args.output << "\n";
    return false;
  }

  if (args.header_version > 3 && !args.vendor_ramdisk.empty()) {
    VendorRamdiskEntry MainEntry;
    MainEntry.name = "";
    MainEntry.type = VENDOR_RAMDISK_TYPE_PLATFORM;
    MainEntry.path = args.vendor_ramdisk;
    args.vendor_ramdisk.clear();
    args.ramdisks.insert(args.ramdisks.begin(), MainEntry);
  }

  if (args.header_version > 3) {
    for (const auto &entry : args.ramdisks) {
      ramdisk_total_size += utils::GetFileSize(entry.path);
    }
  } else {
    ramdisk_total_size = utils::GetFileSize(args.vendor_ramdisk);
  }

  if (!WriteHeader(out))
    return false;
  if (!WriteRamdisks(out))
    return false;

  if (auto dtb = utils::OpenFile(args.dtb)) {
    auto data = utils::ReadFileContents(*dtb);
    out.write(reinterpret_cast<const char *>(data.data()), data.size());
    utils::PadFile(out, args.page_size);
  }

  if (args.header_version > 3) {
    if (!WriteTableEntries(out))
      return false;

    if (auto bc = utils::OpenFile(args.bootconfig)) {
      auto data = utils::ReadFileContents(*bc);
      out.write(reinterpret_cast<const char *>(data.data()), data.size());
      utils::PadFile(out, args.page_size);
    }
  }

  return true;
}

bool VendorBootBuilder::WriteHeader(std::ostream &out) {
  out.write(VENDOR_BOOT_MAGIC.data(), VENDOR_BOOT_MAGIC_SIZE);
  utils::WriteU32(out, args.header_version);
  utils::WriteU32(out, args.page_size);
  utils::WriteU32(out, args.base + args.kernel_offset);
  utils::WriteU32(out, args.base + args.ramdisk_offset);
  utils::WriteU32(out, static_cast<uint32_t>(ramdisk_total_size));

  std::vector<char> cmdline(2048, 0);
  out.write(cmdline.data(), cmdline.size());

  utils::WriteU32(out, args.base + args.tags_offset);

  std::vector<char> board(16, 0);
  out.write(board.data(), board.size());

  const uint32_t header_size = args.header_version > 3 ? VENDOR_BOOT_IMAGE_HEADER_V4_SIZE : VENDOR_BOOT_IMAGE_HEADER_V3_SIZE;
  utils::WriteU32(out, header_size);
  utils::WriteU32(out, utils::GetFileSize(args.dtb));
  utils::WriteU64(out, args.base + args.dtb_offset);

  if (args.header_version > 3) {
    const uint32_t table_size = static_cast<uint32_t>(
        args.ramdisks.size() * VENDOR_RAMDISK_TABLE_ENTRY_V4_SIZE);
    utils::WriteU32(out, table_size);
    utils::WriteU32(out, static_cast<uint32_t>(args.ramdisks.size()));
    utils::WriteU32(out, VENDOR_RAMDISK_TABLE_ENTRY_V4_SIZE);
    utils::WriteU32(out, utils::GetFileSize(args.bootconfig));
  }

  utils::PadFile(out, args.page_size);
  return true;
}

bool VendorBootBuilder::WriteRamdisks(std::ostream &out) {
  if (args.header_version > 3) {
    for (const auto &entry : args.ramdisks) {
      if (auto file = utils::OpenFile(entry.path)) {
        auto data = utils::ReadFileContents(*file);
        out.write(reinterpret_cast<const char *>(data.data()), data.size());
      }
    }
  } else {
    if (auto file = utils::OpenFile(args.vendor_ramdisk)) {
      auto data = utils::ReadFileContents(*file);
      out.write(reinterpret_cast<const char *>(data.data()), data.size());
    }
  }
  utils::PadFile(out, args.page_size);
  return true;
}

bool VendorBootBuilder::WriteTableEntries(std::ostream &out) {
  uint32_t offset = 0;
  for (const auto &entry : args.ramdisks) {
    const auto size = static_cast<uint32_t>(utils::GetFileSize(entry.path));
    utils::WriteU32(out, size);
    utils::WriteU32(out, offset);
    utils::WriteU32(out, entry.type);

    std::vector<char> name(VENDOR_RAMDISK_NAME_SIZE, 0);
    std::copy_n(entry.name.begin(),
                std::min(entry.name.size(), VENDOR_RAMDISK_NAME_SIZE - 1),
                name.begin());
    out.write(name.data(), name.size());

    // TODO: Support board_id? Useless in most cases tho.
    for (const auto &id : entry.board_id) {
      utils::WriteU32(out, 0);
    }
    offset += size;
  }
  utils::PadFile(out, args.page_size);
  return true;
}
