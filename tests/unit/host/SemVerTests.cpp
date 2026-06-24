#include "SemVer.h"

#include <gtest/gtest.h>

TEST(SemVerTest, ParsesFullVersion)
{
    const SemVer v = ParseVersion("1.2.3");
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
}

TEST(SemVerTest, ParsesMultiDigitComponents)
{
    const SemVer v = ParseVersion("2.10.5");
    EXPECT_EQ(v.major, 2);
    EXPECT_EQ(v.minor, 10);
    EXPECT_EQ(v.patch, 5);
}

TEST(SemVerTest, MissingComponentsDefaultToZero)
{
    EXPECT_EQ(ParseVersion("1").minor, 0);
    EXPECT_EQ(ParseVersion("1").patch, 0);
    EXPECT_EQ(ParseVersion("1.2").patch, 0);
}

TEST(SemVerTest, MalformedParsesToZero)
{
    const SemVer v = ParseVersion("not-a-version");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST(SemVerTest, OrdersByMajorThenMinorThenPatch)
{
    EXPECT_TRUE(ParseVersion("1.0.1") > ParseVersion("1.0.0"));
    EXPECT_TRUE(ParseVersion("1.1.0") > ParseVersion("1.0.9"));
    EXPECT_TRUE(ParseVersion("2.0.0") > ParseVersion("1.9.9"));
}

TEST(SemVerTest, EqualIsNotGreater)
{
    EXPECT_FALSE(ParseVersion("1.2.3") > ParseVersion("1.2.3"));
    EXPECT_FALSE(ParseVersion("1.0.0") > ParseVersion("1.0.1"));
}
