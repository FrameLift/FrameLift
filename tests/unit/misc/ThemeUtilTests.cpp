#include "ThemeUtil.h"

#include <cmath>
#include <gtest/gtest.h>

namespace
{
constexpr float kEps = 1.f / 255.f + 1e-4f; // hex round-trip tolerance
} // namespace

TEST(ThemeUtilTest, ParseHexWithAndWithoutHash)
{
    float a[3], b[3];
    ASSERT_TRUE(ThemeUtil::ParseHexColor("#FF8000", a));
    ASSERT_TRUE(ThemeUtil::ParseHexColor("ff8000", b)); // no hash, lowercase
    EXPECT_NEAR(a[0], 1.0f, kEps);
    EXPECT_NEAR(a[1], 128.f / 255.f, kEps);
    EXPECT_NEAR(a[2], 0.0f, kEps);
    EXPECT_NEAR(a[0], b[0], 1e-6f);
    EXPECT_NEAR(a[1], b[1], 1e-6f);
    EXPECT_NEAR(a[2], b[2], 1e-6f);
}

TEST(ThemeUtilTest, ParseHexRejectsMalformedAndLeavesOutputUntouched)
{
    float rgb[3] = {0.25f, 0.5f, 0.75f};
    EXPECT_FALSE(ThemeUtil::ParseHexColor("#12345", rgb));   // too short
    EXPECT_FALSE(ThemeUtil::ParseHexColor("#1234567", rgb)); // too long
    EXPECT_FALSE(ThemeUtil::ParseHexColor("#12G456", rgb));  // bad nibble
    EXPECT_FALSE(ThemeUtil::ParseHexColor(nullptr, rgb));
    // Untouched on failure.
    EXPECT_FLOAT_EQ(rgb[0], 0.25f);
    EXPECT_FLOAT_EQ(rgb[1], 0.5f);
    EXPECT_FLOAT_EQ(rgb[2], 0.75f);
}

TEST(ThemeUtilTest, FormatParseRoundTrip)
{
    const float in[3] = {0.2f, 0.6f, 0.9f};
    char hex[8];
    ThemeUtil::FormatHexColor(in, hex);
    float out[3];
    ASSERT_TRUE(ThemeUtil::ParseHexColor(hex, out));
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_NEAR(in[i], out[i], kEps);
    }
}

TEST(ThemeUtilTest, PresetIndexFallsBackToZero)
{
    EXPECT_EQ(ThemeUtil::PresetIndex("dark"), 0);
    EXPECT_EQ(ThemeUtil::PresetIndex("light"), 1);
    EXPECT_EQ(ThemeUtil::PresetIndex("classic"), 0); // removed preset -> fallback
    EXPECT_EQ(ThemeUtil::PresetIndex("bogus"), 0);
    EXPECT_EQ(ThemeUtil::PresetIndex(nullptr), 0);
}

TEST(ThemeUtilTest, HsvRoundTrip)
{
    const float colors[][3] = {{0.8f, 0.2f, 0.4f}, {0.1f, 0.9f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.f, 1.f, 0.f}};
    for (const auto& rgb : colors)
    {
        float hsv[3], back[3];
        ThemeUtil::RgbToHsv(rgb, hsv);
        ThemeUtil::HsvToRgb(hsv, back);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_NEAR(rgb[i], back[i], 1e-4f);
        }
    }
}

TEST(ThemeUtilTest, RetintPreservesValueAndAlphaAdoptsAccentHueSat)
{
    // A dark-ish gray slot retinted toward a saturated orange accent.
    float slot[4] = {0.3f, 0.3f, 0.3f, 0.7f};

    float accent[3];
    ASSERT_TRUE(ThemeUtil::ParseHexColor("#FF8000", accent));
    float accentHsv[3];
    ThemeUtil::RgbToHsv(accent, accentHsv);

    ThemeUtil::RetintColor(slot, accent);

    // Alpha untouched.
    EXPECT_FLOAT_EQ(slot[3], 0.7f);

    float outHsv[3];
    const float outRgb[3] = {slot[0], slot[1], slot[2]};
    ThemeUtil::RgbToHsv(outRgb, outHsv);
    EXPECT_NEAR(outHsv[0], accentHsv[0], 1e-3f); // hue from accent
    EXPECT_NEAR(outHsv[1], accentHsv[1], 1e-3f); // saturation from accent
    EXPECT_NEAR(outHsv[2], 0.3f, 1e-3f);         // value (brightness) preserved
}

TEST(ThemeUtilTest, RetintWithGrayAccentDesaturates)
{
    float slot[4] = {0.9f, 0.2f, 0.2f, 1.f};
    const float gray[3] = {0.5f, 0.5f, 0.5f};
    ThemeUtil::RetintColor(slot, gray);
    // Gray accent has saturation 0 -> result is gray at the slot's own value.
    EXPECT_NEAR(slot[0], slot[1], 1e-4f);
    EXPECT_NEAR(slot[1], slot[2], 1e-4f);
}
