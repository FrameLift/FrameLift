#pragma once

class ContextMenu;

// Additive context-menu capability for plugin-contributed submenus. Kept separate
// from ContextMenu so submenu support does not extend that existing service's
// vtable. Builders receive the familiar flat ContextMenu surface and populate the
// submenu's children when the context menu assembles its registered sections.
class IContextMenuSubmenuRegistry
{
public:
    static constexpr const char* InterfaceId = "framelift.IContextMenuSubmenuRegistry";
    virtual ~IContextMenuSubmenuRegistry() = default;

    virtual void AddSubmenuSectionRaw(
        const char* label, void (*builder)(ContextMenu& menu, void* ud), void* ud, void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;
};
