#include "bootimg.h"
#include "vendorbootimg.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <stdexcept>
#include <system_error> // For potential future exception types

namespace { // Use anonymous namespace for internal linkage

    const std::unordered_set<std::string> VENDOR_RAMDISK_BLACKLISTED_NAMES = { "default" };

    uint32_t getRamdiskType(const std::string& type) {
        static const std::unordered_map<std::string_view, uint32_t> ramdiskMap = {
            {"none", 0},
            {"platform", 1},
            {"recovery", 2},
            {"dlkm", 3},
        };
        auto it = ramdiskMap.find(type);
        return (it != ramdiskMap.end()) ? it->second : 0;
    }

    struct RamdiskEntryFlags {
        bool has_type = false;
        bool has_name = false;
        bool has_fragment = false;
    };

    [[noreturn]] void print_help() {
        // Using raw string literal for cleaner multi-line output
        std::cout << R"(usage: mkbootimg [-h|--help] [--kernel KERNEL] [--ramdisk RAMDISK] [--second SECOND] [--dtb DTB] [--recovery_dtbo RECOVERY_DTBO] [--cmdline CMDLINE] [--vendor_cmdline VENDOR_CMDLINE] [--base BASE]
                    [--kernel_offset KERNEL_OFFSET] [--ramdisk_offset RAMDISK_OFFSET] [--second_offset SECOND_OFFSET] [--dtb_offset DTB_OFFSET] [--os_version OS_VERSION] [--os_patch_level OS_PATCH_LEVEL] [--tags_offset TAGS_OFFSET]
                    [--board BOARD] [--pagesize {2048,4096,8192,16384}] [--id] [--header_version HEADER_VERSION] [-o/--output OUTPUT] [--vendor_boot VENDOR_BOOT] [--vendor_ramdisk VENDOR_RAMDISK] [--vendor_bootconfig VENDOR_BOOTCONFIG]

options:
  -h, --help            show this help message and exit
  --kernel KERNEL       path to the kernel (e.g., --kernel=path or --kernel path)
  --ramdisk RAMDISK     path to the ramdisk
  --second SECOND       path to the second bootloader
  --dtb DTB             path to the dtb
  --recovery_dtbo RECOVERY_DTBO
                        path to the recovery DTBO
  --cmdline CMDLINE     kernel command line arguments (e.g., --cmdline="console=ttyS0 quiet")
  --vendor_cmdline VENDOR_CMDLINE
                        vendor boot kernel command line arguments
  --base BASE           base address (hex or dec, e.g., --base=0x10000000)
  --kernel_offset KERNEL_OFFSET
                        kernel offset
  --ramdisk_offset RAMDISK_OFFSET
                        ramdisk offset
  --second_offset SECOND_OFFSET
                        second bootloader offset
  --dtb_offset DTB_OFFSET
                        dtb offset
  --os_version OS_VERSION
                        operating system version (e.g., --os_version=12.0.0)
  --os_patch_level OS_PATCH_LEVEL
                        operating system patch level (e.g., --os_patch_level=2023-10)
  --tags_offset TAGS_OFFSET
                        tags offset
  --board BOARD         board name
  --pagesize {2048,4096,8192,16384}
                        page size (default is 2048)
  --header_version HEADER_VERSION
                        boot image header version (default is 3 for vendor_boot and 4 for boot)
  -o, --out, --output, --boot BOOT
                        output file name
  --vendor_boot VENDOR_BOOT
                        vendor boot output file name
  --vendor_ramdisk VENDOR_RAMDISK
                        path to the vendor ramdisk
  --vendor_bootconfig VENDOR_BOOTCONFIG
                        path to the vendor bootconfig file

vendor boot version 4 arguments:
  --ramdisk_type {none,platform,recovery,dlkm}
                        specify the type of the ramdisk
  --ramdisk_name NAME
                        specify the name of the ramdisk
  --vendor_ramdisk_fragment VENDOR_RAMDISK_FILE
                        path to the vendor ramdisk file

  These options can be specified multiple times, where each vendor ramdisk
  option group ends with a --vendor_ramdisk_fragment option.
  Each option group appends an additional ramdisk to the vendor boot image.
)";
        exit(EXIT_FAILURE);
    }

    inline std::string_view parse_quoted_string(std::string_view sv) noexcept {
        if (sv.length() >= 2 && sv.front() == '"' && sv.back() == '"') {
            return sv.substr(1, sv.length() - 2);
        }
        return sv;
    }

    std::optional<std::vector<std::pair<std::string_view, std::string_view>>>
        tokenize_arguments(int argc, char* argv[]) {
        std::vector<std::pair<std::string_view, std::string_view>> args;
        if (argc > 1) {
            args.reserve(static_cast<size_t>(argc) - 1);
        }

        for (int i = 1; i < argc; ++i) {
            std::string_view current_arg(argv[i]);
            std::string_view key;
            std::string_view value;

            if (current_arg == "-h" || current_arg == "--help") {
                args.emplace_back(current_arg, "");
                continue;
            }

            if (current_arg.rfind("--", 0) == 0) {
                size_t equals_pos = current_arg.find('=');
                if (equals_pos != std::string_view::npos) {
                    key = current_arg.substr(0, equals_pos);
                    value = parse_quoted_string(current_arg.substr(equals_pos + 1));
                }
                else {
                    key = current_arg;
                    if (i + 1 < argc && argv[i + 1][0] != '-') {
                        value = parse_quoted_string(argv[i + 1]);
                        ++i;
                    }
                    else {
                        value = "";
                    }
                }
            }
            else if (current_arg.rfind('-', 0) == 0 && current_arg.length() == 2) {
                key = current_arg;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    value = parse_quoted_string(argv[i + 1]);
                    ++i;
                }
                else {
                    std::cerr << key << " requires a value." << std::endl;
                    return std::nullopt;
                }
            }
            else {
                std::cerr << "Unexpected argument format: " << current_arg << std::endl;
                return std::nullopt;
            }

            if (key == "-o" || key == "--out" || key == "--boot") {
                key = "--output";
            }

            args.emplace_back(key, value);
        }
        return args;
    }


    std::optional<std::pair<BootImageArgs, VendorBootArgs>>
        ProcessArguments(const std::vector<std::pair<std::string_view, std::string_view>>& tokenized_args) {
        BootImageArgs args;
        VendorBootArgs vendor_args;
        bool parsing_vendor = false;

        VendorRamdiskEntry currentEntry;
        RamdiskEntryFlags currentFlags;

        auto finishCurrentEntry = [&]() -> bool {
            if (!currentFlags.has_type && !currentFlags.has_name && !currentFlags.has_fragment) {
                return true;
            }

            if (!currentFlags.has_type || !currentFlags.has_name || !currentFlags.has_fragment) {
                std::cerr << "Incomplete vendor ramdisk entry: missing "
                    << (!currentFlags.has_type ? "--ramdisk_type " : "")
                    << (!currentFlags.has_name ? "--ramdisk_name " : "")
                    << (!currentFlags.has_fragment ? "--vendor_ramdisk_fragment " : "")
                    << std::endl;
                return false;
            }

            unsigned int required_version = 4u;
            args.header_version = std::max(args.header_version, required_version);
            vendor_args.header_version = std::max(vendor_args.header_version, required_version);

            vendor_args.ramdisks.push_back(std::move(currentEntry)); // Move the entry
            currentEntry = {}; // Reset
            currentFlags = {}; // Reset
            return true;
            };

        for (const auto& [key, value] : tokenized_args) {
            try {
                if (key == "--help" || key == "-h") {
                    print_help();
                }
                else if (key == "--vendor_boot") {
                    parsing_vendor = true;
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    vendor_args.output = value;
                }
                else if (key == "--ramdisk_type") {
                    parsing_vendor = true;
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    if (currentFlags.has_type || currentFlags.has_name || currentFlags.has_fragment) {
                        if (!finishCurrentEntry()) return std::nullopt;
                    }
                    currentEntry.type = getRamdiskType(std::string(value));
                    currentFlags.has_type = true;
                }
                else if (key == "--ramdisk_name") {
                    parsing_vendor = true;
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    if (!currentFlags.has_type) { std::cerr << key << " provided before --ramdisk_type.\n"; return std::nullopt; }
                    if (currentFlags.has_name) { std::cerr << "Duplicate " << key << " in current vendor entry.\n"; return std::nullopt; }
                    currentEntry.name = value;
                    currentFlags.has_name = true;
                }
                else if (key == "--vendor_ramdisk_fragment") {
                    parsing_vendor = true;
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    if (!currentFlags.has_type) { std::cerr << key << " provided before --ramdisk_type.\n"; return std::nullopt; }
                    if (currentFlags.has_fragment) { std::cerr << "Duplicate " << key << " in current vendor entry.\n"; return std::nullopt; }
                    currentEntry.path = value;
                    currentFlags.has_fragment = true;
                    if (!finishCurrentEntry()) return std::nullopt;
                }
                else if (key == "--vendor_bootconfig") {
                    parsing_vendor = true;
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    vendor_args.bootconfig = value;
                }
                else if (key == "--vendor_cmdline") {
                    parsing_vendor = true;
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    vendor_args.vendor_cmdline = value;
                }
                else if (key == "--vendor_ramdisk") {
                    parsing_vendor = true;
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    vendor_args.vendor_ramdisk = value;
                }
                else if (key == "--kernel") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.kernel = value;
                }
                else if (key == "--recovery_dtbo") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.recovery_dtbo = value;
                }
                else if (key == "--ramdisk") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.ramdisk = value;
                }
                else if (key == "--second") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.second = value;
                }
                else if (key == "--dtb") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.dtb = value;
                    vendor_args.dtb = args.dtb;
                }
                else if (key == "--cmdline") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.cmdline = value;
                }
                else if (key == "--base") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.base = std::stoul(std::string(value), nullptr, 0);
                    vendor_args.base = args.base;
                }
                else if (key == "--kernel_offset") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.kernel_offset = std::stoul(std::string(value), nullptr, 0);
                    vendor_args.kernel_offset = args.kernel_offset;
                }
                else if (key == "--ramdisk_offset") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.ramdisk_offset = std::stoul(std::string(value), nullptr, 0);
                    vendor_args.ramdisk_offset = args.ramdisk_offset;
                }
                else if (key == "--second_offset") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.second_offset = std::stoul(std::string(value), nullptr, 0);
                }
                else if (key == "--dtb_offset") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.dtb_offset = std::stoul(std::string(value), nullptr, 0);
                    vendor_args.dtb_offset = std::stoull(std::string(value), nullptr, 0);
                }
                else if (key == "--os_version") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.os_version.version_str = value;
                }
                else if (key == "--os_patch_level") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.os_version.patch_level_str = value;
                }
                else if (key == "--tags_offset") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.tags_offset = std::stoul(std::string(value), nullptr, 0);
                    vendor_args.tags_offset = args.tags_offset;
                }
                else if (key == "--board") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.board = value;
                    vendor_args.board = args.board;
                }
                else if (key == "--pagesize") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.page_size = std::stoul(std::string(value), nullptr, 0);
                    bool isPageSizeValid = args.page_size == 2048 || args.page_size == 4096 ||
                        args.page_size == 8192 || args.page_size == 16384;
                    if (!isPageSizeValid) {
                        std::cerr << "Invalid page size: " << args.page_size
                            << ". Must be one of {2048, 4096, 8192, 16384}.\n";
                        return std::nullopt;
                    }
                    vendor_args.page_size = args.page_size;
                }
                else if (key == "--header_version") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.header_version = std::stoul(std::string(value), nullptr, 0);
                    vendor_args.header_version = args.header_version;
                }
                else if (key == "--output") {
                    if (value.empty()) { std::cerr << key << " requires a value.\n"; return std::nullopt; }
                    args.output = value;
                }
                else {
                    std::cerr << "Unknown argument: " << key << std::endl;
                    return std::nullopt;
                }
            }
            catch (const std::invalid_argument& e) {
                std::cerr << "Invalid numeric value for " << key << ": '" << value << "'" << std::endl;
                return std::nullopt;
            }
            catch (const std::out_of_range& e) {
                std::cerr << "Numeric value out of range for " << key << ": '" << value << "'" << std::endl;
                return std::nullopt;
            }
        }

        if (currentFlags.has_type || currentFlags.has_name || currentFlags.has_fragment) {
            std::cerr << "Incomplete vendor ramdisk entry at the end of arguments." << std::endl;
            return std::nullopt;
        }

        if (vendor_args.output.empty() && args.output.empty()) {
            std::cerr << "Either --output (or --boot/-o) or --vendor_boot is required." << std::endl;
            return std::nullopt;
        }

        if (parsing_vendor && vendor_args.ramdisks.empty() && vendor_args.vendor_ramdisk.empty()) {
            std::cerr << "--vendor_boot specified, but no vendor ramdisks provided "
                << "(--vendor_ramdisk or --vendor_ramdisk_fragment groups)." << std::endl;
            return std::nullopt;
        }

        std::unordered_set<std::string> names;
        names.reserve(vendor_args.ramdisks.size());
        for (const auto& entry : vendor_args.ramdisks) {
            if (VENDOR_RAMDISK_BLACKLISTED_NAMES.count(entry.name)) {
                std::cerr << "Blocklisted ramdisk name used: " << entry.name << std::endl;
                return std::nullopt;
            }
            if (!names.insert(entry.name).second) {
                std::cerr << "Duplicate ramdisk name found: " << entry.name << std::endl;
                return std::nullopt;
            }
        }

        if (vendor_args.header_version < 3 && !vendor_args.output.empty()) {
            std::cerr << "Vendor Boot requires header version equal or higher than 3." << std::endl;
            return std::nullopt;
        }

        return std::make_pair(std::move(args), std::move(vendor_args));
    }

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
    }

    auto tokenized_args_opt = tokenize_arguments(argc, argv);
    if (!tokenized_args_opt) {
        return EXIT_FAILURE;
    }

    auto parsed_opt = ProcessArguments(*tokenized_args_opt);
    if (!parsed_opt) {
        std::cerr << "Failed to process arguments." << std::endl;
        return EXIT_FAILURE;
    }

    auto& [args, vendor_args] = *parsed_opt;

    try {
        if (!vendor_args.output.empty()) {
            VendorBootBuilder builder(std::move(vendor_args));
            builder.Build();
        } else if (!args.output.empty()) {
            WriteBootImage(args);
        } else {
            std::cerr << "Internal Error: No output file specified or processed." << std::endl;
            return EXIT_FAILURE;
        }

    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "An unknown error occurred." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
