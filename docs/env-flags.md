# Environment Flags

Runtime launch and smoke-test controls use the `FL_` prefix. These flags are
process-local: they override the current launch without requiring edits to
`settings.ini`.

| Flag | Values | Default | Purpose |
| --- | --- | --- | --- |
| `FL_BACKEND` | `auto`, `vulkan`, `gl` | `auto` | Selects the graphics backend before Qt creates the window. |
| `FL_ACCEL_MODE` | `off`, `auto`, `vulkan-zero-copy`, `vulkan`, `cuda-zero-copy`, `cuda`, `d3d11va`, `dxva2`, `vaapi` | `settings.ini` | Overrides hardware decode acceleration mode for this launch. |
| `FL_LOG_LEVEL` | `debug`, `info`, `warn`, `error` | `info` | Sets the minimum host/plugin log level. |
| `FL_LOG_PERF` | `1`, `0`, `true`, `false`, `on`, `off` | `off` | Enables performance timing logs. |
| `FL_VULKAN_VALIDATION` | `1`, `0` | debug builds: `1`; release builds: `0` | Forces Vulkan validation on or off. |
| `FL_VK_HOST_COPY` | `1`, `0` | `auto` | Forces the Vulkan host-image-copy upload path on or off. |
| `FL_VK_NO_PUSH_DESC` | `1`, `0` | `0` | Disables Vulkan push descriptors for testing. |
| `FL_TEST_EXIT_AFTER_MS` | milliseconds | unset | Launch-test builds only: quits through the app event path after startup. |
