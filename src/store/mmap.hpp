#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace needlefish {

/**
 * @brief Cross-platform Read-Only Memory-Mapped File RAII Wrapper.
 * Zero-copy memory mapping on Windows (Win32) and Linux/macOS (POSIX).
 */
class MemoryMappedFile {
  public:
    MemoryMappedFile() = default;
    explicit MemoryMappedFile(const std::filesystem::path& path);
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    void open(const std::filesystem::path& path);
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return data_ != nullptr; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept {
        return std::span<const uint8_t>(data_, size_);
    }

  private:
    const uint8_t* data_{nullptr};
    size_t size_{0};

#if defined(_WIN32)
    void* file_handle_{nullptr};
    void* mapping_handle_{nullptr};
#else
    int fd_{-1};
#endif
};

}  // namespace needlefish
