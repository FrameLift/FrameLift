#include "Theme.h"

#include "ThemeUtil.h"

#include "FontData.h"

#include "imgui.h"

#include <framelift/Log.h>

#include <algorithm>
#include <filesystem>

namespace
{
constexpr const char* kDefaultAccent = "#4296FA";

// Style colors that should pick up the accent hue. Each keeps its own preset
// brightness/alpha (see ThemeUtil::RetintColor), so this one list works across
// Dark, Light and Classic. Names are the v1.91.9b spellings.
constexpr ImGuiCol kAccentSlots[] = {
    ImGuiCol_CheckMark,
    ImGuiCol_SliderGrab,
    ImGuiCol_SliderGrabActive,
    ImGuiCol_Button,
    ImGuiCol_ButtonHovered,
    ImGuiCol_ButtonActive,
    ImGuiCol_FrameBgHovered,
    ImGuiCol_FrameBgActive,
    ImGuiCol_Header,
    ImGuiCol_HeaderHovered,
    ImGuiCol_HeaderActive,
    ImGuiCol_SeparatorHovered,
    ImGuiCol_SeparatorActive,
    ImGuiCol_ResizeGrip,
    ImGuiCol_ResizeGripHovered,
    ImGuiCol_ResizeGripActive,
    ImGuiCol_Tab,
    ImGuiCol_TabHovered,
    ImGuiCol_TabSelected,
    ImGuiCol_TabDimmed,
    ImGuiCol_TabDimmedSelected,
    ImGuiCol_TitleBgActive,
    ImGuiCol_TextSelectedBg,
    ImGuiCol_NavCursor,
};
} // namespace

void Theme::ApplyStyle(const ThemeSettings& s)
{
    if (ThemeUtil::PresetIndex(s.preset.c_str()) == 1)
    {
        ImGui::StyleColorsLight();
    }
    else
    {
        ImGui::StyleColorsDark();
    }

    float accent[3];
    if (!ThemeUtil::ParseHexColor(s.accentColor.c_str(), accent))
    {
        ThemeUtil::ParseHexColor(kDefaultAccent, accent);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    for (const ImGuiCol slot : kAccentSlots)
    {
        ImVec4& c = style.Colors[slot];
        float rgba[4] = {c.x, c.y, c.z, c.w};
        ThemeUtil::RetintColor(rgba, accent);
        c = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
    }
}

void Theme::RebuildFonts(const ThemeSettings& s)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float size = std::clamp(s.fontSize, 8.f, 64.f);
    ImFont* font = nullptr;

    if (!s.fontFile.empty())
    {
        std::error_code ec;
        // exists() pre-check avoids the debug-build IM_ASSERT inside
        // AddFontFromFileTTF when the path is missing.
        if (std::filesystem::exists(s.fontFile, ec) && !ec)
        {
            font = io.Fonts->AddFontFromFileTTF(s.fontFile.c_str(), size);
        }
        if (!font)
        {
            Log::Warn("Theme: could not load font '{}', falling back to the default", s.fontFile);
        }
    }

    if (!font)
    {
        // Bundled Roboto (assets/fonts/Roboto-Regular.ttf, embedded via FontData.h) —
        // a readable default that renders identically on Windows and Linux, replacing
        // ImGui's blocky ProggyClean. FontDataOwnedByAtlas=false: the buffer is static
        // constexpr memory, so ImGui must NOT free() it on atlas teardown.
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(kDefaultFontData), static_cast<int>(kDefaultFontDataSize), size, &cfg
        );
    }

    // ImGui 1.92+ manages font textures dynamically: the OpenGL3 backend declares
    // ImGuiBackendFlags_RendererHasTextures and re-uploads the atlas automatically
    // on the next frame. Clearing and re-adding fonts above marks the atlas dirty,
    // so no manual Build()/CreateFontsTexture() is needed (and Build() was removed).
}
