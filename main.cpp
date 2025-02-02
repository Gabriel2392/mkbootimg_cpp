#include <unordered_set>
#include <unordered_map>
#include "bootimg.h"
#include "vendorbootimg.h"
#include <algorithm>
#include <iostream>
#include <string_view>
#include <string>
#include <optional>
#include <cstdlib>

std::unordered_set<std::string> VENDOR_RAMDISK_BLACKLISTED_NAMES = {"default"};

uint32_t getRamdiskType(const std::string& type) {
    static const std::unordered_map<std::string_view, uint32_t> ramdiskMap = {
        {"none", VENDOR_RAMDISK_TYPE_NONE},
        {"platform", VENDOR_RAMDISK_TYPE_PLATFORM},
        {"recovery", VENDOR_RAMDISK_TYPE_RECOVERY},
        {"dlkm", VENDOR_RAMDISK_TYPE_DLKM},
    };
    auto it = ramdiskMap.find(type);
    return (it != ramdiskMap.end()) ? it->second : VENDOR_RAMDISK_TYPE_PLATFORM;
}

struct RamdiskEntryFlags {
    bool has_type = false;
    bool has_name = false;
    bool has_fragment = false;
};

std::optional<std::pair<BootImageArgs, VendorBootArgs>> ParseArguments(int argc, char *argv[]) {
    BootImageArgs args;
    VendorBootArgs vendor_args;
    bool parsing_vendor = false;

    VendorRamdiskEntry currentEntry;
    RamdiskEntryFlags currentFlags;

    auto finishCurrentEntry = [&]() -> bool {
        if (!currentFlags.has_type || !currentFlags.has_name || !currentFlags.has_fragment) {
            std::cerr << "Incomplete vendor ramdisk entry: missing "
                      << (!currentFlags.has_type ? "--ramdisk_type " : "")
                      << (!currentFlags.has_name ? "--ramdisk_name " : "")
                      << (!currentFlags.has_fragment ? "--vendor_ramdisk_fragment " : "")
                      << "\n";
            return false;
        }
        vendor_args.ramdisks.push_back(currentEntry);
        currentEntry = VendorRamdiskEntry();
        currentFlags = RamdiskEntryFlags();
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "--vendor_boot") {
            parsing_vendor = true;
            if (++i >= argc) {
                std::cerr << "--vendor_boot requires an argument\n";
                return std::nullopt;
            }
            vendor_args.output = argv[i];
        } else if (arg == "--ramdisk_type") {
            parsing_vendor = true;
            if (++i >= argc) {
                std::cerr << "--ramdisk_type requires an argument\n";
                return std::nullopt;
            }
            if (currentFlags.has_type || currentFlags.has_name || currentFlags.has_fragment) {
                if (!finishCurrentEntry()) {
                    return std::nullopt;
                }
            }
            currentEntry.type = getRamdiskType(argv[i]);
            currentFlags.has_type = true;
        } else if (arg == "--ramdisk_name") {
            parsing_vendor = true;
            if (++i >= argc) {
                std::cerr << "--ramdisk_name requires an argument\n";
                return std::nullopt;
            }
            if (!currentFlags.has_type) {
                std::cerr << "--ramdisk_name provided before --ramdisk_type\n";
                return std::nullopt;
            }
            if (currentFlags.has_name) {
                std::cerr << "Duplicate --ramdisk_name in current vendor entry\n";
                return std::nullopt;
            }
            currentEntry.name = argv[i];
            currentFlags.has_name = true;
        } else if (arg == "--vendor_ramdisk_fragment") {
            parsing_vendor = true;
            if (++i >= argc) {
                std::cerr << "--vendor_ramdisk_fragment requires an argument\n";
                return std::nullopt;
            }
            if (!currentFlags.has_type) {
                std::cerr << "--vendor_ramdisk_fragment provided before --ramdisk_type\n";
                return std::nullopt;
            }
            if (currentFlags.has_fragment) {
                std::cerr << "Duplicate --vendor_ramdisk_fragment in current vendor entry\n";
                return std::nullopt;
            }
            currentEntry.path = argv[i];
            currentFlags.has_fragment = true;
        } else if (arg == "--vendor_bootconfig") {
            parsing_vendor = true;
            if (++i >= argc) {
                std::cerr << "--vendor_bootconfig requires an argument\n";
                return std::nullopt;
            }
            vendor_args.bootconfig = argv[i];
        } else if (arg == "--vendor_ramdisk") {
            parsing_vendor = true;
            if (++i >= argc) {
                std::cerr << "--vendor_ramdisk requires an argument\n";
                return std::nullopt;
            }
            vendor_args.vendor_ramdisk = argv[i];
        } else if (arg == "--kernel") {
            if (++i >= argc) {
                std::cerr << "--kernel requires an argument\n";
                return std::nullopt;
            }
            args.kernel = argv[i];
        } else if (arg == "--recovery_dtbo") {
            if (++i >= argc) {
                std::cerr << "--recovery_dtbo requires an argument\n";
                return std::nullopt;
            }
            args.recovery_dtbo = argv[i];
        } else if (arg == "--ramdisk") {
            if (++i >= argc) {
                std::cerr << "--ramdisk requires an argument\n";
                return std::nullopt;
            }
            args.ramdisk = argv[i];
        } else if (arg == "--second") {
            if (++i >= argc) {
                std::cerr << "--second requires an argument\n";
                return std::nullopt;
            }
            args.second = argv[i];
        } else if (arg == "--dtb") {
            if (++i >= argc) {
                std::cerr << "--dtb requires an argument\n";
                return std::nullopt;
            }
            args.dtb = argv[i];
            vendor_args.dtb = args.dtb;
        } else if (arg == "--cmdline") {
            if (++i >= argc) {
                std::cerr << "--cmdline requires an argument\n";
                return std::nullopt;
            }
            args.cmdline = argv[i];
        } else if (arg == "--base") {
            if (++i >= argc) {
                std::cerr << "--base requires an argument\n";
                return std::nullopt;
            }
            args.base = std::strtoul(argv[i], nullptr, 0);
            vendor_args.base = args.base;
        } else if (arg == "--kernel_offset") {
            if (++i >= argc) {
                std::cerr << "--kernel_offset requires an argument\n";
                return std::nullopt;
            }
            args.kernel_offset = std::strtoul(argv[i], nullptr, 0);
            vendor_args.kernel_offset = args.kernel_offset;
        } else if (arg == "--ramdisk_offset") {
            if (++i >= argc) {
                std::cerr << "--ramdisk_offset requires an argument\n";
                return std::nullopt;
            }
            args.ramdisk_offset = std::strtoul(argv[i], nullptr, 0);
            vendor_args.ramdisk_offset = args.ramdisk_offset;
        } else if (arg == "--second_offset") {
            if (++i >= argc) {
                std::cerr << "--second_offset requires an argument\n";
                return std::nullopt;
            }
            args.second_offset = std::strtoul(argv[i], nullptr, 0);
        } else if (arg == "--dtb_offset") {
            if (++i >= argc) {
                std::cerr << "--dtb_offset requires an argument\n";
                return std::nullopt;
            }
            args.dtb_offset = std::strtoul(argv[i], nullptr, 0);
            vendor_args.dtb_offset = args.dtb_offset;
        } else if (arg == "--os_version") {
            if (++i >= argc) {
                std::cerr << "--os_version requires an argument\n";
                return std::nullopt;
            }
            if (auto ver = utils::OSVersion::Parse(argv[i])) {
                args.os_version = *ver;
            } else {
                std::cerr << "Invalid os_version format\n";
                return std::nullopt;
            }
        } else if (arg == "--tags_offset") {
            if (++i >= argc) {
                std::cerr << "--tags_offset requires an argument\n";
                return std::nullopt;
            }
            args.tags_offset = std::strtoul(argv[i], nullptr, 0);
        } else if (arg == "--board") {
            if (++i >= argc) {
                std::cerr << "--board requires an argument\n";
                return std::nullopt;
            }
            args.board = argv[i];
        } else if (arg == "--pagesize") {
            if (++i >= argc) {
                std::cerr << "--pagesize requires an argument\n";
                return std::nullopt;
            }
            args.page_size = std::strtoul(argv[i], nullptr, 0);
            vendor_args.page_size = args.page_size;
        } else if (arg == "--header_version") {
            if (++i >= argc) {
                std::cerr << "--header_version requires an argument\n";
                return std::nullopt;
            }
            args.header_version = std::strtoul(argv[i], nullptr, 0);
            vendor_args.header_version = args.header_version;
        } else if (arg == "-o") {
            if (++i >= argc) {
                std::cerr << "-o requires an argument\n";
                return std::nullopt;
            }
            args.output = argv[i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return std::nullopt;
        }
    }

    if (currentFlags.has_type || currentFlags.has_name || currentFlags.has_fragment) {
        if (!finishCurrentEntry()) {
            return std::nullopt;
        }
    }
    
    if (parsing_vendor && vendor_args.ramdisks.empty() && vendor_args.vendor_ramdisk.empty()) {
        std::cerr << "At least one vendor ramdisk is needed\n";
        return std::nullopt;
    }
    
    std::unordered_set<std::string> names;
    for (const auto &entry : vendor_args.ramdisks) {
        if (VENDOR_RAMDISK_BLACKLISTED_NAMES.count(entry.name)) {
            std::cerr << "Blocklisted ramdisk name: " << entry.name << "\n";
            return std::nullopt;
        }
        if (!names.insert(entry.name).second) {
            std::cerr << "Duplicate ramdisk name: " << entry.name << "\n";
            return std::nullopt;
        }
    }
    
    return std::make_pair(args, vendor_args);
}

int main(int argc, char *argv[]) {
    auto parsed = ParseArguments(argc, argv);
    if (!parsed) {
        std::cerr << "Invalid arguments\n";
        return 1;
    }
    
    auto& [args, vendor_args] = *parsed;
    
    if (!vendor_args.ramdisks.empty() || !vendor_args.vendor_ramdisk.empty()) {
        VendorBootBuilder builder(std::move(vendor_args));
        if (!builder.Build()) {
            std::cerr << "Failed to create vendor boot image\n";
            return 1;
        }
        return 0;
    }

    if (!WriteBootImage(args)) {
        std::cerr << "Failed to create boot image\n";
        return 1;
    }

    return 0;
}
