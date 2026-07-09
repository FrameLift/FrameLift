#pragma once

#include <string>
#include <vector>

// Built-in curated model catalogue. Each entry is a vision-language model that ships as
// TWO GGUF files — the model and its mmproj (vision projector) — both required. Models
// are never packaged with FrameLift; the settings page downloads them on demand into
// <exeDir>/models/ as "<id>.gguf" and "<id>.mmproj.gguf" (the names AITagger resolves).
// A sha256 of "" means "don't verify" (used for large entries we can't pin yet); a
// non-empty sha256 is checked after download. Users can also install a custom local
// GGUF pair outside this catalogue.
namespace aitagger
{

struct CatalogEntry
{
    std::string id;   // stable id, also the on-disk basename
    std::string name; // display name
    std::string quant;
    long long modelSize = 0; // bytes, for the UI (0 = unknown)
    std::string modelUrl;
    std::string modelSha256;
    long long mmprojSize = 0;
    std::string mmprojUrl;
    std::string mmprojSha256;
    bool recommended = false;
};

// The compiled-in catalogue.
[[nodiscard]] std::vector<CatalogEntry> BuiltinCatalog();

} // namespace aitagger
