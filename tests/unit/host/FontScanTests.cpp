#include "FontScan.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace
{
// RAII temp directory unique per instance, recursively removed on destruction.
struct TempDir
{
    fs::path path;

    TempDir()
    {
        static std::atomic<unsigned long long> counter{0};
        const auto n = counter.fetch_add(1);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("framelift_fontscan_" + std::to_string(n) + "_" + std::to_string(stamp));
        fs::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    void Touch(const fs::path& rel) const
    {
        const fs::path full = path / rel;
        fs::create_directories(full.parent_path());
        std::ofstream(full) << "x";
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};
} // namespace

TEST(FontScanTest, DisplayNameDerivation)
{
    EXPECT_EQ(FontDisplayName("DejaVuSans-Bold.ttf"), "DejaVuSans Bold");
    EXPECT_EQ(FontDisplayName("Some_Font.otf"), "Some Font");
    EXPECT_EQ(FontDisplayName("/abs/path/Arial.ttf"), "Arial");
}

TEST(FontScanTest, FindsFontsRecursivelyIgnoresOthers)
{
    TempDir d;
    d.Touch("Alpha.ttf");
    d.Touch("nested/Beta.otf");
    d.Touch("nested/deep/Gamma.TTF"); // uppercase extension
    d.Touch("readme.txt");            // ignored
    d.Touch("nested/notes.md");       // ignored

    const auto fonts = ScanFontDirs({d.path});
    ASSERT_EQ(fonts.size(), 3u);
    // Sorted case-insensitively by name: Alpha, Beta, Gamma.
    EXPECT_EQ(fonts[0].name, "Alpha");
    EXPECT_EQ(fonts[1].name, "Beta");
    EXPECT_EQ(fonts[2].name, "Gamma");
    for (const auto& f : fonts)
    {
        EXPECT_FALSE(f.path.empty());
    }
}

TEST(FontScanTest, DedupByNameKeepsFirst)
{
    TempDir d;
    d.Touch("dirA/Roboto.ttf");
    d.Touch("dirB/Roboto.ttf"); // same display name in another subdir

    const auto fonts = ScanFontDirs({d.path});
    ASSERT_EQ(fonts.size(), 1u);
    EXPECT_EQ(fonts[0].name, "Roboto");
}

TEST(FontScanTest, MissingDirYieldsEmptyWithoutThrowing)
{
    const fs::path nope = fs::temp_directory_path() / "framelift_fontscan_does_not_exist_zzz";
    std::vector<FontEntry> fonts;
    EXPECT_NO_THROW(fonts = ScanFontDirs({nope}));
    EXPECT_TRUE(fonts.empty());
}

TEST(FontScanTest, SystemFontDirsAreReturned)
{
    // Platform-dependent contents, but it must always return at least one path
    // and never throw.
    std::vector<fs::path> dirs;
    EXPECT_NO_THROW(dirs = SystemFontDirs());
    EXPECT_FALSE(dirs.empty());
}
