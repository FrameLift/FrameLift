#pragma once

#include <filesystem>
#include <string>
#include <vector>

// ── System font discovery (no SDL/imgui — testable standalone) ────────────────
// Dependency-free scan of OS font directories for .ttf/.otf files.

struct FontEntry
{
    std::string name; // display name derived from the filename
    std::string path; // absolute font file path
};

// "DejaVuSans-Bold.ttf" -> "DejaVuSans Bold". Stem only; '-' and '_' -> ' '.
std::string FontDisplayName(const std::filesystem::path& file);

// Recursively scan each directory for .ttf/.otf (case-insensitive extensions),
// derive a display name per file, sort case-insensitively by name and dedup by
// name (first occurrence wins). Missing directories are skipped; never throws.
std::vector<FontEntry> ScanFontDirs(const std::vector<std::filesystem::path>& dirs);

// Per-OS font directories (Windows / Linux). Returns absolute paths; entries
// that do not exist are still returned (ScanFontDirs skips them harmlessly).
std::vector<std::filesystem::path> SystemFontDirs();