#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

// ── URL helpers (header-only, plugin-side) ───────────────────────────────────
// Shared between the host's file-open handlers (Playlist) and the RemoteStream
// plugin so both classify paths identically. Header-only — compiled into each
// consumer; does NOT cross the host/plugin ABI boundary.

namespace framelift
{

// The dedicated scheme reserved for custom/encrypted remote streams handled by
// the RemoteStream plugin (or a user-supplied replacement). Plain remote schemes
// (http/https/rtsp/...) are read by FFmpeg directly.
inline constexpr const char* kSecureStreamScheme = "flsec";

// Extract the URL scheme (lowercased) preceding "://", or "" when `path` has none.
//   "https://host/x" -> "https"      "flsec://a"   -> "flsec"
//   "C:\\file.mp4"    -> ""           "/home/x.mkv" -> ""        "x.mp4" -> ""
// A scheme is ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) per RFC 3986, so a
// Windows drive letter ("C:\\...") is never mistaken for a scheme.
inline std::string UrlScheme(const char* path) noexcept
{
    if (!path)
    {
        return {};
    }
    const std::string_view sv(path);
    const std::size_t pos = sv.find("://");
    if (pos == std::string_view::npos || pos == 0)
    {
        return {};
    }
    if (!std::isalpha(static_cast<unsigned char>(sv[0])))
    {
        return {};
    }
    std::string scheme;
    scheme.reserve(pos);
    for (std::size_t i = 0; i < pos; ++i)
    {
        const auto c = static_cast<unsigned char>(sv[i]);
        if (!(std::isalnum(c) || c == '+' || c == '-' || c == '.'))
        {
            return {}; // stray "://" inside a non-URL string
        }
        scheme.push_back(static_cast<char>(std::tolower(c)));
    }
    return scheme;
}

// True when `path` is a remote/URL-style locator (has a "scheme://" prefix)
// rather than a local filesystem path.
inline bool IsRemoteUrl(const char* path) noexcept
{
    return !UrlScheme(path).empty();
}

} // namespace framelift
