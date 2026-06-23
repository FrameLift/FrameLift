#pragma once

#include <framelift/ui/UI.h>
#include <framelift/ui/UIContext.h>

// ── ListView ──────────────────────────────────────────────────────────────────
// Immediate-mode scrollable list with fixed-height rows, a selection highlight, an
// optional secondary highlight (e.g. the now-playing row), click-to-select, per-row
// separators and an empty-state message.
//
// It owns no item data and no selection state: the caller passes the row `count`
// and a row drawer each frame, and keeps its own selection index. Configure with
// the fluent setters, then call Render(); it returns the row index clicked this
// frame (or -1) so the caller updates its own selection. Construct it as a local
// each frame — there is nothing to retain between frames.
//
//     int clicked = framelift::ListView("##items", 44.f)
//                       .Selected(cursor_).Highlighted(current_)
//                       .Render(ctx, panelW, count,
//                               [&](UIContext& c, const framelift::ListRow& row) {
//                                   row.TextLine(c, 6.f, nameCol, items[row.index].label);
//                                   row.TextLine(c, 24.f, pathCol, items[row.index].path);
//                               });
//     if (clicked >= 0) { cursor_ = clicked; Activate(clicked); }

namespace framelift
{
// Per-row state handed to the row drawer.
struct ListRow
{
    int index = 0;            // 0-based row index
    bool selected = false;    // matches ListView::Selected()
    bool highlighted = false; // matches ListView::Highlighted()
    UI::Vec2 min{};           // screen-space top-left of the row
    float width = 0.f;        // row width (== list width)
    float height = 0.f;       // row height
    float localTop = 0.f;     // window-local Y of the row top (for SetCursorPosY)
    float padding = 0.f;      // horizontal text inset (from ListView::Padding)

    // Lay out one stacked text line at (padding, localTop + yOffset).
    void TextLine(UIContext& ctx, const float yOffset, const UI::Color4f col, const char* text) const
    {
        ctx.SetCursorPosX(padding);
        ctx.SetCursorPosY(localTop + yOffset);
        ctx.TextColored(col, text);
    }
};

class ListView
{
  public:
    ListView(const char* id, const float rowHeight) noexcept : id_(id), rowHeight_(rowHeight)
    {
    }

    ListView& Selected(const int index) noexcept
    {
        selected_ = index;
        return *this;
    }
    ListView& Highlighted(const int index) noexcept
    {
        highlighted_ = index;
        return *this;
    }
    ListView& Padding(const float px) noexcept
    {
        padding_ = px;
        return *this;
    }
    ListView& EmptyText(const char* text) noexcept
    {
        emptyText_ = text;
        return *this;
    }
    ListView& SelectedColor(const UI::Color32 c) noexcept
    {
        selectedColor_ = c;
        return *this;
    }
    ListView& HighlightColor(const UI::Color32 c) noexcept
    {
        highlightColor_ = c;
        return *this;
    }
    ListView& SeparatorColor(const UI::Color32 c) noexcept
    {
        separatorColor_ = c;
        return *this;
    }

    // Draws the scroll child + row chrome, calling drawRow(ctx, ListRow) for each
    // row's content. Returns the clicked row index this frame, or -1.
    template <typename RowFn>
    int Render(UIContext& ctx, const float width, const int count, RowFn&& drawRow)
    {
        int clicked = -1;

        ctx.BeginChild(id_, UI::Vec2(width, 0.f));

        DrawList& dl = ctx.GetWindowDrawList();
        for (int i = 0; i < count; ++i)
        {
            const bool selected = i == selected_;
            const bool highlighted = i == highlighted_;

            // Content-local Y of the row top. Scroll-independent: ImGui applies the
            // scroll offset internally, so window-local coordinates keep the content
            // height stable while scrolling and ScrollY isn't clamped.
            const float rowTop = static_cast<float>(i) * rowHeight_;
            ctx.SetCursorPosY(rowTop);

            const UI::Vec2 rowMin = ctx.GetCursorScreenPos();
            const UI::Vec2 rowMax = {rowMin.x + width, rowMin.y + rowHeight_};

            if (selected && !highlighted)
            {
                dl.AddRectFilled(rowMin, rowMax, selectedColor_);
            }
            if (highlighted)
            {
                dl.AddRectFilled(rowMin, rowMax, highlightColor_);
            }

            ctx.PushID(i);
            ctx.SetCursorPosX(0.f);
            if (ctx.Selectable("##row", selected, UI::SelectableFlags::None, UI::Vec2(width, rowHeight_ - 2.f)))
            {
                clicked = i;
            }
            ctx.PopID();

            const ListRow row{
                .index = i,
                .selected = selected,
                .highlighted = highlighted,
                .min = rowMin,
                .width = width,
                .height = rowHeight_,
                .localTop = rowTop,
                .padding = padding_,
            };
            drawRow(ctx, row);

            dl.AddLine(
                {rowMin.x + padding_, rowMax.y - 1.f}, {rowMax.x - padding_, rowMax.y - 1.f}, separatorColor_
            );
        }

        if (count == 0 && emptyText_ && emptyText_[0])
        {
            ctx.SetCursorPosY(40.f);
            ctx.SetCursorPosX(padding_);
            ctx.TextColored(UI::Color4f(0.4f, 0.35f, 0.55f, 1.f), emptyText_);
        }

        ctx.EndChild();
        return clicked;
    }

  private:
    const char* id_;
    float rowHeight_;
    int selected_ = -1;
    int highlighted_ = -1;
    float padding_ = 12.f;
    const char* emptyText_ = nullptr;
    UI::Color32 selectedColor_ = UI::MakeColor32(60, 45, 90, 160);
    UI::Color32 highlightColor_ = UI::MakeColor32(90, 60, 160, 190);
    UI::Color32 separatorColor_ = UI::MakeColor32(70, 55, 100, 80);
};
} // namespace framelift
