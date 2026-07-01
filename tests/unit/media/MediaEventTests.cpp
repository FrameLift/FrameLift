// Contract tests for the media-event surface the SDK exposes to plugins
// (framelift/platform/IMediaPlayer.h). The backend→MediaEvent translation itself lives
// in FFmpegPlayer.cpp, which needs the FFmpeg libraries and so is not part of
// this platform-independent suite; these tests instead pin the ABI-visible enum
// layout and the string-property payload behaviour that plugins rely on.

#include <framelift/platform/IMediaPlayer.h>

#include "QtTestRunner.h"

#include <QtTest/QtTest>

#include <cstdio>
#include <cstring>
#include <string>

// Plugins switch on MediaEventType, so the lifecycle events must be distinct
// values that append after the original set (appending keeps older plugins'
// understanding of the existing values stable across the ABI bump).
class MediaEventTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void LifecycleEventTypesAreDistinctAndAppended()
    {
        QVERIFY((static_cast<int>(MediaEventType::None)) == (0));

        const int lastOriginal = static_cast<int>(MediaEventType::PropertyChange);
        QVERIFY((static_cast<int>(MediaEventType::StartFile)) > (lastOriginal));
        QVERIFY((static_cast<int>(MediaEventType::FileLoaded)) > (lastOriginal));
        QVERIFY((static_cast<int>(MediaEventType::PlaybackRestart)) > (lastOriginal));
        QVERIFY((static_cast<int>(MediaEventType::Seek)) > (lastOriginal));
        QVERIFY((static_cast<int>(MediaEventType::AudioReconfig)) > (lastOriginal));

        // All new values are mutually distinct.
        const int vals[] = {
            static_cast<int>(MediaEventType::StartFile),       static_cast<int>(MediaEventType::FileLoaded),
            static_cast<int>(MediaEventType::PlaybackRestart), static_cast<int>(MediaEventType::Seek),
            static_cast<int>(MediaEventType::AudioReconfig),
        };
        for (size_t i = 0; i < std::size(vals); ++i)
        {
            for (size_t j = i + 1; j < std::size(vals); ++j)
            {
                QVERIFY((vals[i]) != (vals[j]));
            }
        }
    }

    // EndFileReason gained classified failure values (issue #13). They must be distinct,
    // ordered after the original Eof/Error/Other, and the enum must stay a single byte so
    // the MediaEvent layout is unchanged (no ABI bump).
    void EndFileReasonClassifiedValuesAreDistinctAndAppended()
    {
        QVERIFY((sizeof(EndFileReason)) == (1u));

        const int lastOriginal = static_cast<int>(EndFileReason::Other);
        QVERIFY((static_cast<int>(EndFileReason::NotFound)) > (lastOriginal));
        QVERIFY((static_cast<int>(EndFileReason::Unsupported)) > (lastOriginal));
        QVERIFY((static_cast<int>(EndFileReason::Corrupt)) > (lastOriginal));
        QVERIFY((static_cast<int>(EndFileReason::NoStream)) > (lastOriginal));

        const int vals[] = {
            static_cast<int>(EndFileReason::Eof),         static_cast<int>(EndFileReason::Error),
            static_cast<int>(EndFileReason::Other),       static_cast<int>(EndFileReason::NotFound),
            static_cast<int>(EndFileReason::Unsupported), static_cast<int>(EndFileReason::Corrupt),
            static_cast<int>(EndFileReason::NoStream),
        };
        for (size_t i = 0; i < std::size(vals); ++i)
        {
            for (size_t j = i + 1; j < std::size(vals); ++j)
            {
                QVERIFY((vals[i]) != (vals[j]));
            }
        }
    }

    // DecodeErrors was appended before Unknown (after the cache metrics), keeping every
    // earlier PlayerProperty ordinal stable.
    void DecodeErrorsPropertyAppendedBeforeUnknown()
    {
        QVERIFY((static_cast<int>(PlayerProperty::DecodeErrors)) > (static_cast<int>(PlayerProperty::CacheMisses)));
        QVERIFY((static_cast<int>(PlayerProperty::DecodeErrors)) < (static_cast<int>(PlayerProperty::Unknown)));
    }

    // A PropertyChange carrying a string value: plugins read value.str when
    // type == String. The value must be a NUL-terminated copy.
    void StringPropertyValueRoundTrips()
    {
        MediaEvent e;
        e.type = MediaEventType::PropertyChange;
        e.property.prop = PlayerProperty::MediaTitle;
        e.property.type = PropertyType::String;
        // Mirrors the bounded copy FFmpegPlayer::PollEvent performs.
        std::snprintf(e.property.value.str, sizeof(e.property.value.str), "%s", "A Title");

        QVERIFY((e.property.type) == (PropertyType::String));
        QVERIFY(::framelift::test::CStringEqual(e.property.value.str, "A Title"));
    }

    // An over-long source string must be truncated into the fixed buffer and stay
    // NUL-terminated — the property of snprintf the copy relies on for safety.
    void StringPropertyValueTruncatesSafely()
    {
        MediaEvent e;
        e.property.type = PropertyType::String;
        const std::string tooLong(1000, 'x');
        std::snprintf(e.property.value.str, sizeof(e.property.value.str), "%s", tooLong.c_str());

        QVERIFY((std::strlen(e.property.value.str)) == (sizeof(e.property.value.str) - 1));
        QVERIFY((e.property.value.str[sizeof(e.property.value.str) - 1]) == ('\0'));
    }
};

namespace
{
const ::framelift::test::Registrar<MediaEventTest> kRegisterMediaEventTest{"MediaEventTest"};
}

#include "MediaEventTests.moc"
