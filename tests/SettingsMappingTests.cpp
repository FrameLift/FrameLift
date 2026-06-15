#include "SettingsMapping.h"

#include <gtest/gtest.h>

TEST(SettingsMappingTest, AudioParamsFromDefaults)
{
    const Settings s;
    const AudioNormalizeParams p = ParamsFromSettings(s);
    EXPECT_EQ(p.frameLen, s.dynaudnormFrameLen);
    EXPECT_EQ(p.gaussSize, s.dynaudnormGaussSize);
    EXPECT_FLOAT_EQ(p.peak, s.dynaudnormPeak);
    EXPECT_FLOAT_EQ(p.maxGain, s.dynaudnormMaxGain);
    EXPECT_FLOAT_EQ(p.volume, s.dynaudnormVolume);
}

TEST(SettingsMappingTest, AudioParamsTrackEditedSettings)
{
    Settings s;
    s.dynaudnormFrameLen = 321;
    s.dynaudnormVolume = 2.25f;
    const AudioNormalizeParams p = ParamsFromSettings(s);
    EXPECT_EQ(p.frameLen, 321);
    EXPECT_FLOAT_EQ(p.volume, 2.25f);
}

TEST(SettingsMappingTest, PlaybackOptionsMapBooleans)
{
    Settings s;
    s.hwdec = false;
    s.hrSeek = true;
    s.subAutoLoad = true;
    s.audioFileAutoLoad = false;

    const PlaybackOptions o = PlaybackOptsFromSettings(s);
    EXPECT_FALSE(o.hwdec);
    EXPECT_TRUE(o.hrSeek);
    EXPECT_TRUE(o.subAutoLoad);
    EXPECT_FALSE(o.audioFileAutoLoad);
}

TEST(SettingsMappingTest, ReadAheadDefaultsConvertMbToBytes)
{
    const Settings s;
    const ReadAheadCacheOptions o = ReadAheadOptsFromSettings(s);
    EXPECT_TRUE(o.enabled);
    EXPECT_EQ(o.maxBytes, static_cast<int64_t>(s.readAheadSizeMB) * 1024 * 1024);
}

TEST(SettingsMappingTest, ReadAheadTracksEditedSettings)
{
    Settings s;
    s.readAheadEnabled = false;
    s.readAheadSizeMB = 128;
    const ReadAheadCacheOptions o = ReadAheadOptsFromSettings(s);
    EXPECT_FALSE(o.enabled);
    EXPECT_EQ(o.maxBytes, int64_t{128} * 1024 * 1024);
}

TEST(SettingsMappingTest, ReadAheadClampsNegativeSizeToZero)
{
    Settings s;
    s.readAheadSizeMB = -10;
    const ReadAheadCacheOptions o = ReadAheadOptsFromSettings(s);
    EXPECT_EQ(o.maxBytes, 0);
}
