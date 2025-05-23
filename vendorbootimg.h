#pragma once

#include "utils.hpp"

struct VendorRamdiskEntry {
  std::filesystem::path path;
  uint32_t type;
  std::string name;
  std::array<uint32_t, 16> board_id{}; // Initialize to zero
};

struct VendorBootArgs {
  std::filesystem::path output;
  std::filesystem::path dtb;
  std::filesystem::path bootconfig;
  std::filesystem::path vendor_ramdisk;
  std::string vendor_cmdline;
  std::string board;
  std::vector<VendorRamdiskEntry> ramdisks;
  uint32_t base = 0x10000000;
  uint32_t kernel_offset = 0x00008000;
  uint32_t ramdisk_offset = 0x01000000;
  uint64_t dtb_offset = 0x01f00000;
  uint32_t tags_offset = 0x00000100;
  uint32_t page_size = 2048;
  uint32_t header_version = 3;
};

class VendorBootBuilder {
  VendorBootArgs args;
  uint64_t ramdisk_total_size = 0;

public:
  explicit VendorBootBuilder(VendorBootArgs &&args) : args(std::move(args)) {}
  void Build();

private:
  bool WriteHeader(std::ostream &out);
  bool WriteRamdisks(std::ostream &out);
  bool WriteTableEntries(std::ostream &out);
};
