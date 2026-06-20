#include "SettingsMapping.h"
#include "FFmpegAudioOptions.h"

#include <gtest/gtest.h>

TEST(SettingsMappingTest, AudioParamsFromDefaults)
{
    const AudioSettings s;
    const AudioNormalizeParams p = ParamsFromSettings(s);
    EXPECT_EQ(p.frameLen, s.dynaudnormFrameLen);
    EXPECT_EQ(p.gaussSize, s.dynaudnormGaussSize);
    EXPECT_FLOAT_EQ(p.peak, s.dynaudnormPeak);
    EXPECT_FLOAT_EQ(p.maxGain, s.dynaudnormMaxGain);
    EXPECT_FLOAT_EQ(p.volume, s.dynaudnormVolume);
}

TEST(SettingsMappingTest, AudioParamsTrackEditedSettings)
{
    AudioSettings s;
    s.dynaudnormFrameLen = 321;
    s.dynaudnormVolume = 2.25f;
    const AudioNormalizeParams p = ParamsFromSettings(s);
    EXPECT_EQ(p.frameLen, 321);
    EXPECT_FLOAT_EQ(p.volume, 2.25f);
}

TEST(SettingsMappingTest, AudioPreferencesMapDefaults)
{
    const AudioSettings s;
    const AudioPreferences p = AudioPrefsFromSettings(s);
    EXPECT_STREQ(p.preferredLang, "");
    EXPECT_STREQ(p.outputDevice, "");
    EXPECT_EQ(p.defaultVolume, 100);
    EXPECT_EQ(p.syncOffsetMs, 0);
    EXPECT_EQ(p.channelMode, AudioChannelMode::Auto);
    EXPECT_FALSE(p.duckingEnabled);
    EXPECT_EQ(p.duckingLevel, 50);
}

TEST(SettingsMappingTest, AudioPreferencesClampAndCopyFields)
{
    AudioSettings s;
    s.defaultLanguage = "english-too-long";
    s.outputDevice = "Headphones";
    s.defaultVolume = 150;
    s.syncOffsetMs = -125;
    s.channelMode = 99;
    s.duckingEnabled = true;
    s.duckingLevel = -5;

    const AudioPreferences p = AudioPrefsFromSettings(s);
    EXPECT_STREQ(p.preferredLang, "english");
    EXPECT_STREQ(p.outputDevice, "Headphones");
    EXPECT_EQ(p.defaultVolume, 100);
    EXPECT_EQ(p.syncOffsetMs, -125);
    EXPECT_EQ(p.channelMode, AudioChannelMode::Surround);
    EXPECT_TRUE(p.duckingEnabled);
    EXPECT_EQ(p.duckingLevel, 0);
}

TEST(SettingsMappingTest, AudioChannelModeMapsToOutputChannels)
{
    EXPECT_EQ(AudioOutputChannelsForMode(AudioChannelMode::Auto), 2);
    EXPECT_EQ(AudioOutputChannelsForMode(AudioChannelMode::Mono), 1);
    EXPECT_EQ(AudioOutputChannelsForMode(AudioChannelMode::Stereo), 2);
    EXPECT_EQ(AudioOutputChannelsForMode(AudioChannelMode::Surround), 6);
}

TEST(SettingsMappingTest, PlaybackOptionsMapBooleans)
{
    PlaybackSettings s;
    s.hwdec = false;
    s.hwdecMode = "off";
    s.hrSeek = true;
    s.subAutoLoad = true;
    s.audioFileAutoLoad = false;

    const PlaybackOptions o = PlaybackOptsFromSettings(s);
    EXPECT_FALSE(o.hwdec);
    EXPECT_TRUE(o.hrSeek);
    EXPECT_TRUE(o.subAutoLoad);
    EXPECT_FALSE(o.audioFileAutoLoad);
}

TEST(SettingsMappingTest, PlaybackOptionsTreatHwdecModeOffAsDisabled)
{
    PlaybackSettings s;
    s.hwdec = true;
    s.hwdecMode = "off";

    const PlaybackOptions o = PlaybackOptsFromSettings(s);
    EXPECT_FALSE(o.hwdec);
    EXPECT_EQ(VideoDecodeModeFromSettings(s), VideoDecodeMode::Off);
}

TEST(SettingsMappingTest, VideoDecodeModeMapsFromSettings)
{
    PlaybackSettings s;
    s.hwdecMode = "vulkan-zero-copy";
    EXPECT_EQ(VideoDecodeModeFromSettings(s), VideoDecodeMode::VulkanZeroCopy);

    s.hwdec = false;
    EXPECT_EQ(VideoDecodeModeFromSettings(s), VideoDecodeMode::Off);
}

TEST(SettingsMappingTest, ReadAheadDefaultsConvertMbToBytes)
{
    const CacheSettings s;
    const ReadAheadCacheOptions o = ReadAheadOptsFromSettings(s);
    EXPECT_TRUE(o.enabled);
    EXPECT_EQ(o.maxBytes, static_cast<int64_t>(s.readAheadSizeMB) * 1024 * 1024);
}

TEST(SettingsMappingTest, ReadAheadTracksEditedSettings)
{
    CacheSettings s;
    s.readAheadEnabled = false;
    s.readAheadSizeMB = 128;
    const ReadAheadCacheOptions o = ReadAheadOptsFromSettings(s);
    EXPECT_FALSE(o.enabled);
    EXPECT_EQ(o.maxBytes, int64_t{128} * 1024 * 1024);
}

TEST(SettingsMappingTest, ReadAheadClampsNegativeSizeToZero)
{
    CacheSettings s;
    s.readAheadSizeMB = -10;
    const ReadAheadCacheOptions o = ReadAheadOptsFromSettings(s);
    EXPECT_EQ(o.maxBytes, 0);
}

TEST(SettingsMappingTest, SubtitleColorsPackToAssRgba)
{
    SubtitleSettings s;
    s.textColor = "#FF8040"; // R=255 G=128 B=64
    s.outlineColor = "#000000";
    const SubtitleStyle st = SubtitleStyleFromSettings(s);
    // 0xRRGGBBAA, AA = transparency; text/outline are fully opaque (AA = 0x00).
    EXPECT_EQ(st.textColor, 0xFF8040'00u);
    EXPECT_EQ(st.outlineColor, 0x000000'00u);
}

TEST(SettingsMappingTest, SubtitleBackOpacityFoldsIntoAlpha)
{
    SubtitleSettings s;
    s.backColor = "#102030";
    s.backOpacity = 1.0f; // fully opaque ⇒ alpha byte 0x00
    EXPECT_EQ(SubtitleStyleFromSettings(s).backColor, 0x102030'00u);

    s.backOpacity = 0.0f; // fully transparent ⇒ alpha byte 0xFF
    EXPECT_EQ(SubtitleStyleFromSettings(s).backColor & 0xFFu, 0xFFu);

    s.backOpacity = 0.5f; // ~half ⇒ alpha byte 255 - round(0.5*255) = 255 - 128 = 127
    EXPECT_EQ(SubtitleStyleFromSettings(s).backColor & 0xFFu, 127u);
}

TEST(SettingsMappingTest, SubtitleStyleTracksFieldsAndDefaults)
{
    const SubtitleSettings def;
    const SubtitleStyle dst = SubtitleStyleFromSettings(def);
    EXPECT_FALSE(dst.overrideEnabled);
    EXPECT_EQ(dst.edgeStyle, SubtitleEdgeStyle::Outline);
    EXPECT_EQ(dst.alignment, 2);
    EXPECT_STREQ(dst.fontFamily, "");

    SubtitleSettings s;
    s.overrideStyle = true;
    s.fontScale = 1.5f;
    s.fontFamily = "Arial";
    s.edgeStyle = 3;
    s.alignment = 0; // file default
    s.defaultLanguage = "eng";
    s.preferForced = true;
    const SubtitleStyle st = SubtitleStyleFromSettings(s);
    EXPECT_TRUE(st.overrideEnabled);
    EXPECT_FLOAT_EQ(st.fontScale, 1.5f);
    EXPECT_STREQ(st.fontFamily, "Arial");
    EXPECT_EQ(st.edgeStyle, SubtitleEdgeStyle::OpaqueBox);
    EXPECT_EQ(st.alignment, 0);
    EXPECT_STREQ(st.preferredLang, "eng");
    EXPECT_TRUE(st.preferForced);
}

TEST(SettingsMappingTest, SubtitleAlignmentClampsOutOfRangeToFileDefault)
{
    SubtitleSettings s;
    s.alignment = 42; // invalid \an value
    EXPECT_EQ(SubtitleStyleFromSettings(s).alignment, 0);
}
