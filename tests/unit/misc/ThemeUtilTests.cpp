#include "ThemeUtil.h"

#include "QtTestRunner.h"
#include <cmath>

#include <QtTest/QtTest>

namespace
{
constexpr float kEps = 1.f / 255.f + 1e-4f; // hex round-trip tolerance
} // namespace

class ThemeUtilTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ParseHexWithAndWithoutHash()
    {
        float a[3], b[3];
        QVERIFY(ThemeUtil::ParseHexColor("#FF8000", a));
        QVERIFY(ThemeUtil::ParseHexColor("ff8000", b)); // no hash, lowercase
        QVERIFY(::framelift::test::NearlyEqual(a[0], 1.0f, kEps));
        QVERIFY(::framelift::test::NearlyEqual(a[1], 128.f / 255.f, kEps));
        QVERIFY(::framelift::test::NearlyEqual(a[2], 0.0f, kEps));
        QVERIFY(::framelift::test::NearlyEqual(a[0], b[0], 1e-6f));
        QVERIFY(::framelift::test::NearlyEqual(a[1], b[1], 1e-6f));
        QVERIFY(::framelift::test::NearlyEqual(a[2], b[2], 1e-6f));
    }

    void ParseHexRejectsMalformedAndLeavesOutputUntouched()
    {
        float rgb[3] = {0.25f, 0.5f, 0.75f};
        QVERIFY(!(ThemeUtil::ParseHexColor("#12345", rgb)));   // too short
        QVERIFY(!(ThemeUtil::ParseHexColor("#1234567", rgb))); // too long
        QVERIFY(!(ThemeUtil::ParseHexColor("#12G456", rgb)));  // bad nibble
        QVERIFY(!(ThemeUtil::ParseHexColor(nullptr, rgb)));
        // Untouched on failure.
        QCOMPARE(rgb[0], 0.25f);
        QCOMPARE(rgb[1], 0.5f);
        QCOMPARE(rgb[2], 0.75f);
    }

    void FormatParseRoundTrip()
    {
        const float in[3] = {0.2f, 0.6f, 0.9f};
        char hex[8];
        ThemeUtil::FormatHexColor(in, hex);
        float out[3];
        QVERIFY(ThemeUtil::ParseHexColor(hex, out));
        for (int i = 0; i < 3; ++i)
        {
            QVERIFY(::framelift::test::NearlyEqual(in[i], out[i], kEps));
        }
    }

    void PresetIndexFallsBackToZero()
    {
        QVERIFY((ThemeUtil::PresetIndex("dark")) == (0));
        QVERIFY((ThemeUtil::PresetIndex("light")) == (1));
        QVERIFY((ThemeUtil::PresetIndex("classic")) == (0)); // removed preset -> fallback
        QVERIFY((ThemeUtil::PresetIndex("bogus")) == (0));
        QVERIFY((ThemeUtil::PresetIndex(nullptr)) == (0));
    }

    void HsvRoundTrip()
    {
        const float colors[][3] = {{0.8f, 0.2f, 0.4f}, {0.1f, 0.9f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.f, 1.f, 0.f}};
        for (const auto& rgb : colors)
        {
            float hsv[3], back[3];
            ThemeUtil::RgbToHsv(rgb, hsv);
            ThemeUtil::HsvToRgb(hsv, back);
            for (int i = 0; i < 3; ++i)
            {
                QVERIFY(::framelift::test::NearlyEqual(rgb[i], back[i], 1e-4f));
            }
        }
    }

    void RetintPreservesValueAndAlphaAdoptsAccentHueSat()
    {
        // A dark-ish gray slot retinted toward a saturated orange accent.
        float slot[4] = {0.3f, 0.3f, 0.3f, 0.7f};

        float accent[3];
        QVERIFY(ThemeUtil::ParseHexColor("#FF8000", accent));
        float accentHsv[3];
        ThemeUtil::RgbToHsv(accent, accentHsv);

        ThemeUtil::RetintColor(slot, accent);

        // Alpha untouched.
        QCOMPARE(slot[3], 0.7f);

        float outHsv[3];
        const float outRgb[3] = {slot[0], slot[1], slot[2]};
        ThemeUtil::RgbToHsv(outRgb, outHsv);
        QVERIFY(::framelift::test::NearlyEqual(outHsv[0], accentHsv[0], 1e-3f)); // hue from accent
        QVERIFY(::framelift::test::NearlyEqual(outHsv[1], accentHsv[1], 1e-3f)); // saturation from accent
        QVERIFY(::framelift::test::NearlyEqual(outHsv[2], 0.3f, 1e-3f));         // value (brightness) preserved
    }

    void RetintWithGrayAccentDesaturates()
    {
        float slot[4] = {0.9f, 0.2f, 0.2f, 1.f};
        const float gray[3] = {0.5f, 0.5f, 0.5f};
        ThemeUtil::RetintColor(slot, gray);
        // Gray accent has saturation 0 -> result is gray at the slot's own value.
        QVERIFY(::framelift::test::NearlyEqual(slot[0], slot[1], 1e-4f));
        QVERIFY(::framelift::test::NearlyEqual(slot[1], slot[2], 1e-4f));
    }
};

namespace
{
const ::framelift::test::Registrar<ThemeUtilTest> kRegisterThemeUtilTest{"ThemeUtilTest"};
}

#include "ThemeUtilTests.moc"
