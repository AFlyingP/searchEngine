#include "store/index_file.hpp"
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) {
        return 0;
    }

    // Write arbitrary payload to a temporary test file and attempt to open as IndexView
    // Guarantee: corrupt input must yield clean error/exception, never UB or crash.
    std::string tmp_path = "fuzz_temp_corrupt.idx";
    {
        std::ofstream out(tmp_path, std::ios::binary);
        if (!out) return 0;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    try {
        needlefish::IndexView view;
        (void)view.open(tmp_path);
        // If it opened, attempt basic queries without crashing
        (void)view.stats();
        (void)view.term_dict();
    } catch (const std::exception&) {
        // Clean error handling expected
    } catch (...) {
        // Any other exception handled
    }

    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);

    return 0;
}

#ifndef NEEDLEFISH_LIBFUZZER
int main() {
    std::vector<std::string> corrupt_samples = {
        "RANDOM_GARBAGE_HEADER_DATA",
        "NFLSHIDX\x00\x00\x00\x01\xFF\xFF\xFF\xFF",
        std::string(1024, '\x00'),
        std::string(2048, '\xFF'),
        "NFLSHIDX" + std::string(500, '\xAA')
    };

    for (const auto& sample : corrupt_samples) {
        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(sample.data()), sample.size());
    }

    return 0;
}
#endif
