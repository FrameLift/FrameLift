#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// Small helpers for tests that touch the filesystem. Each TempFile owns a unique
// path under the system temp dir and removes it on destruction.

inline std::filesystem::path UniqueTempPath()
{
    static std::atomic<unsigned long long> counter{0};
    const auto n = counter.fetch_add(1);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("framelift_test_" + std::to_string(n) + "_" + std::to_string(stamp) + ".ini");
}

struct TempFile
{
    std::filesystem::path path = UniqueTempPath();

    TempFile() = default;

    explicit TempFile(const std::string& contents)
    {
        Write(contents);
    }

    void Write(const std::string& contents) const
    {
        std::ofstream(path, std::ios::trunc) << contents;
    }

    [[nodiscard]] std::string str() const
    {
        return path.string();
    }

    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};
