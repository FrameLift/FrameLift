#include "SettingsMenu.h"
#include "KeybindList.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "Version.h"
#include <IGraphicsBackend.h>
#include <ThemeUtil.h>
#include <framelift/core.h>
#include <framelift/platform/IAppWindow.h>
#include <framelift/platform/IMediaPlayer.h>
#include <framelift/ui.h>

#ifndef FRAMELIFT_MODULE_GRAPHICS_VULKAN
#define FRAMELIFT_MODULE_GRAPHICS_VULKAN 1
#endif

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

struct DecodeModeItem
{
    const char* label;
    const char* value;
    bool requiresNvidia = false;
};

#if defined(_WIN32)
constexpr DecodeModeItem kDecodeModes[] = {
    {"Off", "off", false},
    {"Auto", "auto", false},
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    {"Vulkan (zero-copy)", "vulkan-zero-copy", false},
    {"Vulkan", "vulkan", false},
#endif
    {"CUDA (zero-copy)", "cuda-zero-copy", true},
    {"CUDA", "cuda", true},
    {"D3D11VA", "d3d11va", false},
    {"DXVA2", "dxva2", false},
};
#else
constexpr DecodeModeItem kDecodeModes[] = {
    {"Off", "off", false},
    {"Auto", "auto", false},
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    {"Vulkan (zero-copy)", "vulkan-zero-copy", false},
    {"Vulkan", "vulkan", false},
#endif
    {"CUDA (zero-copy)", "cuda-zero-copy", true},
    {"CUDA", "cuda", true},
    {"VAAPI", "vaapi", false},
};
#endif

bool HasNvidiaAdapter(const IModuleContext* ctx)
{
    auto* surface = ctx ? ctx->GetService<IGraphicsSurface>() : nullptr;
    auto* backend = surface ? static_cast<IGraphicsBackend*>(surface->GetGraphicsBackend()) : nullptr;
    return backend && backend->HasNvidiaAdapter();
}

// ── Core keybind table (Settings → Keybinds "Player" section) ─────────────────
// At file scope so both RenderPageKeybinds and the conflict search can iterate it.
struct CoreBindEntry
{
    const char* label;
    const char* name; // Hotkeys action name
    const char* key;  // "keybinds.*" model field
};

constexpr CoreBindEntry kCoreKeybinds[] = {
    {"Toggle pause", "togglePause", "keybinds.togglePause"},
    {"Toggle fullscreen", "toggleFullscreen", "keybinds.toggleFullscreen"},
    {"Quit", "quit", "keybinds.quit"},
    {"Volume up", "volumeUp", "keybinds.volumeUp"},
    {"Volume down", "volumeDown", "keybinds.volumeDown"},
    {"Toggle mute", "toggleMute", "keybinds.toggleMute"},
    {"Seek forward", "seekForward", "keybinds.seekForward"},
    {"Seek back", "seekBack", "keybinds.seekBack"},
    {"Seek forward (long)", "seekForwardLong", "keybinds.seekForwardLong"},
    {"Seek back (long)", "seekBackLong", "keybinds.seekBackLong"},
    {"Toggle normalize", "toggleNormalize", "keybinds.toggleNormalize"},
    {"Toggle subtitles", "toggleSubtitles", "keybinds.toggleSubtitles"},
    {"Open file dialog", "openFileDialog", "keybinds.openFileDialog"},
};

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

std::vector<framelift::Keybind> SettingsMenu::Keybinds()
{
    return {
        {"Open settings", "openSettings", &openSettingsKey_, "Ctrl+Comma", [this]
         {
             Open();
         }}
    };
}

void SettingsMenu::SeedValue(IModuleContext& ctx, const FieldMeta& f)
{
    auto* store = ctx.GetService<ISettingsStore>();
    if (!store)
    {
        return;
    }
    // Pull one field's current host value into the editing model, by type tag.
    switch (f.type)
    {
    case 0: // bool
        model_.Bool(f.key) = store->GetSettingBool(f.key.c_str());
        break;
    case 1: // int
        model_.Int(f.key) = store->GetSettingInt(f.key.c_str());
        break;
    case 2: // float
        model_.Float(f.key) = store->GetSettingFloat(f.key.c_str());
        break;
    case 3: // string
    {
        const int n = store->GetSettingString(f.key.c_str(), nullptr, 0);
        std::string s(static_cast<std::size_t>(n), '\0');
        if (n > 0)
        {
            store->GetSettingString(f.key.c_str(), s.data(), n + 1);
        }
        model_.Str(f.key) = std::move(s);
        break;
    }
    default:
        break;
    }
}

void SettingsMenu::SeedFromContext(IModuleContext& ctx)
{
    // Discover every host settings field over the ABI (no Settings struct crosses
    // the boundary), then seed the editing model with the current values.
    fields_.clear();
    model_.Clear();

    struct SeedCtx
    {
        SettingsMenu* self;
        IModuleContext* ctx;
    };
    auto* registry = ctx.GetService<ISettingsRegistry>();
    if (!registry)
    {
        return;
    }
    SeedCtx sc{this, &ctx};
    registry->EnumerateSettings(
        [](const FrameLiftSettingDesc* d, void* ud)
        {
            auto& sc = *static_cast<SeedCtx*>(ud);
            FieldMeta meta{d->key ? d->key : "", d->type, d->defaultValue ? d->defaultValue : ""};
            sc.self->SeedValue(*sc.ctx, meta);
            sc.self->fields_.push_back(std::move(meta));
        },
        &sc
    );
    dirty_ = false;
}

void SettingsMenu::OnInstall(IModuleContext& ctx)
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

void SettingsMenu::RegisterCorePages(IModuleContext& ctx)
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

    // applyFn is null: core fields are committed wholesale in Save() by iterating
    // the ABI-discovered field list, so no per-page apply is needed.
    auto* registry = ctx.GetService<ISettingsRegistry>();
    if (!registry)
    {
        return;
    }
    for (auto& page : corePages_)
    {
        registry->RegisterSettingsPage(page.title, &CoreRenderThunk, nullptr, &page, true, nullptr);
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
    const std::string keyStr = bindBuf;

    // Reject keys already owned by a different action (conflict). The captured key
    // is added to the *current* action's list, so a key already in this action's
    // own list is a harmless duplicate handled below.
    const std::string owner = FindKeyOwnerLabel(keyStr, capturingName_.c_str());
    if (!owner.empty())
    {
        keybindConflict_ = "\"" + keyStr + "\" is already bound to " + owner;
        SetCapturing(false);
        return true;
    }

    const std::string cur = capturingBind_         ? *capturingBind_
                            : capturingGetStr_     ? capturingGetStr_(capturingUd_)
                                                   : std::string{};
    if (keybinds::Contains(cur, keyStr))
    {
        // Already bound to this same action (either slot) — nothing to do.
        keybindConflict_.clear();
        SetCapturing(false);
        return true;
    }

    const std::string next = keybinds::SetSlot(cur, capturingSlot_, keyStr);
    if (capturingBind_)
    {
        *capturingBind_ = next;
    }
    else if (capturingSetStr_)
    {
        capturingSetStr_(capturingUd_, next.c_str());
    }
    if (!capturingName_.empty())
    {
        if (auto* hk = ctx_ ? ctx_->GetService<Hotkeys>() : nullptr)
        {
            hk->RebindList(capturingName_.c_str(), next.c_str());
        }
    }
    keybindConflict_.clear();
    dirty_ = true;
    SetCapturing(false);
    return true;
}

std::string SettingsMenu::FindKeyOwnerLabel(const std::string& canonicalKey, const char* exceptAction)
{
    if (canonicalKey.empty())
    {
        return {};
    }
    const std::string except = exceptAction ? exceptAction : "";

    // Core keybinds — compare against the live edited model values.
    for (const auto& e : kCoreKeybinds)
    {
        if (e.name == except)
        {
            continue;
        }
        if (keybinds::Contains(model_.Str(e.key), canonicalKey))
        {
            return e.label;
        }
    }

    // Plugin keybinds — enumerate registered entries and check each one's list.
    struct SearchCtx
    {
        const std::string* key;
        const std::string* except;
        std::string found;
    };
    SearchCtx sc{&canonicalKey, &except, {}};
    if (auto* reg = SettingsReg())
    {
        reg->EnumerateKeybindEntries(
            [](const char* label, const char* actionName, const char* (*getStr)(void*),
               void (*)(void*, const char*), void* ud, void* pv)
            {
                auto& s = *static_cast<SearchCtx*>(pv);
                if (!s.found.empty() || (actionName && *s.except == actionName))
                {
                    return;
                }
                const std::string list = getStr ? getStr(ud) : "";
                if (keybinds::Contains(list, *s.key))
                {
                    s.found = label ? label : "";
                }
            },
            &sc
        );
    }
    return sc.found;
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
    keybindConflict_.clear();
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

// ── Internal helpers ─────────────────────────────────────────────────────────────

void SettingsMenu::Save()
{
    dirty_ = false;
    if (!ctx_)
    {
        return;
    }

    // Call all plugin applyFns first so plugin settings are written before SaveSettings().
    SettingsReg()->EnumerateSettingsPages(
        [](const char*, void (*)(void*, UIContext&), void (*applyFn)(void*), void* ud, bool, void*)
        {
            if (applyFn)
            {
                applyFn(ud);
            }
        },
        nullptr
    );

    // Commit each discovered field via the ABI-stable per-key API, by type tag.
    for (const auto& f : fields_)
    {
        switch (f.type)
        {
        case 0:
            SettingsStore()->CommitSettingBool(f.key.c_str(), model_.Bool(f.key));
            break;
        case 1:
            SettingsStore()->CommitSettingInt(f.key.c_str(), model_.Int(f.key));
            break;
        case 2:
            SettingsStore()->CommitSettingFloat(f.key.c_str(), model_.Float(f.key));
            break;
        case 3:
            SettingsStore()->CommitSettingString(f.key.c_str(), model_.Str(f.key).c_str());
            break;
        default:
            break;
        }
    }
    SettingsStore()->SaveSettings();
}

void SettingsMenu::ResetValue(const FieldMeta& f)
{
    switch (f.type)
    {
    case 0:
        model_.Bool(f.key) = (f.defaultValue == "1");
        break;
    case 1:
        try
        {
            model_.Int(f.key) = std::stoi(f.defaultValue);
        }
        catch (...)
        {
            model_.Int(f.key) = 0;
        }
        break;
    case 2:
        try
        {
            model_.Float(f.key) = std::stof(f.defaultValue);
        }
        catch (...)
        {
            model_.Float(f.key) = 0.f;
        }
        break;
    case 3:
        model_.Str(f.key) = f.defaultValue;
        break;
    default:
        break;
    }
}

void SettingsMenu::Reset()
{
    for (const auto& f : fields_)
    {
        ResetValue(f);
    }
    dirty_ = true;
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
    SettingsReg()->EnumerateSettingsPages(
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
    SettingsReg()->EnumerateSettingsPages(
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
    if (!sec0)
    {
        return;
    }

    // Reset every discovered field whose key is in this page's section.
    const std::string prefix = std::string(sec0) + ".";
    for (const auto& f : fields_)
    {
        if (f.key.compare(0, prefix.size(), prefix) == 0)
        {
            ResetValue(f);
        }
    }

    dirty_ = true;
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
    SettingsReg()->EnumerateSettingsPages(visit, &corePass);

    Pass pluginPass{this, &ctx, false, corePass.rendered};
    SettingsReg()->EnumerateSettingsPages(visit, &pluginPass);
}

// ── Pages ─────────────────────────────────────────────────────────────────────

void SettingsMenu::RenderPageGeneral(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Window");
    dirty_ |= Widgets::SliderFloat(
        ctx, "Max display ratio", "Maximum fraction of the display the window may occupy (0.3 – 1.0).",
        model_.Float("general.maxDisplayRatio"), 0.3f, 1.0f
    );
}

void SettingsMenu::RenderPageGraphics(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Renderer");

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    // backend stores "vulkan" or "gl"; map to/from the combo index.
    const bool isVulkan = model_.Str("graphics.backend") == "vulkan" || model_.Str("graphics.backend") == "vk" || model_.Str("graphics.backend") == "Vulkan";
    const char* const items[] = {"Vulkan", "OpenGL"};
    int idx = isVulkan ? 0 : 1;
    if (Widgets::Combo(
            ctx, "Backend",
            "Graphics API used for video + UI rendering. Vulkan is the default; it falls back to "
            "OpenGL automatically if no Vulkan device is available. Takes effect after a restart.",
            items, 2, idx
        ))
    {
        model_.Str("graphics.backend") = idx == 0 ? "vulkan" : "gl";
        dirty_ = true;
    }
#else
    const char* const items[] = {"OpenGL"};
    int idx = 0;
    if (model_.Str("graphics.backend") != "gl")
    {
        model_.Str("graphics.backend") = "gl";
        dirty_ = true;
    }
    if (Widgets::Combo(
            ctx, "Backend", "Graphics API used for video + UI rendering. Takes effect after a restart.", items, 1, idx
        ))
    {
        model_.Str("graphics.backend") = "gl";
        dirty_ = true;
    }
#endif
}

void SettingsMenu::RenderPagePlayback(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Video");
    const bool hasNvidia = HasNvidiaAdapter(ctx_);
    std::vector<const DecodeModeItem*> visibleModes;
    visibleModes.reserve(std::size(kDecodeModes));
    for (const auto& mode : kDecodeModes)
    {
        if (!mode.requiresNvidia || hasNvidia)
        {
            visibleModes.push_back(&mode);
        }
    }

    std::vector<const char*> labels;
    labels.reserve(visibleModes.size());
    int decodeIdx = 1; // Auto
    for (std::size_t i = 0; i < visibleModes.size(); ++i)
    {
        labels.push_back(visibleModes[i]->label);
        if (model_.Str("playback.hwdecMode") == visibleModes[i]->value)
        {
            decodeIdx = static_cast<int>(i);
        }
    }
    if (Widgets::Combo(
            ctx, "Video acceleration",
            "Hardware decode mode. Auto prefers GPU-resident paths, while explicit modes are useful for "
            "driver troubleshooting and performance tracing.",
            labels.data(), static_cast<int>(labels.size()), decodeIdx
        ))
    {
        model_.Str("playback.hwdecMode") = visibleModes[static_cast<std::size_t>(decodeIdx)]->value;
        model_.Bool("playback.hwdec") = model_.Str("playback.hwdecMode") != "off";
        dirty_ = true;
    }
    dirty_ |= Widgets::Checkbox(
        ctx, "High-precision seeking",
        "Seek to the exact requested frame instead of the nearest keyframe. "
        "Disable for faster seeking on large files.",
        model_.Bool("playback.hrSeek")
    );
    dirty_ |= Widgets::Checkbox(
        ctx, "Sync video to display",
        "Resample audio to stay in sync with the display refresh rate (display-resample). "
        "Disable to use audio-driven sync instead.",
        model_.Bool("playback.videoSync")
    );
    Widgets::SectionHeader(ctx, "Auto-load");
    dirty_ |= Widgets::Checkbox(
        ctx, "Subtitle files", "Automatically load external subtitle files found in the same directory.",
        model_.Bool("playback.subAutoLoad")
    );
    dirty_ |= Widgets::Checkbox(
        ctx, "External audio files", "Automatically load external audio files found in the same directory.",
        model_.Bool("playback.audioFileAutoLoad")
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
        model_.Bool("subtitles.overrideStyle")
    );

    EnsureFontsQueried();
    // Font family combo (index 0 = keep the file's font). fontNames_[0] is the UI
    // default label; reuse the rest as candidate family names for libass.
    int familyIdx = 0;
    for (std::size_t i = 1; i < fontNames_.size(); ++i)
    {
        if (fontNames_[i] == model_.Str("subtitles.fontFamily"))
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
        model_.Str("subtitles.fontFamily") = familyIdx == 0 ? "" : fontNames_[familyIdx];
        dirty_ = true;
    }

    dirty_ |= Widgets::SliderFloat(
        ctx, "Font size", "Multiplier applied to the file's subtitle size.", model_.Float("subtitles.fontScale"), 0.5f, 3.0f
    );

    colorRow("Text color", "Primary fill color of the subtitle glyphs.", model_.Str("subtitles.textColor"));
    colorRow("Outline color", "Color of the outline / border around glyphs.", model_.Str("subtitles.outlineColor"));

    Widgets::SectionHeader(ctx, "Edges & background");
    const char* const edgeItems[] = {"None", "Outline", "Drop shadow", "Opaque box"};
    int edgeIdx = std::clamp(model_.Int("subtitles.edgeStyle"), 0, 3);
    if (Widgets::Combo(ctx, "Edge style", "How glyphs are separated from the video.", edgeItems, 4, edgeIdx))
    {
        model_.Int("subtitles.edgeStyle") = edgeIdx;
        dirty_ = true;
    }
    dirty_ |= Widgets::SliderFloat(ctx, "Outline width", "Outline / box border thickness in pixels.",
                                   model_.Float("subtitles.outlineWidth"), 0.f, 6.f);
    dirty_ |= Widgets::SliderFloat(ctx, "Shadow depth", "Drop-shadow / box offset in pixels.", model_.Float("subtitles.shadowDepth"),
                                   0.f, 6.f);
    colorRow("Background color", "Drop-shadow / opaque-box color.", model_.Str("subtitles.backColor"));
    dirty_ |= Widgets::SliderFloat(ctx, "Background opacity", "Opacity of the shadow / box (0 = transparent).",
                                   model_.Float("subtitles.backOpacity"), 0.f, 1.f);

    Widgets::SectionHeader(ctx, "Layout");
    // Alignment combo maps the numpad value (1-9) plus a "file default" (0) entry.
    const char* const alignItems[] = {"File default", "Bottom left", "Bottom center", "Bottom right",
                                       "Middle left",  "Middle center", "Middle right",
                                       "Top left",     "Top center",    "Top right"};
    int alignIdx = (model_.Int("subtitles.alignment") >= 1 && model_.Int("subtitles.alignment") <= 9) ? model_.Int("subtitles.alignment") : 0;
    if (Widgets::Combo(ctx, "Alignment", "On-screen position of the subtitles.", alignItems, 10, alignIdx))
    {
        model_.Int("subtitles.alignment") = alignIdx; // 0 = file default, 1-9 = \an position
        dirty_ = true;
    }
    dirty_ |= Widgets::SliderFloat(ctx, "Line spacing", "Extra space between lines, pixels.", model_.Float("subtitles.lineSpacing"),
                                   0.f, 30.f);
    dirty_ |= Widgets::SliderFloat(ctx, "Letter spacing", "Extra space between glyphs, pixels.",
                                   model_.Float("subtitles.letterSpacing"), 0.f, 20.f);

    Widgets::SectionHeader(ctx, "Track selection");
    dirty_ |= Widgets::InputText(
        ctx, "Preferred language",
        "Auto-select a subtitle track in this language when loading a file (ISO 639 code, e.g. eng). "
        "Empty = no preference.",
        model_.Str("subtitles.defaultLanguage")
    );
    dirty_ |= Widgets::Checkbox(
        ctx, "Prefer forced subtitles", "When a file has a forced subtitle track, select it on load.",
        model_.Bool("subtitles.preferForced")
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
        model_.Bool("cache.readAheadEnabled")
    );
    dirty_ |= Widgets::SliderInt(
        ctx, "Cache size",
        "Memory budget for the read-ahead buffer in MB, shared across audio/video/subtitle. "
        "Larger values prefetch further ahead at the cost of memory (default: 64).",
        model_.Int("cache.readAheadSizeMB"), 8, 512
    );
}

void SettingsMenu::RenderPageUI(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Side panel");
    dirty_ |= Widgets::SliderFloat(
        ctx, "Panel width", "Width of the side panel in pixels.", model_.Float("ui.panelWidth"), 160.f, 600.f
    );
    dirty_ |=
        Widgets::SliderFloat(ctx, "Slide speed", "How fast the panel slides in/out.", model_.Float("ui.slideSpeed"), 1.f, 50.f);
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
    FontCatalog()->EnumerateSystemFonts(
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
    int presetIdx = ThemeUtil::PresetIndex(model_.Str("theme.preset").c_str());
    if (Widgets::Combo(ctx, "Base style", "Built-in Dear ImGui color preset.", presetItems, 2, presetIdx))
    {
        model_.Str("theme.preset") = ThemeUtil::PresetNames[presetIdx];
        dirty_ = true;
    }

    // Accent color.
    float rgb[3];
    if (!ThemeUtil::ParseHexColor(model_.Str("theme.accentColor").c_str(), rgb))
    {
        ThemeUtil::ParseHexColor("#4296FA", rgb);
    }
    if (Widgets::ColorEdit(ctx, "Accent color", "Tints buttons, sliders, headers and highlights.", rgb))
    {
        char hex[8];
        ThemeUtil::FormatHexColor(rgb, hex);
        model_.Str("theme.accentColor") = hex;
        dirty_ = true;
    }

    Widgets::SectionHeader(ctx, "Font");

    EnsureFontsQueried();

    // Resolve the current font's index by matching the stored path.
    int fontIdx = 0;
    for (std::size_t i = 0; i < fontPaths_.size(); ++i)
    {
        if (fontPaths_[i] == model_.Str("theme.fontFile"))
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
    if (fontIdx == 0 && !model_.Str("theme.fontFile").empty())
    {
        staleName = model_.Str("theme.fontFile").substr(model_.Str("theme.fontFile").find_last_of("/\\") + 1);
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
            model_.Str("theme.fontFile") = fontPaths_[fontIdx];
            dirty_ = true;
        }
    }

    dirty_ |= Widgets::SliderFloat(ctx, "Font size", "Glyph size in pixels.", model_.Float("theme.fontSize"), 10.f, 28.f);

    ctx.Dummy({0.f, 6.f});
    ctx.TextDisabled("Theme and font changes apply when you press Save.");
}

void SettingsMenu::RenderPageFiles(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "File extensions");
    dirty_ |=
        Widgets::InputText(ctx, "Video file extensions", "Semicolon-separated, no dots", model_.Str("files.videoExtensions"));
    dirty_ |=
        Widgets::InputText(ctx, "Image file extensions", "Semicolon-separated, no dots", model_.Str("files.imageExtensions"));
}

void SettingsMenu::RenderPageAudio(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Track selection");
    dirty_ |= Widgets::InputText(
        ctx, "Preferred language",
        "Auto-select an audio track in this language when loading a file (ISO 639 code, e.g. eng). "
        "Empty = use the file default.",
        model_.Str("audio.defaultLanguage")
    );

    Widgets::SectionHeader(ctx, "Output");
    std::vector<std::string> deviceNames;
    std::vector<const char*> deviceItems;
    int deviceIdx = 0;
    deviceNames.emplace_back("System default");
    if (model_.Str("audio.outputDevice").empty())
    {
        deviceIdx = 0;
    }
    if (auto* player = ctx_ ? ctx_->GetService<IAudioControl>() : nullptr)
    {
        struct Devices
        {
            std::vector<std::string>* names;
            std::string* selected;
            int* selectedIdx;
        };
        Devices devices{&deviceNames, &model_.Str("audio.outputDevice"), &deviceIdx};
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
    if (deviceIdx == 0 && !model_.Str("audio.outputDevice").empty())
    {
        staleDevice = model_.Str("audio.outputDevice") + " (missing)";
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
        model_.Str("audio.outputDevice") = deviceIdx == 0 ? "" : deviceNames[deviceIdx];
        const std::string suffix = " (missing)";
        if (model_.Str("audio.outputDevice").ends_with(suffix))
        {
            model_.Str("audio.outputDevice").erase(model_.Str("audio.outputDevice").size() - suffix.size());
        }
        dirty_ = true;
    }
    dirty_ |= Widgets::SliderInt(ctx, "Default volume", "Playback volume applied on startup and Save.",
                                 model_.Int("audio.defaultVolume"), 0, 100);

    Widgets::SectionHeader(ctx, "Sync");
    dirty_ |= Widgets::SliderInt(
        ctx, "Audio offset", "Audio sync offset in ms. Positive values delay audio relative to video.",
        model_.Int("audio.syncOffsetMs"), -1000, 1000
    );

    Widgets::SectionHeader(ctx, "Channels");
    const char* const channelItems[] = {"Auto", "Mono", "Stereo", "Surround"};
    int channelIdx = std::clamp(model_.Int("audio.channelMode"), 0, 3);
    if (Widgets::Combo(ctx, "Channel mode", "Output channel layout preference.", channelItems, 4, channelIdx))
    {
        model_.Int("audio.channelMode") = channelIdx;
        dirty_ = true;
    }

    Widgets::SectionHeader(ctx, "Ducking");
    dirty_ |= Widgets::Checkbox(
        ctx, "Enable ducking", "Temporarily reduce playback volume while app notifications are active.",
        model_.Bool("audio.duckingEnabled")
    );
    dirty_ |= Widgets::SliderInt(ctx, "Ducking level", "Playback gain while ducked, as percent of current volume.",
                                 model_.Int("audio.duckingLevel"), 0, 100);

    Widgets::SectionHeader(ctx, "Audio normalization");
    dirty_ |= Widgets::Checkbox(
        ctx, "Enable normalization", "Apply dynamic audio normalization by default when playback starts.",
        model_.Bool("audio.normalizeEnabled")
    );
    dirty_ |= Widgets::SliderInt(
        ctx, "Frame length",
        "dynaudnorm f: analysis frame length in ms. "
        "Smaller values respond faster; larger values sound smoother (default: 100).",
        model_.Int("audio.dynaudnormFrameLen"), 50, 2000
    );
    dirty_ |= Widgets::SliderInt(
        ctx, "Gaussian window",
        "dynaudnorm g: size of the gaussian smoothing window in frames. "
        "Must be an odd number; even values are rounded up automatically (default: 5).",
        model_.Int("audio.dynaudnormGaussSize"), 3, 31
    );
    dirty_ |= Widgets::SliderFloat(
        ctx, "Target peak",
        "dynaudnorm p: target peak level (0.0–1.0). "
        "Lower for more headroom; raise for louder output (default: 0.95).",
        model_.Float("audio.dynaudnormPeak"), 0.1f, 1.0f
    );
    dirty_ |= Widgets::SliderFloat(
        ctx, "Max gain",
        "dynaudnorm m: maximum amplification factor per frame. "
        "Lower if output sounds squashed; raise for a stronger boost on quiet content (default: 5).",
        model_.Float("audio.dynaudnormMaxGain"), 1.f, 50.f
    );
    dirty_ |= Widgets::SliderFloat(
        ctx, "Output volume",
        "Output gain multiplier applied after normalization and soft clipping. "
        "1.0 = unity gain; raise to boost overall loudness (default: 1.5).",
        model_.Float("audio.dynaudnormVolume"), 0.1f, 5.f
    );
}

void SettingsMenu::RenderPageKeybinds(UIContext& ctx)
{
    if (!keybindConflict_.empty())
    {
        ctx.TextColored(UI::Color4f(1.f, 0.45f, 0.4f, 1.f), keybindConflict_.c_str());
    }

    // ── Core keybinds (host "keybinds.*" fields) ──────────────────────────────
    // Each action shows up to two key slots (primary + alternate). The alternate
    // row only appears once a primary key is set.
    Widgets::SectionHeader(ctx, "Player");
    for (const auto& e : kCoreKeybinds)
    {
        auto* hk = ctx_ ? ctx_->GetService<Hotkeys>() : nullptr;
        for (int slot = 0; slot < 2; ++slot)
        {
            std::string& list = model_.Str(e.key);
            if (slot == 1 && keybinds::Slot(list, 0).empty())
            {
                break;
            }
            const bool thisCapturing = isCapturing_ && capturingName_ == e.name && capturingSlot_ == slot;
            ctx.PushID(e.name);
            ctx.PushID(slot);
            const auto action = Widgets::KeybindRow(ctx, slot == 0 ? e.label : "", keybinds::Slot(list, slot), thisCapturing);
            ctx.PopID();
            ctx.PopID();

            switch (action)
            {
            case Widgets::KeybindAction::StartCapture:
                SetCapturing(true);
                capturingName_ = e.name;
                capturingSlot_ = slot;
                capturingBind_ = &model_.Str(e.key);
                keybindConflict_.clear();
                break;
            case Widgets::KeybindAction::CancelCapture:
                SetCapturing(false);
                break;
            case Widgets::KeybindAction::Clear:
                list = keybinds::SetSlot(list, slot, "");
                if (hk)
                {
                    list.empty() ? hk->Unbind(e.name) : hk->RebindList(e.name, list.c_str());
                }
                dirty_ = true;
                break;
            case Widgets::KeybindAction::None:
                break;
            }
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

        SettingsReg()->EnumerateKeybindEntries(
            [](const char* label, const char* actionName, const char* (*getStr)(void*),
               void (*setStr)(void*, const char*), void* ud, void* pv)
            {
                auto& kc = *static_cast<KbCtx*>(pv);
                if (!kc.hadEntries)
                {
                    Widgets::SectionHeader(*kc.ctx, "Plugins");
                    kc.hadEntries = true;
                }

                auto* hk = kc.self->ctx_->GetService<Hotkeys>();
                for (int slot = 0; slot < 2; ++slot)
                {
                    const std::string list = getStr ? getStr(ud) : "";
                    if (slot == 1 && keybinds::Slot(list, 0).empty())
                    {
                        break;
                    }
                    const bool thisCapturing = kc.self->isCapturing_ && kc.self->capturingName_ == actionName &&
                                               kc.self->capturingSlot_ == slot;
                    kc.ctx->PushID(actionName);
                    kc.ctx->PushID(slot);
                    const auto action =
                        Widgets::KeybindRow(*kc.ctx, slot == 0 ? label : "", keybinds::Slot(list, slot), thisCapturing);
                    kc.ctx->PopID();
                    kc.ctx->PopID();

                    switch (action)
                    {
                    case Widgets::KeybindAction::StartCapture:
                        kc.self->SetCapturing(true);
                        kc.self->capturingName_ = actionName;
                        kc.self->capturingSlot_ = slot;
                        kc.self->capturingGetStr_ = getStr;
                        kc.self->capturingSetStr_ = setStr;
                        kc.self->capturingUd_ = ud;
                        kc.self->keybindConflict_.clear();
                        break;
                    case Widgets::KeybindAction::CancelCapture:
                        kc.self->SetCapturing(false);
                        break;
                    case Widgets::KeybindAction::Clear:
                    {
                        const std::string next = keybinds::SetSlot(getStr ? getStr(ud) : "", slot, "");
                        if (setStr)
                        {
                            setStr(ud, next.c_str());
                        }
                        if (hk)
                        {
                            next.empty() ? hk->Unbind(actionName) : hk->RebindList(actionName, next.c_str());
                        }
                        kc.self->dirty_ = true;
                        break;
                    }
                    case Widgets::KeybindAction::None:
                        break;
                    }
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

    IPackageCatalog* const catalog = PackageCatalog();
    if (!catalog)
    {
        return;
    }

    ctx.TextDisabledWrapped("Enabling or disabling a module takes effect after restarting FrameLift.");
    ctx.Dummy({0.f, 6.f});

    // Collect the catalogue into host-side structures (STL never crosses the ABI; the
    // C callbacks copy every string they need before the visitor call returns), then
    // render one block per package with its modules nested underneath.
    struct ModuleRow
    {
        std::string id;
        std::string name;
        std::string description;
        bool enabled;
        bool loaded;
        bool loadFailed;
    };
    struct PackageRow
    {
        std::string id;
        std::string displayName;
        std::string publisher;
        std::string description;
        int version[3];
        bool loaded;
        std::vector<ModuleRow> modules;
    };
    std::vector<PackageRow> packages;

    catalog->EnumeratePackages(
        [](const char* packageId, const char* displayName, const int* version, const char* publisher,
           const char* description, bool loaded, void* ud)
        {
            auto& out = *static_cast<std::vector<PackageRow>*>(ud);
            out.push_back(
                {packageId, displayName ? displayName : packageId, publisher ? publisher : "",
                 description ? description : "", {version[0], version[1], version[2]}, loaded, {}}
            );
        },
        &packages
    );

    catalog->EnumerateModules(
        [](const char* packageId, const char* moduleId, const char* moduleName, const char* description, bool enabled,
           bool loaded, bool loadFailed, void* ud)
        {
            auto& pkgs = *static_cast<std::vector<PackageRow>*>(ud);
            for (auto& p : pkgs)
            {
                if (p.id == packageId)
                {
                    p.modules.push_back(
                        {moduleId, moduleName ? moduleName : moduleId, description ? description : "", enabled, loaded,
                         loadFailed}
                    );
                    break;
                }
            }
        },
        &packages
    );

    if (packages.empty())
    {
        ctx.TextDisabled("No plugins found.");
        return;
    }

    int blockIdx = 0;
    for (const auto& pkg : packages)
    {
        if (blockIdx++ > 0)
        {
            ctx.Dummy({0.f, 10.f});
        }

        Widgets::SectionHeader(ctx, pkg.displayName.c_str());

        if (pkg.loaded)
        {
            char line[256];
            snprintf(line, sizeof(line), "Version %d.%d.%d", pkg.version[0], pkg.version[1], pkg.version[2]);
            if (!pkg.publisher.empty())
            {
                const std::size_t n = strlen(line);
                snprintf(line + n, sizeof(line) - n, "  -  %s", pkg.publisher.c_str());
            }
            ctx.TextDisabled(line);
        }
        if (!pkg.description.empty())
        {
            ctx.TextWrapped(pkg.description.c_str());
        }

        // The SettingsMenu package can't disable its own modules — that would remove
        // this very UI.
        const bool isSelfPackage = pkg.id == "framelift.settings_menu";
        const bool labelModules = pkg.modules.size() > 1;

        ctx.PushID(pkg.id.c_str());
        for (const auto& mod : pkg.modules)
        {
            ctx.PushID(mod.id.c_str());

            // For a multi-module package, name each module above its toggle so the
            // checkboxes are distinguishable; a single-module package needs no label.
            if (labelModules)
            {
                ctx.TextDisabled(mod.name.c_str());
            }

            if (isSelfPackage)
            {
                ctx.TextDisabled("Enabled (required)");
            }
            else
            {
                bool en = mod.enabled;
                if (ctx.Checkbox("Enabled", &en))
                {
                    catalog->SetModuleEnabled(mod.id.c_str(), en);
                }
            }

            if (mod.loaded)
            {
                if (!mod.description.empty())
                {
                    ctx.TextWrapped(mod.description.c_str());
                }
            }
            else if (mod.loadFailed)
            {
                ctx.TextDisabled("Failed to load - check the log.");
            }
            else
            {
                ctx.TextDisabled(mod.enabled ? "Will load after restart." : "Disabled - enable and restart to load.");
            }

            ctx.PopID();
        }
        ctx.PopID();
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
    const int n = SettingsStore()->GetSettingsFilePath(nullptr, 0);
    std::string path(static_cast<std::size_t>(n), '\0');
    if (n > 0)
    {
        SettingsStore()->GetSettingsFilePath(path.data(), n + 1);
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
                    SettingsStore()->ReloadSettings(); // host re-reads the file and re-applies live
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
        SettingsReg()->EnumerateSettingsPages(
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
    const bool darkChrome = model_.Str("theme.preset") != "light";

    const framelift::ScopedWindow window(
        ctx, "##frameliftsettings", framelift::WindowPreset::Fullscreen, &open_,
        {.rounding = 0.f, .padding = {0.f, 0.f}, .hasBg = darkChrome, .bg = {0.12f, 0.10f, 0.16f, 1.f}}
    );
    if (window)
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
                SettingsReg()->EnumerateSettingsPages(
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
}
