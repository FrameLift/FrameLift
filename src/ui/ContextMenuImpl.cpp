#include "ContextMenuImpl.h"
#include <cstring>
#include <framelift/Hotkeys.h>
#include <framelift/ui/UIContext.h>
#include <utility>
#include <vector>

void ContextMenuImpl::AddItemRaw(const char* label, void (*action)(void*), void* ud, void (*cleanup)(void*)) noexcept
{
    items_.push_back({label ? label : "", {}, action, ud, cleanup});
}

void ContextMenuImpl::AddItemWithHotkeyRaw(
    const char* label, const char* hotkey, void (*action)(void*), void* ud, void (*cleanup)(void*)
) noexcept
{
    items_.push_back({label ? label : "", hotkey ? hotkey : "", action, ud, cleanup});
}

void ContextMenuImpl::AddSeparator() noexcept
{
    items_.push_back({}); // empty label = separator
}

void ContextMenuImpl::AddDynamicSubMenuRaw(
    const char* label, void (*builder)(void*, UIContext&), void* ud, void (*cleanup)(void*)
) noexcept
{
    Item item;
    item.label = label ? label : "";
    item.dynamicBuilder = builder;
    item.builderUd = ud;
    item.cleanup = cleanup;
    items_.push_back(std::move(item));
}

void ContextMenuImpl::AddSectionRaw(void (*builder)(ContextMenu&, void*), void* ud, void (*cleanup)(void*)) noexcept
{
    sections_.push_back({builder, ud, cleanup});
}

void ContextMenuImpl::EmitSections()
{
    for (auto& section : sections_)
    {
        if (section.builder)
        {
            section.builder(*this, section.ud);
        }
    }
}

void ContextMenuImpl::Clear() noexcept
{
    for (auto& item : items_)
    {
        if (item.cleanup)
        {
            item.cleanup(item.ud);
        }
    }
    items_.clear();

    for (auto& section : sections_)
    {
        if (section.cleanup)
        {
            section.cleanup(section.ud);
        }
    }
    sections_.clear();
}

void ContextMenuImpl::RenderItems(UIContext& ctx, std::vector<Item>& items)
{
    for (auto& item : items)
    {
        if (item.label.empty())
        {
            ctx.Separator();
        }
        else if (item.dynamicBuilder)
        {
            if (ctx.BeginMenu(item.label.c_str()))
            {
                item.dynamicBuilder(item.builderUd, ctx);
                ctx.EndMenu();
            }
        }
        else if (!item.children.empty())
        {
            if (ctx.BeginMenu(item.label.c_str()))
            {
                RenderItems(ctx, item.children);
                ctx.EndMenu();
            }
        }
        else
        {
            const char* sc = nullptr;
            char scBuf[64] = {};
            if (keys_ && !item.hotkeyName.empty())
            {
                if (keys_->GetShortcutString(item.hotkeyName.c_str(), scBuf, sizeof(scBuf)) > 0)
                {
                    sc = scBuf;
                }
            }
            if (ctx.MenuItem(item.label.c_str(), sc) && item.action)
            {
                item.action(item.ud);
            }
        }
    }
}

void ContextMenuImpl::Render(int /*w*/, int /*h*/, UIContext& ctx) noexcept
{
    if (ctx.BeginPopupContextVoid("##main_ctx"))
    {
        menuOpen_ = true;
        RenderItems(ctx, items_);
        ctx.EndPopup();
    }
    else
    {
        menuOpen_ = false;
    }
}