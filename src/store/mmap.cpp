#include "store/mmap.hpp"

#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace needlefish {

MemoryMappedFile::MemoryMappedFile(const std::filesystem::path& path) {
    open(path);
}

MemoryMappedFile::~MemoryMappedFile() {
    close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : data_(other.data_), size_(other.size_) {
#if defined(_WIN32)
    file_handle_ = other.file_handle_;
    mapping_handle_ = other.mapping_handle_;
    other.file_handle_ = nullptr;
    other.mapping_handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    other.data_ = nullptr;
    other.size_ = 0;
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
#if defined(_WIN32)
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_ = nullptr;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void MemoryMappedFile::open(const std::filesystem::path& path) {
    close();

#if defined(_WIN32)
    file_handle_ = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_handle_ == INVALID_HANDLE_VALUE) {
        file_handle_ = nullptr;
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Failed to open file: " + path.string());
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle_, &file_size)) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Failed to get file size: " + path.string());
    }
    size_ = static_cast<size_t>(file_size.QuadPart);

    if (size_ == 0) {
        data_ = nullptr;
        return;
    }

    mapping_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_handle_) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Failed to create file mapping: " + path.string());
    }

    data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0));
    if (!data_) {
        CloseHandle(mapping_handle_);
        CloseHandle(file_handle_);
        mapping_handle_ = nullptr;
        file_handle_ = nullptr;
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Failed to map view of file: " + path.string());
    }
#else
    fd_ = ::open(path.string().c_str(), O_RDONLY);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "Failed to open file: " + path.string());
    }

    struct stat sb;
    if (::fstat(fd_, &sb) != 0) {
        ::close(fd_);
        fd_ = -1;
        throw std::system_error(errno, std::generic_category(),
                                "Failed to fstat file: " + path.string());
    }
    size_ = static_cast<size_t>(sb.st_size);

    if (size_ == 0) {
        data_ = nullptr;
        return;
    }

    void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd_, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd_);
        fd_ = -1;
        throw std::system_error(errno, std::generic_category(),
                                "Failed to mmap file: " + path.string());
    }
    data_ = static_cast<const uint8_t*>(mapped);
#endif
}

void MemoryMappedFile::close() noexcept {
#if defined(_WIN32)
    if (data_) {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }
    if (mapping_handle_) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
    }
#else
    if (data_) {
        ::munmap(const_cast<uint8_t*>(data_), size_);
        data_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    size_ = 0;
}

}  // namespace needlefish
