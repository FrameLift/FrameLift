#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// Pure parsing for the FL_TEST_FILE launch flag. Extracted so it can be
// unit-tested without pulling in Qt or FFmpeg (same pattern as Cli.h). The spec
// is a set of `-`-separated tokens in any order, each dimension optional with a
// default and allowed at most once: duration (`10s`, `2m`), resolution (`720p`,
// `4k`, `WxH`), video codec (`h264`, `hevc`, `mpeg4`, `vp9`, `av1`), embedded
// subtitles (`srt`, `ass`) and `noaudio`. See docs/env-flags.md for the full
// grammar. TestMediaGenerator turns the parsed spec into an ffmpeg CLI run.

enum class TestSubtitleKind
{
    None,
    Srt,
    Ass
};

struct TestMediaSpec
{
    int durationSeconds = 30;
    int width = 1280;
    int height = 720;
    std::string codec = "h264"; // canonical token: h264|hevc|mpeg4|vp9|av1
    TestSubtitleKind subtitles = TestSubtitleKind::None;
    bool audio = true;
};

namespace test_media_detail
{

inline std::string ToLower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// Parse a whole string of digits; nullopt on empty/overflow/trailing garbage.
inline std::optional<int> ParseInt(std::string_view s)
{
    int value = 0;
    const char* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), end, value);
    if (ec != std::errc{} || ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

} // namespace test_media_detail

// Parse one spec string. Returns nullopt when the spec is empty, a token is
// unknown, or a dimension appears twice (reject instead of last-wins so typos
// fail loudly and the canonical cache name stays deterministic).
inline std::optional<TestMediaSpec> ParseTestMediaSpec(std::string_view rawSpec)
{
    using test_media_detail::ParseInt;
    using test_media_detail::ToLower;

    const std::string spec = ToLower(rawSpec);
    if (spec.empty())
    {
        return std::nullopt;
    }

    constexpr int kMaxDurationSeconds = 3600;
    constexpr int kMaxDimension = 7680;

    TestMediaSpec out;
    bool seenDuration = false;
    bool seenResolution = false;
    bool seenCodec = false;
    bool seenSubtitles = false;
    bool seenAudio = false;

    size_t pos = 0;
    while (pos <= spec.size())
    {
        const size_t dash = spec.find('-', pos);
        const std::string_view token =
            std::string_view(spec).substr(pos, dash == std::string::npos ? std::string::npos : dash - pos);
        pos = dash == std::string::npos ? spec.size() + 1 : dash + 1;

        if (token.empty())
        {
            return std::nullopt; // leading/trailing/double dash
        }

        // Codec / subtitle / audio keywords.
        if (token == "h264" || token == "hevc" || token == "h265" || token == "mpeg4" || token == "vp9" ||
            token == "av1")
        {
            if (seenCodec)
            {
                return std::nullopt;
            }
            seenCodec = true;
            out.codec = token == "h265" ? "hevc" : std::string(token);
            continue;
        }
        if (token == "srt" || token == "ass")
        {
            if (seenSubtitles)
            {
                return std::nullopt;
            }
            seenSubtitles = true;
            out.subtitles = token == "srt" ? TestSubtitleKind::Srt : TestSubtitleKind::Ass;
            continue;
        }
        if (token == "noaudio")
        {
            if (seenAudio)
            {
                return std::nullopt;
            }
            seenAudio = true;
            out.audio = false;
            continue;
        }

        // Named resolutions.
        struct NamedRes
        {
            std::string_view name;
            int w, h;
        };

        constexpr std::array<NamedRes, 6> kNamed = {{
            {"360p", 640, 360},
            {"480p", 854, 480},
            {"720p", 1280, 720},
            {"1080p", 1920, 1080},
            {"1440p", 2560, 1440},
            {"4k", 3840, 2160},
        }};
        bool matchedNamed = false;
        for (const auto& [name, w, h] : kNamed)
        {
            if (token == name)
            {
                if (seenResolution)
                {
                    return std::nullopt;
                }
                seenResolution = true;
                out.width = w;
                out.height = h;
                matchedNamed = true;
                break;
            }
        }
        if (matchedNamed)
        {
            continue;
        }

        // Duration: <n>s / <n>m.
        if (token.size() >= 2 && (token.back() == 's' || token.back() == 'm'))
        {
            if (const auto n = ParseInt(token.substr(0, token.size() - 1)))
            {
                if (seenDuration)
                {
                    return std::nullopt;
                }
                seenDuration = true;
                const long long seconds = token.back() == 'm' ? static_cast<long long>(*n) * 60 : *n;
                if (seconds < 1 || seconds > kMaxDurationSeconds)
                {
                    return std::nullopt;
                }
                out.durationSeconds = static_cast<int>(seconds);
                continue;
            }
        }

        // Explicit resolution: <W>x<H>. Even dimensions only — the encoders run
        // with yuv420p, which needs 2x2-aligned frames.
        if (const size_t x = token.find('x'); x != std::string_view::npos && x > 0 && x + 1 < token.size())
        {
            const auto w = ParseInt(token.substr(0, x));
            const auto h = ParseInt(token.substr(x + 1));
            if (w && h)
            {
                if (seenResolution)
                {
                    return std::nullopt;
                }
                seenResolution = true;
                if (*w < 2 || *h < 2 || *w > kMaxDimension || *h > kMaxDimension || *w % 2 != 0 || *h % 2 != 0)
                {
                    return std::nullopt;
                }
                out.width = *w;
                out.height = *h;
                continue;
            }
        }

        return std::nullopt; // unknown token
    }

    return out;
}

// Deterministic cache-file stem (no extension) that spells out every dimension,
// so any token order of the same spec hits the same cache entry:
// "30s-1920x1080-h264-audio-srt".
inline std::string CanonicalTestMediaName(const TestMediaSpec& s)
{
    std::string name =
        std::format("{}s-{}x{}-{}-{}", s.durationSeconds, s.width, s.height, s.codec, s.audio ? "audio" : "noaudio");
    if (s.subtitles == TestSubtitleKind::Srt)
    {
        name += "-srt";
    }
    else if (s.subtitles == TestSubtitleKind::Ass)
    {
        name += "-ass";
    }
    return name;
}

// SubRip text with one 2-second cue per slot, each showing its own start
// timestamp ("FrameLift 00:14"), so seek targets are visually verifiable.
inline std::string BuildTestSubtitleCues(const int durationSeconds)
{
    std::string out;
    int index = 1;
    for (int start = 0; start < durationSeconds; start += 2, ++index)
    {
        const int end = std::min(start + 2, durationSeconds);
        out += std::format(
            "{}\n{:02}:{:02}:{:02},000 --> {:02}:{:02}:{:02},000\nFrameLift {:02}:{:02}\n\n", index, start / 3600,
            start / 60 % 60, start % 60, end / 3600, end / 60 % 60, end % 60, start / 60 % 60, start % 60
        );
    }
    return out;
}
