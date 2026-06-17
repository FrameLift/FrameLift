#include "SettingsMenu.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <utility>

#include "Version.h"
#include <ThemeUtil.h>
#include <framelift/core.h>
#include <framelift/platform/IMediaPlayer.h>
#include <framelift/ui.h>

// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>

// ── Layout constants ──────────────────────────────────────────────────────────

namespace
{
constexpr float SideW = 120.f;
constexpr float BotH = 44.f;

// Fixed capacity for the raw-config edit buffer. A settings.ini is a few KB at
// most; the cap exists only because the ABI multiline widget takes a plain
// char* (no resize callback crosses the boundary).
constexpr int ConfigBufCap = 64 * 1024;
} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void SettingsMenu::SetStoragePath(std::string path)
{
    storagePath_ = std::move(path);
    settings_.Load(storagePath_);
    saved_ = settings_;
}

std::vector<framelift::Keybind> SettingsMenu::Keybinds()
{
    return {
        {"Open settings", "openSettings", &openSettingsKey_, "Ctrl+Comma", [this]
         {
             Open();
         }}
    };
}

void SettingsMenu::SeedFromContext(IPluginContext& ctx)
{
    // Seed the editing model from the authoritative host settings via the
    // ABI-stable per-key getters (no Settings struct crosses the boundary).
    auto load = [&]<typename T0>(const char* key, T0& field)
    {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, float>)
        {
            field = ctx.GetSettingFloat(key);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            field = ctx.GetSettingBool(key);
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            field = ctx.GetSettingInt(key);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            const int n = ctx.GetSettingString(key, nullptr, 0);
            std::string s(static_cast<std::size_t>(n), '\0');
            if (n > 0)
            {
                ctx.GetSettingString(key, s.data(), n + 1);
            }
            field = std::move(s);
        }
    };
#define X(section, name, type, def, desc) load(#section "." #name, settings_.name);
    SETTINGS_FIELDS(X)
#undef X
    saved_ = settings_;
    dirty_ = false;
}

void SettingsMenu::OnInstall(IPluginContext& ctx)
{
    SeedFromContext(ctx);

    // Register the six built-in pages through the same host pipeline plugins use,
    // so the sidebar, content dispatch and per-page reset all run one code path.
    RegisterCorePages(ctx);

    // Register a hidden page so SaveSettings() is called on Apply to persist
    // openSettingsKey_. No visible UI needed for SettingsMenu itself.
    SetupSettingsPage(ctx, false);

    if (auto* menu = ctx.GetService<ContextMenu>())
    {
        framelift::AddSection(
            *menu,
            [this](ContextMenu& m)
            {
                m.AddSeparator();
                framelift::AddItem(
                    m, "Settings", "openSettings",
                    [this]
                    {
                        Open();
                    }
                );
            }
        );
    }
}

void SettingsMenu::CoreRenderThunk(void* ud, UIContext& ctx)
{
    const auto* page = static_cast<CorePage*>(ud);
    (page->self->*(page->render))(ctx);
}

void SettingsMenu::RegisterCorePages(IPluginContext& ctx)
{
    corePages_ = {{
        {this, &SettingsMenu::RenderPageGeneral, "General", "general"},
        {this, &SettingsMenu::RenderPageGraphics, "Graphics", "graphics"},
        {this, &SettingsMenu::RenderPagePlayback, "Playback", "playback"},
        {this, &SettingsMenu::RenderPageSubtitles, "Subtitles", "subtitles"},
        {this, &SettingsMenu::RenderPageCache, "Cache", "cache"},
        {this, &SettingsMenu::RenderPageUI, "UI", "ui"},
        {this, &SettingsMenu::RenderPageTheme, "Theme", "theme"},
        {this, &SettingsMenu::RenderPageFiles, "Files", "files"},
        {this, &SettingsMenu::RenderPageAudio, "Audio", "audio"},
        {this, &SettingsMenu::RenderPageKeybinds, "Keybinds", "keybinds"},
        // Read-only catalogue of loaded plugins — no settings section to reset.
        {this, &SettingsMenu::RenderPagePlugins, "Plugins", nullptr},
        // Raw settings.ini editor — operates on the file directly, not a section.
        {this, &SettingsMenu::RenderPageConfig, "Config", nullptr},
    }};

    // applyFn is null: core fields are committed wholesale in Save() via the
    // SETTINGS_FIELDS macro, so no per-page apply is needed.
    for (auto& page : corePages_)
    {
        ctx.RegisterSettingsPage(page.title, &CoreRenderThunk, nullptr, &page, true, nullptr);
    }
}

bool SettingsMenu::HandleKeyDownEvent(const AppEvent& e)
{
    if (!isCapturing_)
    {
        // While the dialog is open it holds keyboard focus and swallows every key
        // press so global hotkeys don't fire underneath it. ImGui already received
        // the event earlier in App::Dispatch, so text fields still type normally.
        return open_;
    }
    const AppEvent::KeyPayload& kp = e.AsKey();
    if (kp.key == Keys::Escape)
    {
        SetCapturing(false);
        return true;
    }
    char bindBuf[64] = {};
    KeyBindToString(kp.key, kp.mods, bindBuf, sizeof(bindBuf));
    const std::string bindStr = bindBuf;
    if (capturingBind_)
    {
        *capturingBind_ = bindStr;
    }
    else if (capturingSetStr_)
    {
        capturingSetStr_(capturingUd_, bindStr.c_str());
    }
    if (!capturingName_.empty())
    {
        if (auto* hk = ctx_ ? ctx_->GetService<Hotkeys>() : nullptr)
        {
            hk->Rebind(capturingName_.c_str(), kp.key, kp.mods);
        }
    }
    dirty_ = true;
    SetCapturing(false);
    return true;
}

void SettingsMenu::SetCapturing(const bool v)
{
    // Focus is held for the whole time the dialog is open (see Open/Close), so
    // capturing only toggles the local capture state — it must not touch the
    // FocusManager, or ending a capture would drop the still-open dialog's focus.
    isCapturing_ = v;
    if (!v)
    {
        capturingBind_ = nullptr;
        capturingGetStr_ = nullptr;
        capturingSetStr_ = nullptr;
        capturingUd_ = nullptr;
        capturingName_.clear();
    }
}

void SettingsMenu::Open() noexcept
{
    open_ = true;
    activePageIndex_ = -1; // resolved to the General page on first render
    // Grab keyboard focus so App::Dispatch routes key events here first and our
    // HandleKeyDownEvent can swallow them before the global hotkey layer.
    if (auto* fm = ctx_ ? ctx_->GetService<FocusManager>() : nullptr)
    {
        fm->Acquire(this);
    }
    if (ctx_)
    {
        ctx_->Publish<SettingsVisibilityEvent>({true});
    }
}

void SettingsMenu::Close() noexcept
{
    open_ = false;
    if (isCapturing_)
    {
        SetCapturing(false);
    }
    if (auto* fm = ctx_ ? ctx_->GetService<FocusManager>() : nullptr)
    {
        fm->Release(this);
    }
    if (ctx_)
    {
        ctx_->Publish<SettingsVisibilityEvent>({false});
    }
}

void SettingsMenu::RegisterChangeCallback(std::function<void(const Settings&)> cb)
{
    changeCallbacks_.push_back(std::move(cb));
}

// ── Internal helpers ─────────────────────────────────────────────────────────────

void SettingsMenu::Save()
{
    saved_ = settings_;
    dirty_ = false;
    if (ctx_)
    {
        // Call all plugin applyFns first so plugin settings are written before SaveSettings().
        ctx_->EnumerateSettingsPages(
            [](const char*, void (*)(void*, UIContext&), void (*applyFn)(void*), void* ud, bool, void*)
            {
                if (applyFn)
                {
                    applyFn(ud);
                }
            },
            nullptr
        );

        // Commit each typed settings field via the ABI-stable per-key API.
        // Generic lambda → the per-field if constexpr is dependent and discards
        // mismatched branches correctly (a plain function body would hard-check them).
        auto commit = [&]<typename T0>(const char* key, const T0& field)
        {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, float>)
            {
                ctx_->CommitSettingFloat(key, field);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                ctx_->CommitSettingBool(key, field);
            }
            else if constexpr (std::is_same_v<T, int>)
            {
                ctx_->CommitSettingInt(key, field);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                ctx_->CommitSettingString(key, field.c_str());
            }
        };
#define X(section, name, type, def, desc) commit(#section "." #name, settings_.name);
        SETTINGS_FIELDS(X)
#undef X
        ctx_->SaveSettings();
    }
    else if (!storagePath_.empty())
    {
        settings_.Save(storagePath_);
    }
    FireChangeCallbacks();
}

void SettingsMenu::Reset()
{
    settings_ = Settings{};
    dirty_ = true;
    FireChangeCallbacks();
}

const char* SettingsMenu::PageName() const
{
    if (!ctx_ || activePageIndex_ < 0)
    {
        return "Current";
    }

    // Walk the enumeration to find the title at activePageIndex_.
    struct S
    {
        int target;
        int cur = 0;
        const char* found = nullptr;
    };

    S s{activePageIndex_};
    ctx_->EnumerateSettingsPages(
        [](const char* title, void (*)(void*, UIContext&), void (*)(void*), void*, bool, void* pv)
        {
            auto& s = *static_cast<S*>(pv);
            if (s.cur++ == s.target)
            {
                s.found = title;
            }
        },
        &s
    );
    return s.found ? s.found : "Current";
}

void SettingsMenu::ResetPage()
{
    if (!ctx_ || activePageIndex_ < 0)
    {
        return;
    }

    // Resolve the active page's user-data so we can tell core pages from plugin
    // pages and map a core page back to its settings section.
    struct S
    {
        int target;
        int cur = 0;
        void* ud = nullptr;
    };

    S s{activePageIndex_};
    ctx_->EnumerateSettingsPages(
        [](const char*, void (*)(void*, UIContext&), void (*)(void*), void* ud, bool, void* pv)
        {
            auto& s = *static_cast<S*>(pv);
            if (s.cur++ == s.target)
            {
                s.ud = ud;
            }
        },
        &s
    );

    if (!s.ud || !IsCorePageUd(s.ud))
    {
        return; // plugin pages have no per-page reset
    }
    const char* const sec0 = static_cast<const CorePage*>(s.ud)->resetSection;

    const Settings defaults{};
#define X(section, name, type, def, desc)                                                                             \
    if (sec0 && strcmp(#section, sec0) == 0)                                                                           \
        settings_.name = defaults.name;
    SETTINGS_FIELDS(X)
#undef X

    dirty_ = true;
    FireChangeCallbacks();
}

void SettingsMenu::FireChangeCallbacks() const
{
    for (const auto& cb : changeCallbacks_)
    {
        cb(settings_);
    }
}

// ── Sidebar ───────────────────────────────────────────────────────────────────

void SettingsMenu::RenderSidebar(UIContext& ctx)
{
    if (!ctx_)
    {
        return;
    }

    // One flat list, grouped: core pages, a thin separator, then plugin pages.
    // Done in two passes so the grouping holds regardless of plugin load order.
    struct Pass
    {
        SettingsMenu* self;
        UIContext* ctx;
        bool wantCore;         // which group this pass renders
        bool coreRendered;     // did the core pass render anything (for separator)
        bool rendered = false; // did this pass render anything yet
        int idx = 0;
    };

    auto visit = [](const char* title, void (*)(void*, UIContext&), void (*)(void*), void* ud, bool visible, void* pv)
    {
        auto& [self, ctx, wantCore, coreRendered, rendered, idx] = *static_cast<Pass*>(pv);
        const int i = idx++;
        if (!visible || self->IsCorePageUd(ud) != wantCore)
        {
            return;
        }
        // Divider before the first plugin entry, only if core pages came before it.
        if (!wantCore && !rendered && coreRendered)
        {
            ctx->Dummy({0.f, 4.f});
            ctx->SeparatorLine(UI::MakeColor32(90, 70, 130, 160));
            ctx->Dummy({0.f, 4.f});
        }
        const bool active = self->activePageIndex_ == i;
        if (ctx->Selectable(title, active, UI::SelectableFlags::None, {0.f, 28.f}))
        {
            self->activePageIndex_ = i;
        }
        rendered = true;
    };

    Pass corePass{this, &ctx, true, false};
    ctx_->EnumerateSettingsPages(visit, &corePass);

    Pass pluginPass{this, &ctx, false, corePass.rendered};
    ctx_->EnumerateSettingsPages(visit, &pluginPass);
}

// ── Pages ─────────────────────────────────────────────────────────────────────

void SettingsMenu::RenderPageGeneral(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Window");
    dirty_ |= Widgets::SliderFloat(
        ctx, "Max display ratio", "Maximum fraction of the display the window may occupy (0.3 – 1.0).",
        settings_.maxDisplayRatio, 0.3f, 1.0f
    );
}

void SettingsMenu::RenderPageGraphics(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Renderer");

    // backend stores "vulkan" or "gl"; map to/from the combo index.
    const bool isVulkan = settings_.backend == "vulkan" || settings_.backend == "vk" || settings_.backend == "Vulkan";
    const char* const items[] = {"Vulkan", "OpenGL"};
    int idx = isVulkan ? 0 : 1;
    if (Widgets::Combo(
            ctx, "Backend",
            "Graphics API used for video + UI rendering. Vulkan is the default; it falls back to "
            "OpenGL automatically if no Vulkan device is available. Takes effect after a restart.",
            items, 2, idx
        ))
    {
        settings_.backend = idx == 0 ? "vulkan" : "gl";
        dirty_ = true;
    }
}

void SettingsMenu::RenderPagePlayback(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Video");
    dirty_ |= Widgets::Checkbox(
        ctx, "Hardware decoding",
        "Use GPU-accelerated decoding where available (auto-safe). "
        "Disable if you see visual glitches or playback errors.",
        settings_.hwdec
    );
    dirty_ |= Widgets::Checkbox(
        ctx, "High-precision seeking",
        "Seek to the exact requested frame instead of the nearest keyframe. "
        "Disable for faster seeking on large files.",
        settings_.hrSeek
    );
    dirty_ |= Widgets::Checkbox(
        ctx, "Sync video to display",
        "Resample audio to stay in sync with the display refresh rate (display-resample). "
        "Disable to use audio-driven sync instead.",
        settings_.videoSync
    );
    Widgets::SectionHeader(ctx, "Auto-load");
    dirty_ |= Widgets::Checkbox(
        ctx, "Subtitle files", "Automatically load external subtitle files found in the same directory.",
        settings_.subAutoLoad
    );
    dirty_ |= Widgets::Checkbox(
        ctx, "External audio files", "Automatically load external audio files found in the same directory.",
        settings_.audioFileAutoLoad
    );
}

void SettingsMenu::RenderPageSubtitles(UIContext& ctx)
{
    // Local helper: a "#RRGGBB" string field edited through the RGB color picker.
    const auto colorRow = [&](const char* label, const char* desc, std::string& field)
    {
        float rgb[3];
        if (!ThemeUtil::ParseHexColor(field.c_str(), rgb))
        {
            rgb[0] = rgb[1] = rgb[2] = 0.f;
        }
        if (Widgets::ColorEdit(ctx, label, desc, rgb))
        {
            char hex[8];
            ThemeUtil::FormatHexColor(rgb, hex);
            field = hex;
            dirty_ = true;
        }
    };

    Widgets::SectionHeader(ctx, "Appearance");
    dirty_ |= Widgets::Checkbox(
        ctx, "Override subtitle styling",
        "Apply the options below on top of each file's own subtitle style. "
        "When off, subtitles render exactly as authored.",
        settings_.overrideStyle
    );

    EnsureFontsQueried();
    // Font family combo (index 0 = keep the file's font). fontNames_[0] is the UI
    // default label; reuse the rest as candidate family names for libass.
    int familyIdx = 0;
    for (std::size_t i = 1; i < fontNames_.size(); ++i)
    {
        if (fontNames_[i] == settings_.fontFamily)
        {
            familyIdx = static_cast<int>(i);
            break;
        }
    }
    std::vector<const char*> families;
    families.reserve(fontNames_.size());
    families.push_back("Default (file's font)");
    for (std::size_t i = 1; i < fontNames_.size(); ++i)
    {
        families.push_back(fontNames_[i].c_str());
    }
    if (Widgets::Combo(
            ctx, "Font", "Font family for subtitles. Default keeps the font chosen by the file.",
            families.data(), static_cast<int>(families.size()), familyIdx
        ))
    {
        settings_.fontFamily = familyIdx == 0 ? "" : fontNames_[familyIdx];
        dirty_ = true;
    }

    dirty_ |= Widgets::SliderFloat(
        ctx, "Font size", "Multiplier applied to the file's subtitle size.", settings_.fontScale, 0.5f, 3.0f
    );

    colorRow("Text color", "Primary fill color of the subtitle glyphs.", settings_.textColor);
    colorRow("Outline color", "Color of the outline / border around glyphs.", settings_.outlineColor);

    Widgets::SectionHeader(ctx, "Edges & background");
    const char* const edgeItems[] = {"None", "Outline", "Drop shadow", "Opaque box"};
    int edgeIdx = std::clamp(settings_.edgeStyle, 0, 3);
    if (Widgets::Combo(ctx, "Edge style", "How glyphs are separated from the video.", edgeItems, 4, edgeIdx))
    {
        settings_.edgeStyle = edgeIdx;
        dirty_ = true;
    }
    dirty_ |= Widgets::SliderFloat(ctx, "Outline width", "Outline / box border thickness in pixels.",
                                   settings_.outlineWidth, 0.f, 6.f);
    dirty_ |= Widgets::SliderFloat(ctx, "Shadow depth", "Drop-shadow / box offset in pixels.", settings_.shadowDepth,
                                   0.f, 6.f);
    colorRow("Background color", "Drop-shadow / opaque-box color.", settings_.backColor);
    dirty_ |= Widgets::SliderFloat(ctx, "Background opacity", "Opacity of the shadow / box (0 = transparent).",
                                   settings_.backOpacity, 0.f, 1.f);

    Widgets::SectionHeader(ctx, "Layout");
    // Alignment combo maps the numpad value (1-9) plus a "file default" (0) entry.
    const char* const alignItems[] = {"File default", "Bottom left", "Bottom center", "Bottom right",
                                       "Middle left",  "Middle center", "Middle right",
                                       "Top left",     "Top center",    "Top right"};
    int alignIdx = (settings_.alignment >= 1 && settings_.alignment <= 9) ? settings_.alignment : 0;
    if (Widgets::Combo(ctx, "Alignment", "On-screen position of the subtitles.", alignItems, 10, alignIdx))
    {
        settings_.alignment = alignIdx; // 0 = file default, 1-9 = \an position
        dirty_ = true;
    }
    dirty_ |= Widgets::SliderFloat(ctx, "Line spacing", "Extra space between lines, pixels.", settings_.lineSpacing,
                                   0.f, 30.f);
    dirty_ |= Widgets::SliderFloat(ctx, "Letter spacing", "Extra space between glyphs, pixels.",
                                   settings_.letterSpacing, 0.f, 20.f);

    Widgets::SectionHeader(ctx, "Track selection");
    dirty_ |= Widgets::InputText(
        ctx, "Preferred language",
        "Auto-select a subtitle track in this language when loading a file (ISO 639 code, e.g. eng). "
        "Empty = no preference.",
        settings_.defaultLanguage
    );
    dirty_ |= Widgets::Checkbox(
        ctx, "Prefer forced subtitles", "When a file has a forced subtitle track, select it on load.",
        settings_.preferForced
    );

    ctx.Dummy({0.f, 6.f});
    ctx.TextDisabled("Style changes apply when you press Save; track selection applies to the next file opened.");
}

void SettingsMenu::RenderPageCache(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Read-ahead buffer");
    dirty_ |= Widgets::Checkbox(
        ctx, "Read-ahead enabled",
        "Prefetch upcoming demuxed packets to smooth playback and reduce stalls. "
        "Disable to fall back to a small fixed packet buffer.",
        settings_.readAheadEnabled
    );
    dirty_ |= Widgets::SliderInt(
        ctx, "Cache size",
        "Memory budget for the read-ahead buffer in MB, shared across audio/video/subtitle. "
        "Larger values prefetch further ahead at the cost of memory (default: 64).",
        settings_.readAheadSizeMB, 8, 512
    );
}

void SettingsMenu::RenderPageUI(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Side panel");
    dirty_ |= Widgets::SliderFloat(
        ctx, "Panel width", "Width of the side panel in pixels.", settings_.panelWidth, 160.f, 600.f
    );
    dirty_ |=
        Widgets::SliderFloat(ctx, "Slide speed", "How fast the panel slides in/out.", settings_.slideSpeed, 1.f, 50.f);
}

void SettingsMenu::EnsureFontsQueried()
{
    if (fontsQueried_ || !ctx_)
    {
        return;
    }
    fontsQueried_ = true;

    // Index 0 is always the bundled default (empty path = embedded Roboto).
    fontNames_.emplace_back("Default (Roboto)");
    fontPaths_.emplace_back("");

    struct Lists
    {
        std::vector<std::string>* names;
        std::vector<std::string>* paths;
    };

    Lists lists{&fontNames_, &fontPaths_};
    ctx_->EnumerateSystemFonts(
        [](const char* name, const char* path, void* ud)
        {
            auto& l = *static_cast<Lists*>(ud);
            l.names->emplace_back(name ? name : "");
            l.paths->emplace_back(path ? path : "");
        },
        &lists
    );
}

void SettingsMenu::RenderPageTheme(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Style");

    // Base preset.
    const char* const presetItems[] = {"Dark", "Light"};
    int presetIdx = ThemeUtil::PresetIndex(settings_.preset.c_str());
    if (Widgets::Combo(ctx, "Base style", "Built-in Dear ImGui color preset.", presetItems, 2, presetIdx))
    {
        settings_.preset = ThemeUtil::PresetNames[presetIdx];
        dirty_ = true;
    }

    // Accent color.
    float rgb[3];
    if (!ThemeUtil::ParseHexColor(settings_.accentColor.c_str(), rgb))
    {
        ThemeUtil::ParseHexColor("#4296FA", rgb);
    }
    if (Widgets::ColorEdit(ctx, "Accent color", "Tints buttons, sliders, headers and highlights.", rgb))
    {
        char hex[8];
        ThemeUtil::FormatHexColor(rgb, hex);
        settings_.accentColor = hex;
        dirty_ = true;
    }

    Widgets::SectionHeader(ctx, "Font");

    EnsureFontsQueried();

    // Resolve the current font's index by matching the stored path.
    int fontIdx = 0;
    for (std::size_t i = 0; i < fontPaths_.size(); ++i)
    {
        if (fontPaths_[i] == settings_.fontFile)
        {
            fontIdx = static_cast<int>(i);
            break;
        }
    }

    // Build a const char* view for the combo. If the stored font is no longer on
    // disk (no match, non-empty path), show its filename so it stays visible.
    std::vector<const char*> items;
    items.reserve(fontNames_.size() + 1);
    for (const auto& n : fontNames_)
    {
        items.push_back(n.c_str());
    }
    std::string staleName;
    if (fontIdx == 0 && !settings_.fontFile.empty())
    {
        staleName = settings_.fontFile.substr(settings_.fontFile.find_last_of("/\\") + 1);
        items.push_back(staleName.c_str());
        fontIdx = static_cast<int>(items.size()) - 1;
    }

    if (Widgets::Combo(
            ctx, "Font", "Installed system fonts (.ttf/.otf).", items.data(), static_cast<int>(items.size()), fontIdx
        ))
    {
        // The stale entry (if any) is the last item and maps to no path change.
        if (fontIdx < static_cast<int>(fontPaths_.size()))
        {
            settings_.fontFile = fontPaths_[fontIdx];
            dirty_ = true;
        }
    }

    dirty_ |= Widgets::SliderFloat(ctx, "Font size", "Glyph size in pixels.", settings_.fontSize, 10.f, 28.f);

    ctx.Dummy({0.f, 6.f});
    ctx.TextDisabled("Theme and font changes apply when you press Save.");
}

void SettingsMenu::RenderPageFiles(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "File extensions");
    dirty_ |=
        Widgets::InputText(ctx, "Video file extensions", "Semicolon-separated, no dots", settings_.videoExtensions);
    dirty_ |=
        Widgets::InputText(ctx, "Image file extensions", "Semicolon-separated, no dots", settings_.imageExtensions);
}

void SettingsMenu::RenderPageAudio(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Track selection");
    dirty_ |= Widgets::InputText(
        ctx, "Preferred language",
        "Auto-select an audio track in this language when loading a file (ISO 639 code, e.g. eng). "
        "Empty = use the file default.",
        settings_.defaultAudioLanguage
    );

    Widgets::SectionHeader(ctx, "Output");
    std::vector<std::string> deviceNames;
    std::vector<const char*> deviceItems;
    int deviceIdx = 0;
    deviceNames.emplace_back("System default");
    if (settings_.outputDevice.empty())
    {
        deviceIdx = 0;
    }
    if (auto* player = ctx_ ? ctx_->GetService<IMediaPlayer>() : nullptr)
    {
        struct Devices
        {
            std::vector<std::string>* names;
            std::string* selected;
            int* selectedIdx;
        };
        Devices devices{&deviceNames, &settings_.outputDevice, &deviceIdx};
        player->EnumerateAudioOutputDevices(
            [](const AudioOutputDevice* d, void* ud)
            {
                auto& state = *static_cast<Devices*>(ud);
                if (!d || d->isDefault || d->name[0] == '\0')
                {
                    return;
                }
                state.names->emplace_back(d->name);
                if (*state.selected == d->name)
                {
                    *state.selectedIdx = static_cast<int>(state.names->size()) - 1;
                }
            },
            &devices
        );
    }
    std::string staleDevice;
    if (deviceIdx == 0 && !settings_.outputDevice.empty())
    {
        staleDevice = settings_.outputDevice + " (missing)";
        deviceNames.push_back(staleDevice);
        deviceIdx = static_cast<int>(deviceNames.size()) - 1;
    }
    deviceItems.reserve(deviceNames.size());
    for (const auto& name : deviceNames)
    {
        deviceItems.push_back(name.c_str());
    }
    if (Widgets::Combo(
            ctx, "Output device", "Preferred playback device. Missing devices fall back to system default.",
            deviceItems.data(), static_cast<int>(deviceItems.size()), deviceIdx
        ))
    {
        settings_.outputDevice = deviceIdx == 0 ? "" : deviceNames[deviceIdx];
        const std::string suffix = " (missing)";
        if (settings_.outputDevice.ends_with(suffix))
        {
            settings_.outputDevice.erase(settings_.outputDevice.size() - suffix.size());
        }
        dirty_ = true;
    }
    dirty_ |= Widgets::SliderInt(ctx, "Default volume", "Playback volume applied on startup and Save.",
                                 settings_.defaultVolume, 0, 100);

    Widgets::SectionHeader(ctx, "Sync");
    dirty_ |= Widgets::SliderInt(
        ctx, "Audio offset", "Audio sync offset in ms. Positive values delay audio relative to video.",
        settings_.syncOffsetMs, -1000, 1000
    );

    Widgets::SectionHeader(ctx, "Channels");
    const char* const channelItems[] = {"Auto", "Mono", "Stereo", "Surround"};
    int channelIdx = std::clamp(settings_.channelMode, 0, 3);
    if (Widgets::Combo(ctx, "Channel mode", "Output channel layout preference.", channelItems, 4, channelIdx))
    {
        settings_.channelMode = channelIdx;
        dirty_ = true;
    }

    Widgets::SectionHeader(ctx, "Ducking");
    dirty_ |= Widgets::Checkbox(
        ctx, "Enable ducking", "Temporarily reduce playback volume while app notifications are active.",
        settings_.duckingEnabled
    );
    dirty_ |= Widgets::SliderInt(ctx, "Ducking level", "Playback gain while ducked, as percent of current volume.",
                                 settings_.duckingLevel, 0, 100);

    Widgets::SectionHeader(ctx, "Audio normalization");
    dirty_ |= Widgets::Checkbox(
        ctx, "Enable normalization", "Apply dynamic audio normalization by default when playback starts.",
        settings_.normalizeEnabled
    );
    dirty_ |= Widgets::SliderInt(
        ctx, "Frame length",
        "dynaudnorm f: analysis frame length in ms. "
        "Smaller values respond faster; larger values sound smoother (default: 100).",
        settings_.dynaudnormFrameLen, 50, 2000
    );
    dirty_ |= Widgets::SliderInt(
        ctx, "Gaussian window",
        "dynaudnorm g: size of the gaussian smoothing window in frames. "
        "Must be an odd number; even values are rounded up automatically (default: 5).",
        settings_.dynaudnormGaussSize, 3, 31
    );
    dirty_ |= Widgets::SliderFloat(
        ctx, "Target peak",
        "dynaudnorm p: target peak level (0.0–1.0). "
        "Lower for more headroom; raise for louder output (default: 0.95).",
        settings_.dynaudnormPeak, 0.1f, 1.0f
    );
    dirty_ |= Widgets::SliderFloat(
        ctx, "Max gain",
        "dynaudnorm m: maximum amplification factor per frame. "
        "Lower if output sounds squashed; raise for a stronger boost on quiet content (default: 5).",
        settings_.dynaudnormMaxGain, 1.f, 50.f
    );
    dirty_ |= Widgets::SliderFloat(
        ctx, "Output volume",
        "Output gain multiplier applied after normalization and soft clipping. "
        "1.0 = unity gain; raise to boost overall loudness (default: 1.5).",
        settings_.dynaudnormVolume, 0.1f, 5.f
    );
}

void SettingsMenu::RenderPageKeybinds(UIContext& ctx)
{
    // ── Core keybinds (Settings struct fields) ────────────────────────────────
    struct BindEntry
    {
        const char* label;
        const char* name;
        std::string Settings::* field;
    };

    static const BindEntry coreEntries[] = {
        {"Toggle pause", "togglePause", &Settings::togglePause},
        {"Toggle fullscreen", "toggleFullscreen", &Settings::toggleFullscreen},
        {"Quit", "quit", &Settings::quit},
        {"Volume up", "volumeUp", &Settings::volumeUp},
        {"Volume down", "volumeDown", &Settings::volumeDown},
        {"Toggle mute", "toggleMute", &Settings::toggleMute},
        {"Seek forward", "seekForward", &Settings::seekForward},
        {"Seek back", "seekBack", &Settings::seekBack},
        {"Seek forward (long)", "seekForwardLong", &Settings::seekForwardLong},
        {"Seek back (long)", "seekBackLong", &Settings::seekBackLong},
        {"Toggle normalize", "toggleNormalize", &Settings::toggleNormalize},
        {"Toggle subtitles", "toggleSubtitles", &Settings::toggleSubtitles},
        {"Open file dialog", "openFileDialog", &Settings::openFileDialog},
    };

    Widgets::SectionHeader(ctx, "Player");
    for (const auto& e : coreEntries)
    {
        const bool thisCapturing = isCapturing_ && capturingName_ == e.name;
        ctx.PushID(e.name);
        const auto action = Widgets::KeybindRow(ctx, e.label, settings_.*e.field, thisCapturing);
        ctx.PopID();

        switch (action)
        {
        case Widgets::KeybindAction::StartCapture:
            SetCapturing(true);
            capturingName_ = e.name;
            capturingBind_ = &(settings_.*e.field);
            break;
        case Widgets::KeybindAction::CancelCapture:
            SetCapturing(false);
            break;
        case Widgets::KeybindAction::Clear:
            settings_.*e.field = "";
            if (auto* hk = ctx_ ? ctx_->GetService<Hotkeys>() : nullptr)
            {
                hk->Unbind(e.name);
            }
            dirty_ = true;
            break;
        case Widgets::KeybindAction::None:
            break;
        }
    }

    // ── Plugin keybinds ───────────────────────────────────────────────────────
    if (ctx_)
    {
        struct KbCtx
        {
            SettingsMenu* self;
            UIContext* ctx;
            bool hadEntries = false;
        };

        KbCtx kc{this, &ctx};

        ctx_->EnumerateKeybindEntries(
            [](const char* label, const char* actionName, const char* (*getStr)(void*),
               void (*setStr)(void*, const char*), void* ud, void* pv)
            {
                auto& kc = *static_cast<KbCtx*>(pv);
                if (!kc.hadEntries)
                {
                    Widgets::SectionHeader(*kc.ctx, "Plugins");
                    kc.hadEntries = true;
                }

                const bool thisCapturing = kc.self->isCapturing_ && kc.self->capturingName_ == actionName;
                const std::string binding = getStr ? getStr(ud) : "";
                kc.ctx->PushID(actionName);
                const auto action = Widgets::KeybindRow(*kc.ctx, label, binding, thisCapturing);
                kc.ctx->PopID();

                switch (action)
                {
                case Widgets::KeybindAction::StartCapture:
                    kc.self->SetCapturing(true);
                    kc.self->capturingName_ = actionName;
                    kc.self->capturingGetStr_ = getStr;
                    kc.self->capturingSetStr_ = setStr;
                    kc.self->capturingUd_ = ud;
                    kc.self->dirty_ = true;
                    break;
                case Widgets::KeybindAction::CancelCapture:
                    kc.self->SetCapturing(false);
                    break;
                case Widgets::KeybindAction::Clear:
                    if (setStr)
                    {
                        setStr(ud, "");
                    }
                    if (auto* hk = kc.self->ctx_->GetService<Hotkeys>())
                    {
                        hk->Unbind(actionName);
                    }
                    kc.self->dirty_ = true;
                    break;
                case Widgets::KeybindAction::None:
                    break;
                }
            },
            &kc
        );
    }
}

void SettingsMenu::RenderPagePlugins(UIContext& ctx)
{
    if (!ctx_)
    {
        return;
    }

    ctx.TextDisabledWrapped("Enabling or disabling a plugin takes effect after restarting FrameLift.");
    ctx.Dummy({0.f, 6.f});

    // Walk the host's plugin catalogue and render one block per plugin. State
    // rides through the C callback via void* (no captures cross the ABI).
    struct PluginsCtx
    {
        SettingsMenu* self;
        UIContext* ctx;
        int count = 0;
    };

    PluginsCtx pc{this, &ctx};
    ctx_->EnumeratePlugins(
        [](const char* name, const FrameLiftPluginInfo& info, bool enabled, bool loaded, bool loadFailed, void* pv)
        {
            auto& [self, ctx, count] = *static_cast<PluginsCtx*>(pv);
            if (count++ > 0)
            {
                ctx->Dummy({0.f, 10.f});
            }

            const char* title = (loaded && info.name) ? info.name : name;
            Widgets::SectionHeader(*ctx, title);

            ctx->PushID(name);

            // SettingsMenu cannot disable itself — that would remove this very UI.
            if (strcmp(name, self->PluginName()) == 0)
            {
                ctx->TextDisabled("Enabled (required)");
            }
            else
            {
                bool en = enabled;
                if (ctx->Checkbox("Enabled", &en))
                {
                    self->ctx_->SetPluginEnabled(name, en);
                }
            }

            if (loaded)
            {
                char line[256];
                snprintf(line, sizeof(line), "Version %d.%d.%d", info.version[0], info.version[1], info.version[2]);
                if (info.publisher && info.publisher[0])
                {
                    const std::size_t n = strlen(line);
                    snprintf(line + n, sizeof(line) - n, "  -  %s", info.publisher);
                }
                ctx->TextDisabled(line);

                if (info.description && info.description[0])
                {
                    ctx->TextWrapped(info.description);
                }
            }
            else if (loadFailed)
            {
                ctx->TextDisabled("Failed to load - check the log.");
            }
            else
            {
                ctx->TextDisabled(enabled ? "Will load after restart." : "Disabled - enable and restart to load.");
            }

            ctx->PopID();
        },
        &pc
    );

    if (pc.count == 0)
    {
        ctx.TextDisabled("No plugins found.");
    }
}

void SettingsMenu::LoadConfigText()
{
    configText_.assign(ConfigBufCap, '\0');
    configTruncated_ = false;
    configLoaded_ = true;
    if (!ctx_)
    {
        configPath_.clear();
        return;
    }

    // Resolve the path the host actually persists to (settings.ini).
    const int n = ctx_->GetSettingsFilePath(nullptr, 0);
    std::string path(static_cast<std::size_t>(n), '\0');
    if (n > 0)
    {
        ctx_->GetSettingsFilePath(path.data(), n + 1);
    }
    configPath_ = std::move(path);

    // Slurp the file into the fixed-capacity buffer, leaving room for the NUL.
    std::ifstream f(configPath_, std::ios::binary);
    if (f)
    {
        f.read(configText_.data(), ConfigBufCap - 1);
        const auto got = static_cast<std::size_t>(f.gcount());
        configText_[got] = '\0';
        // If the buffer filled completely and bytes remain, the view is truncated.
        if (got == static_cast<std::size_t>(ConfigBufCap - 1))
        {
            configTruncated_ = f.peek() != std::char_traits<char>::eof();
        }
    }
}

void SettingsMenu::RenderPageConfig(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Configuration file");

    if (!configLoaded_)
    {
        LoadConfigText();
    }
    if (configPath_.empty())
    {
        ctx.TextDisabledWrapped("Settings file path is unavailable.");
        return;
    }

    ctx.TextDisabled(configPath_.c_str());
    ctx.TextDisabledWrapped(
        "Edit the raw settings.ini below. \"Save to disk\" applies core settings live; "
        "changes to plugin-owned sections take effect after a restart."
    );
    if (configTruncated_)
    {
        ctx.TextDisabledWrapped(
            "Warning: the file is larger than the 64 KB editor buffer. Saving is disabled "
            "to avoid losing the rest of the file."
        );
    }
    ctx.Dummy({0.f, 6.f});

    // Editor fills the page, leaving a row below for the action buttons.
    ctx.InputTextMultiline("##configraw", configText_.data(), static_cast<int>(configText_.size()), {0.f, -36.f});

    if (!configTruncated_)
    {
        if (ctx.Button("Save to disk", {120.f, 0.f}))
        {
            std::ofstream out(configPath_, std::ios::binary | std::ios::trunc);
            if (out)
            {
                out.write(configText_.data(), static_cast<std::streamsize>(std::strlen(configText_.data())));
                out.close();
                if (ctx_)
                {
                    ctx_->ReloadSettings(); // host re-reads the file and re-applies live
                    SeedFromContext(*ctx_); // refresh the typed pages from the new values
                }
                LoadConfigText(); // normalize the buffer to exactly what was persisted
            }
        }
        ctx.SameLine();
    }
    if (ctx.Button("Reload from disk", {130.f, 0.f}))
    {
        LoadConfigText();
    }
}

// ── Top-level render ──────────────────────────────────────────────────────────

void SettingsMenu::OnRender(UIContext& ctx)
{
    if (!open_)
    {
        return;
    }

    // Resolve the default page (General) on the first frame after Open().
    if (activePageIndex_ < 0 && ctx_)
    {
        struct F
        {
            const void* target;
            int cur = 0;
            int found = -1;
        };

        F f{&corePages_[0]};
        ctx_->EnumerateSettingsPages(
            [](const char*, void (*)(void*, UIContext&), void (*)(void*), void* ud, bool, void* pv)
            {
                auto& [target, cur, found] = *static_cast<F*>(pv);
                const int i = cur++;
                if (ud == target)
                {
                    found = i;
                }
            },
            &f
        );
        activePageIndex_ = f.found;
    }

    ctx.SetNextWindowPos({0.f, 0.f});
    ctx.SetNextWindowSize(ctx.GetMainWindowSize());

    // The dark purple chrome is designed for dark text-on-light contrast. Under a
    // light preset it would clash with the theme's dark text, so skip the
    // background overrides and let the themed (light) colors show through.
    const bool darkChrome = settings_.preset != "light";

    if (darkChrome)
    {
        ctx.PushStyleColor(UI::ColorSlot::WindowBg, UI::Color4f(0.12f, 0.10f, 0.16f, 1.f));
    }
    ctx.PushStyleVar(UI::StyleVar::WindowPadding, UI::Vec2(0.f, 0.f));
    ctx.PushStyleVar(UI::StyleVar::WindowRounding, 0.f);

    const UI::WindowFlags flags = UI::WindowFlags::NoResize | UI::WindowFlags::NoMove |
                                  UI::WindowFlags::NoSavedSettings | UI::WindowFlags::NoCollapse |
                                  UI::WindowFlags::NoTitleBar;

    if (ctx.Begin("##frameliftsettings", &open_, flags))
    {
        // ── Body: sidebar + content side-by-side ──────────────────────────────
        ctx.BeginChild("##settbody", {0.f, -BotH});
        {
            // Left sidebar (darker background under the dark chrome)
            if (darkChrome)
            {
                ctx.PushStyleColor(UI::ColorSlot::ChildBg, UI::Color4f(0.08f, 0.06f, 0.12f, 1.f));
            }
            ctx.PushStyleVar(UI::StyleVar::WindowPadding, UI::Vec2(8.f, 8.f));
            ctx.BeginChild("##settsidebar", {SideW, 0.f});
            RenderSidebar(ctx);
            ctx.EndChild();
            ctx.PopStyleVar(1);
            if (darkChrome)
            {
                ctx.PopStyleColor(1);
            }
        }
        ctx.SameLine();
        {
            // Right content pane
            ctx.PushStyleVar(UI::StyleVar::WindowPadding, UI::Vec2(14.f, 12.f));
            ctx.BeginChild("##settcontent", {0.f, 0.f});
            if (ctx_ && activePageIndex_ >= 0)
            {
                // Find and call the render function for the active page (core or plugin).
                struct RC
                {
                    UIContext* ctx;
                    int target;
                    int cur = 0;
                };

                RC rc{&ctx, activePageIndex_};
                ctx_->EnumerateSettingsPages(
                    [](const char*, void (*renderFn)(void*, UIContext&), void (*)(void*), void* ud, bool, void* pv)
                    {
                        auto& rc = *static_cast<RC*>(pv);
                        if (rc.cur++ == rc.target && renderFn)
                        {
                            renderFn(ud, *rc.ctx);
                        }
                    },
                    &rc
                );
            }
            ctx.EndChild();
            ctx.PopStyleVar(1);
        }
        ctx.EndChild(); // ##settbody

        // ── Bottom bar ────────────────────────────────────────────────────────
        {
            const UI::Vec2 sep = ctx.GetCursorScreenPos();
            auto& dl = ctx.GetWindowDrawList();
            dl.AddLine(sep, {sep.x + ctx.GetWindowWidth(), sep.y}, UI::MakeColor32(80, 55, 120, 200));
        }

        // Reset – left
        ctx.SetCursorPosX(8.f);
        if (ctx.Button("Reset", {70.f, 28.f}))
        {
            ctx.OpenPopup("##resetconfirm");
        }

        if (ctx.BeginPopupModal("##resetconfirm"))
        {
            ctx.Text("Reset settings to defaults?");
            ctx.Separator();
            if (ctx.Button("All settings", {200.f, 0.f}))
            {
                Reset();
                ctx.CloseCurrentPopup();
            }
            char pageBtn[32];
            snprintf(pageBtn, sizeof(pageBtn), "%s page only", PageName());
            if (ctx.Button(pageBtn, {200.f, 0.f}))
            {
                ResetPage();
                ctx.CloseCurrentPopup();
            }
            ctx.Separator();
            if (ctx.Button("Cancel", {200.f, 0.f}))
            {
                ctx.CloseCurrentPopup();
            }
            ctx.EndPopup();
        }

        // Save + Close – right
        ctx.SameLine();
        ctx.SetCursorPosX(ctx.GetWindowWidth() - 8.f - 70.f - 8.f - 70.f);
        if (ctx.Button(dirty_ ? "Save *" : "Save", {70.f, 28.f}))
        {
            Save();
        }
        ctx.SameLine();
        if (ctx.Button("Close", {70.f, 28.f}))
        {
            Close();
        }
    }
    ctx.End();

    ctx.PopStyleVar(2);
    if (darkChrome)
    {
        ctx.PopStyleColor(1);
    }
}

FRAMELIFT_PLUGIN_EXPORT(
    SettingsMenu, {
                      .name = "SettingsMenu",
                      .version = {FRAMELIFT_VERSION_MAJOR, FRAMELIFT_VERSION_MINOR, FRAMELIFT_VERSION_PATCH},
                      .renderOrder = 50,
                      .publisher = "FrameLift",
                      .description = "Settings and keybind editor",
                  }
)
