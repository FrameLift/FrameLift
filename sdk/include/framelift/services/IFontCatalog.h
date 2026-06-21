#pragma once

// Installed-font discovery: enumerate .ttf/.otf fonts found in the OS font
// directories. The scan runs lazily on the first call and is cached for the
// session. A capability service — discover it with ctx.GetService<IFontCatalog>().
class IFontCatalog
{
public:
    static constexpr const char* InterfaceId = "framelift.IFontCatalog";
    virtual ~IFontCatalog() = default;

    //   name — display name derived from the filename.
    //   path — absolute font file path.
    // Both pointers are valid only for the duration of each visit() call.
    virtual void EnumerateSystemFonts(
        void (*visit)(const char* name, const char* path, void* visitUd), void* visitUd
    ) const noexcept = 0;
};
