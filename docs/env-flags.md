# Environment Flags

Runtime launch and smoke-test controls use the `FL_` prefix. These flags are
process-local: they override the current launch without requiring edits to
`settings.ini`.

| Flag | Values | Default | Purpose |
| --- | --- | --- | --- |
| `FL_BACKEND` | `auto`, `vulkan`, `gl` | `auto` | Selects the graphics backend before Qt creates the window. |
| `FL_ACCEL_MODE` | `off`, `auto`, `vulkan-zero-copy`, `vulkan`, `cuda`, `d3d11va`, `dxva2`, `vaapi` | `settings.ini` | Overrides hardware decode acceleration mode for this launch. |
| `FL_LOG_LEVEL` | `debug`, `info`, `warn`, `error` | `info` | Sets the minimum host/plugin log level. |
| `FL_LOG_PERF` | `1`, `0`, `true`, `false`, `on`, `off` | `off` | Enables performance timing logs. |
| `FL_VULKAN_VALIDATION` | `1`, `0` | debug builds: `1`; release builds: `0` | Forces Vulkan validation on or off. |
| `FL_VK_HOST_COPY` | `1`, `0` | `auto` | Forces the Vulkan host-image-copy upload path on or off. |
| `FL_VK_NO_PUSH_DESC` | `1`, `0` | `0` | Disables Vulkan push descriptors for testing. |
| `FL_TEST_EXIT_AFTER_MS` | milliseconds | unset | Launch-test builds only: quits through the app event path after startup. |
| `FL_TEST_FILE` | spec, e.g. `30s-1080p-h264-srt` | unset | Generates (and caches) a synthetic test clip via the external `ffmpeg` CLI and plays it on launch; overrides the positional CLI file argument. |

## FL_TEST_FILE spec

The spec is a set of `-`-separated tokens in any order. Every dimension is
optional and allowed at most once; a duplicate or unknown token rejects the
whole spec (an error is logged and the app starts with no file).

| Dimension | Tokens | Default |
| --- | --- | --- |
| Duration | `<n>s`, `<n>m` (1 s – 1 h) | `30s` |
| Resolution | `360p`, `480p`, `720p`, `1080p`, `1440p`, `4k`, or explicit `<W>x<H>` (even dimensions) | `720p` |
| Video codec | `h264`, `hevc` (alias `h265`), `mpeg4`, `vp9`, `av1` | `h264` |
| Subtitles | `srt`, `ass` — embedded stream with a timestamp cue every 2 s | none |
| Audio | `noaudio` (a 440 Hz sine track is included by default) | audio on |

Examples: `FL_TEST_FILE=10s`, `FL_TEST_FILE=2m-4k-hevc`,
`FL_TEST_FILE=30s-1080p-h264-srt-noaudio`.

Generation shells out to the `ffmpeg` binary on `PATH` (`lavfi` `testsrc2` +
`sine`); output is always Matroska (`.mkv`). Clips are cached under the user
cache dir (`~/.cache/FrameLift/FrameLift/test-media/` on Linux) keyed by the canonical
spec, so token order does not fragment the cache and repeat launches are
instant. Delete that directory to force regeneration. If `ffmpeg` is missing or
encoding fails, the error is logged and the app starts with no file.
