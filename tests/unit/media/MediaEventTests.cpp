// Contract tests for the media-event surface the SDK exposes to plugins
// (framelift/platform/IMediaPlayer.h). The backend→MediaEvent translation itself lives
// in FFmpegPlayer.cpp, which needs the FFmpeg libraries and so is not part of
// this platform-independent suite; these tests instead pin the ABI-visible enum
// layout and the string-property payload behaviour that plugins rely on.

#include <framelift/platform/IMediaPlayer.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

// Plugins switch on MediaEventType, so the lifecycle events must be distinct
// values that append after the original set (appending keeps older plugins'
// understanding of the existing values stable across the ABI bump).
TEST(MediaEventTest, LifecycleEventTypesAreDistinctAndAppended)
{
    EXPECT_EQ(static_cast<int>(MediaEventType::None), 0);

    const int lastOriginal = static_cast<int>(MediaEventType::PropertyChange);
    EXPECT_GT(static_cast<int>(MediaEventType::StartFile), lastOriginal);
    EXPECT_GT(static_cast<int>(MediaEventType::FileLoaded), lastOriginal);
    EXPECT_GT(static_cast<int>(MediaEventType::PlaybackRestart), lastOriginal);
    EXPECT_GT(static_cast<int>(MediaEventType::Seek), lastOriginal);
    EXPECT_GT(static_cast<int>(MediaEventType::AudioReconfig), lastOriginal);

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
            EXPECT_NE(vals[i], vals[j]);
        }
    }
}

// A PropertyChange carrying a string value: plugins read value.str when
// type == String. The value must be a NUL-terminated copy.
TEST(MediaEventTest, StringPropertyValueRoundTrips)
{
    MediaEvent e;
    e.type = MediaEventType::PropertyChange;
    e.property.prop = PlayerProperty::MediaTitle;
    e.property.type = PropertyType::String;
    // Mirrors the bounded copy FFmpegPlayer::PollEvent performs.
    std::snprintf(e.property.value.str, sizeof(e.property.value.str), "%s", "A Title");

    EXPECT_EQ(e.property.type, PropertyType::String);
    EXPECT_STREQ(e.property.value.str, "A Title");
}

// An over-long source string must be truncated into the fixed buffer and stay
// NUL-terminated — the property of snprintf the copy relies on for safety.
TEST(MediaEventTest, StringPropertyValueTruncatesSafely)
{
    MediaEvent e;
    e.property.type = PropertyType::String;
    const std::string tooLong(1000, 'x');
    std::snprintf(e.property.value.str, sizeof(e.property.value.str), "%s", tooLong.c_str());

    EXPECT_EQ(std::strlen(e.property.value.str), sizeof(e.property.value.str) - 1);
    EXPECT_EQ(e.property.value.str[sizeof(e.property.value.str) - 1], '\0');
}
