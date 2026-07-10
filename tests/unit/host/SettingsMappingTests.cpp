#include "FFmpegAudioOptions.h"
#include "SettingsMapping.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class SettingsMappingTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void AudioParamsFromDefaults()
    {
        const AudioSettings s;
        const AudioNormalizeParams p = ToAudioNormalizeParams(s);
        QVERIFY((p.algorithm) == (AudioNormalizeAlgorithm::Limiter));
        QCOMPARE(p.limiterLevelIn, s.limiterLevelIn);
        QCOMPARE(p.limiterLevelOut, s.limiterLevelOut);
        QCOMPARE(p.limiterLimit, s.limiterLimit);
        QCOMPARE(p.limiterAttack, s.limiterAttack);
        QCOMPARE(p.limiterRelease, s.limiterRelease);
        QVERIFY((p.frameLen) == (s.dynaudnormFrameLen));
        QVERIFY((p.gaussSize) == (s.dynaudnormGaussSize));
        QCOMPARE(p.peak, s.dynaudnormPeak);
        QCOMPARE(p.maxGain, s.dynaudnormMaxGain);
        QCOMPARE(p.volume, s.dynaudnormVolume);
    }

    void AudioParamsTrackEditedSettings()
    {
        AudioSettings s;
        s.normalizeMode = "dynaudnorm";
        s.limiterLevelIn = 2.25f;
        s.dynaudnormFrameLen = 321;
        s.dynaudnormVolume = 2.25f;
        const AudioNormalizeParams p = ToAudioNormalizeParams(s);
        QVERIFY((p.algorithm) == (AudioNormalizeAlgorithm::DynamicNormalizer));
        QCOMPARE(p.limiterLevelIn, 2.25f);
        QVERIFY((p.frameLen) == (321));
        QCOMPARE(p.volume, 2.25f);
    }

    void InvalidAudioModeFallsBackToLimiter()
    {
        AudioSettings s;
        s.normalizeMode = "unknown";
        QVERIFY((ToAudioNormalizeParams(s).algorithm) == (AudioNormalizeAlgorithm::Limiter));
    }

    void AudioPreferencesMapDefaults()
    {
        const AudioSettings s;
        const AudioPreferences p = ToAudioPreferences(s);
        QVERIFY(::framelift::test::CStringEqual(p.preferredLang, ""));
        QVERIFY(::framelift::test::CStringEqual(p.outputDevice, ""));
        QVERIFY((p.defaultVolume) == (100));
        QVERIFY((p.syncOffsetMs) == (0));
        QVERIFY((p.channelMode) == (AudioChannelMode::Auto));
        QVERIFY(!(p.duckingEnabled));
        QVERIFY((p.duckingLevel) == (50));
    }

    void AudioPreferencesClampAndCopyFields()
    {
        AudioSettings s;
        s.defaultLanguage = "english-too-long";
        s.outputDevice = "Headphones";
        s.defaultVolume = 150;
        s.syncOffsetMs = -125;
        s.channelMode = 99;
        s.duckingEnabled = true;
        s.duckingLevel = -5;

        const AudioPreferences p = ToAudioPreferences(s);
        QVERIFY(::framelift::test::CStringEqual(p.preferredLang, "english"));
        QVERIFY(::framelift::test::CStringEqual(p.outputDevice, "Headphones"));
        QVERIFY((p.defaultVolume) == (100));
        QVERIFY((p.syncOffsetMs) == (-125));
        QVERIFY((p.channelMode) == (AudioChannelMode::Surround));
        QVERIFY(p.duckingEnabled);
        QVERIFY((p.duckingLevel) == (0));
    }

    void AudioChannelModeMapsToOutputChannels()
    {
        QVERIFY((AudioOutputChannelsForMode(AudioChannelMode::Auto)) == (2));
        QVERIFY((AudioOutputChannelsForMode(AudioChannelMode::Mono)) == (1));
        QVERIFY((AudioOutputChannelsForMode(AudioChannelMode::Stereo)) == (2));
        QVERIFY((AudioOutputChannelsForMode(AudioChannelMode::Surround)) == (6));
    }

    void PlaybackOptionsMapBooleans()
    {
        PlaybackSettings s;
        s.hwdecMode = "off";
        s.subAutoLoad = true;
        s.audioFileAutoLoad = false;

        const PlaybackOptions o = ToPlaybackOptions(s);
        QVERIFY(!(o.hwdec));
        QVERIFY(o.hrSeek); // default seekMode "smart" keeps the SDK hrSeek bool true
        QVERIFY(o.subAutoLoad);
        QVERIFY(!(o.audioFileAutoLoad));
    }

    void SeekModeMapsToPrecisionAndKeepsHrSeekCoherent()
    {
        PlaybackSettings s;
        QVERIFY((ToSeekPrecisionMode(s)) == (SeekPrecisionMode::Smart)); // default "smart"

        s.seekMode = "exact";
        QVERIFY((ToSeekPrecisionMode(s)) == (SeekPrecisionMode::Exact));
        QVERIFY(ToPlaybackOptions(s).hrSeek);

        s.seekMode = "keyframe";
        QVERIFY((ToSeekPrecisionMode(s)) == (SeekPrecisionMode::Keyframe));
        QVERIFY(!(ToPlaybackOptions(s).hrSeek)); // keyframe-everything is the only hrSeek=false case

        s.seekMode = "not-a-mode";
        QVERIFY((ToSeekPrecisionMode(s)) == (SeekPrecisionMode::Smart)); // unknown falls back
    }

    void PlaybackOptionsTreatHwdecModeOffAsDisabled()
    {
        PlaybackSettings s;
        s.hwdecMode = "off";

        const PlaybackOptions o = ToPlaybackOptions(s);
        QVERIFY(!(o.hwdec));
        QVERIFY((ToVideoDecodeMode(s)) == (VideoDecodeMode::Off));
    }

    void VideoDecodeModeMapsFromSettings()
    {
        PlaybackSettings s;
        s.hwdecMode = "vulkan-zero-copy";
        QVERIFY((ToVideoDecodeMode(s)) == (VideoDecodeMode::VulkanZeroCopy));

        s.hwdecMode = "off";
        QVERIFY((ToVideoDecodeMode(s)) == (VideoDecodeMode::Off));
    }

    void ReadAheadDefaultsConvertMbToBytes()
    {
        const CacheSettings s;
        const ReadAheadCacheOptions o = ToReadAheadCacheOptions(s);
        QVERIFY(o.enabled);
        QVERIFY((o.maxBytes) == (static_cast<int64_t>(s.readAheadSizeMB) * 1024 * 1024));
    }

    void ReadAheadTracksEditedSettings()
    {
        CacheSettings s;
        s.readAheadEnabled = false;
        s.readAheadSizeMB = 128;
        const ReadAheadCacheOptions o = ToReadAheadCacheOptions(s);
        QVERIFY(!(o.enabled));
        QVERIFY((o.maxBytes) == (int64_t{128} * 1024 * 1024));
    }

    void ReadAheadClampsNegativeSizeToZero()
    {
        CacheSettings s;
        s.readAheadSizeMB = -10;
        const ReadAheadCacheOptions o = ToReadAheadCacheOptions(s);
        QVERIFY((o.maxBytes) == (0));
    }

    void SubtitleColorsPackToAssRgba()
    {
        SubtitleSettings s;
        s.textColor = "#FF8040"; // R=255 G=128 B=64
        s.outlineColor = "#000000";
        const SubtitleStyle st = ToSubtitleStyle(s);
        // 0xRRGGBBAA, AA = transparency; text/outline are fully opaque (AA = 0x00).
        QVERIFY((st.textColor) == (0xFF8040'00u));
        QVERIFY((st.outlineColor) == (0x000000'00u));
    }

    void SubtitleBackOpacityFoldsIntoAlpha()
    {
        SubtitleSettings s;
        s.backColor = "#102030";
        s.backOpacity = 1.0f; // fully opaque ⇒ alpha byte 0x00
        QVERIFY((ToSubtitleStyle(s).backColor) == (0x102030'00u));

        s.backOpacity = 0.0f; // fully transparent ⇒ alpha byte 0xFF
        QVERIFY((ToSubtitleStyle(s).backColor & 0xFFu) == (0xFFu));

        s.backOpacity = 0.5f; // ~half ⇒ alpha byte 255 - round(0.5*255) = 255 - 128 = 127
        QVERIFY((ToSubtitleStyle(s).backColor & 0xFFu) == (127u));
    }

    void SubtitleStyleTracksFieldsAndDefaults()
    {
        const SubtitleSettings def;
        const SubtitleStyle dst = ToSubtitleStyle(def);
        QVERIFY(!(dst.overrideEnabled));
        QVERIFY((dst.edgeStyle) == (SubtitleEdgeStyle::Outline));
        QVERIFY((dst.alignment) == (2));
        QVERIFY(::framelift::test::CStringEqual(dst.fontFamily, ""));

        SubtitleSettings s;
        s.overrideStyle = true;
        s.fontScale = 1.5f;
        s.fontFamily = "Arial";
        s.edgeStyle = 3;
        s.alignment = 0; // file default
        s.defaultLanguage = "eng";
        s.preferForced = true;
        const SubtitleStyle st = ToSubtitleStyle(s);
        QVERIFY(st.overrideEnabled);
        QCOMPARE(st.fontScale, 1.5f);
        QVERIFY(::framelift::test::CStringEqual(st.fontFamily, "Arial"));
        QVERIFY((st.edgeStyle) == (SubtitleEdgeStyle::OpaqueBox));
        QVERIFY((st.alignment) == (0));
        QVERIFY(::framelift::test::CStringEqual(st.preferredLang, "eng"));
        QVERIFY(st.preferForced);
    }

    void SubtitleAlignmentClampsOutOfRangeToFileDefault()
    {
        SubtitleSettings s;
        s.alignment = 42; // invalid \an value
        QVERIFY((ToSubtitleStyle(s).alignment) == (0));
    }
};

namespace
{
const ::framelift::test::Registrar<SettingsMappingTest> kRegisterSettingsMappingTest{"SettingsMappingTest"};
}

#include "SettingsMappingTests.moc"
