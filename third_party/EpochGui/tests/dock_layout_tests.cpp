#include <cmath>
#include <cstdint>

#if defined(EPOCHGUI_TEST_MODULE_IMPORT)
import epoch.gui;
#else
#include <gui/dock_layout.hpp>
#endif

namespace
{
    [[nodiscard]] bool same(float lhs, float rhs) noexcept
    {
        return std::abs(lhs - rhs) < 0.001f;
    }

#if !defined(EPOCHGUI_TEST_MODULE_IMPORT)
    [[nodiscard]] int check_three_by_two_grid()
    {
        using namespace epochengine::gui_lib;

        const Rect previews[6]{
            { { 10.0f, 20.0f }, { 100.0f, 80.0f } },
            { { 120.0f, 20.0f }, { 100.0f, 80.0f } },
            { { 230.0f, 20.0f }, { 100.0f, 80.0f } },
            { { 10.0f, 140.0f }, { 100.0f, 80.0f } },
            { { 120.0f, 140.0f }, { 100.0f, 80.0f } },
            { { 230.0f, 140.0f }, { 100.0f, 80.0f } }
        };

        DockContextGridOptions options{};
        options.item_previews = previews;
        options.item_count = 6U;
        options.pointer = { 11.0f, 160.0f };
        options.highlight_extent = 12.0f;
        options.column_count = 3U;

        const auto beforeLayout = make_dock_context_grid_layout(options);
        if (beforeLayout.count != 12U
            || beforeLayout.column_count != 3U
            || beforeLayout.row_count != 2U)
        {
            return 1;
        }

        for (std::uint32_t index = 0U; index < 6U; ++index)
        {
            const auto& before = beforeLayout.highlights[index * 2U];
            const auto& after = beforeLayout.highlights[index * 2U + 1U];
            const std::uint32_t expectedRow = index / 3U;
            const std::uint32_t expectedColumn = index % 3U;

            if (before.position != DockInsertionPosition::before
                || before.context_item_index != index
                || before.insertion_index != index
                || before.row_index != expectedRow
                || before.column_index != expectedColumn
                || !same(before.target_bounds.position.y, previews[index].position.y)
                || !same(before.target_bounds.size.y, previews[index].size.y))
            {
                return 2;
            }

            if (after.position != DockInsertionPosition::after
                || after.context_item_index != index
                || after.insertion_index != index + 1U
                || after.row_index != expectedRow
                || after.column_index != expectedColumn
                || !same(after.target_bounds.position.y, previews[index].position.y)
                || !same(after.target_bounds.size.y, previews[index].size.y))
            {
                return 3;
            }
        }

        if (!beforeLayout.has_hovered_insertion
            || beforeLayout.hovered_position != DockInsertionPosition::before
            || beforeLayout.hovered_context_item_index != 3U
            || beforeLayout.hovered_insertion_index != 3U
            || beforeLayout.hovered_row_index != 1U
            || beforeLayout.hovered_column_index != 0U)
        {
            return 4;
        }

        options.pointer = { 329.0f, 160.0f };
        const auto afterLayout = make_dock_context_grid_layout(options);
        if (!afterLayout.has_hovered_insertion
            || afterLayout.hovered_position != DockInsertionPosition::after
            || afterLayout.hovered_context_item_index != 5U
            || afterLayout.hovered_insertion_index != 6U
            || afterLayout.hovered_row_index != 1U
            || afterLayout.hovered_column_index != 2U)
        {
            return 5;
        }

        return 0;
    }

    [[nodiscard]] int check_general_item_count()
    {
        using namespace epochengine::gui_lib;

        const Rect previews[5]{
            { { 0.0f, 0.0f }, { 80.0f, 60.0f } },
            { { 90.0f, 0.0f }, { 80.0f, 60.0f } },
            { { 0.0f, 70.0f }, { 80.0f, 60.0f } },
            { { 90.0f, 70.0f }, { 80.0f, 60.0f } },
            { { 0.0f, 140.0f }, { 80.0f, 60.0f } }
        };

        DockContextGridOptions options{};
        options.item_previews = previews;
        options.item_count = 5U;
        options.pointer = { -100.0f, -100.0f };
        options.column_count = 2U;

        const auto layout = make_dock_context_grid_layout(options);
        if (layout.count != 10U
            || layout.column_count != 2U
            || layout.row_count != 3U
            || layout.has_hovered_insertion)
        {
            return 6;
        }

        const auto& finalBefore = layout.highlights[8U];
        const auto& finalAfter = layout.highlights[9U];
        if (finalBefore.context_item_index != 4U
            || finalBefore.row_index != 2U
            || finalBefore.column_index != 0U
            || finalBefore.insertion_index != 4U
            || finalAfter.insertion_index != 5U)
        {
            return 7;
        }

        return 0;
    }

    [[nodiscard]] int check_overlapping_targets_choose_nearest_edge()
    {
        using namespace epochengine::gui_lib;

        const Rect previews[2]{
            { { 0.0f, 0.0f }, { 100.0f, 40.0f } },
            { { 96.0f, 0.0f }, { 100.0f, 40.0f } }
        };

        DockContextGridOptions options{};
        options.item_previews = previews;
        options.item_count = 2U;
        options.pointer = { 97.0f, 20.0f };
        options.highlight_extent = 10.0f;
        options.column_count = 2U;

        const auto layout = make_dock_context_grid_layout(options);
        if (!layout.has_hovered_insertion
            || layout.hovered_position != DockInsertionPosition::before
            || layout.hovered_context_item_index != 1U
            || layout.hovered_insertion_index != 1U
            || layout.hovered_row_index != 0U
            || layout.hovered_column_index != 1U)
        {
            return 8;
        }

        return 0;
    }
#endif

    [[nodiscard]] int check_direct_tab_strip_slots()
    {
        using namespace epochengine::gui_lib;

        const Rect tabs[3]{
            { { 20.0f, 10.0f }, { 90.0f, 28.0f } },
            { { 112.0f, 10.0f }, { 110.0f, 28.0f } },
            { { 224.0f, 10.0f }, { 80.0f, 28.0f } }
        };
        DockTabStripOptions options{};
        options.strip_bounds = { { 16.0f, 8.0f }, { 360.0f, 32.0f } };
        options.tab_bounds = tabs;
        options.tab_count = 3U;
        options.pointer = { 221.0f, 24.0f };
        options.dragged_tab_size = { 110.0f, 28.0f };
        options.source_group_id = 7U;
        options.target_group_id = 7U;
        options.source_index = 0U;
        options.drag_active = true;

        const auto layout = make_dock_tab_strip_layout(options);
        if (!layout.target_hovered || !layout.direct_drop_available
            || !layout.suppress_outer_guides || layout.no_op
            || layout.insertion_index != 2U
            || !same(layout.insertion_marker.position.x, 221.5f)
            || !same(layout.insertion_ghost.size.x, 110.0f))
        {
            return 9;
        }

        options.pointer = { 40.0f, 24.0f };
        const auto noOp = make_dock_tab_strip_layout(options);
        if (!noOp.no_op || noOp.insertion_index != 0U)
            return 10;

        options.target_group_id = 8U;
        options.target_compatible = false;
        const auto incompatible = make_dock_tab_strip_layout(options);
        if (incompatible.target_hovered || incompatible.direct_drop_available
            || incompatible.suppress_outer_guides)
        {
            return 11;
        }

        options.target_compatible = true;
        options.cancelled = true;
        const auto cancelled = make_dock_tab_strip_layout(options);
        if (!cancelled.cancelled || cancelled.direct_drop_available
            || cancelled.suppress_outer_guides)
        {
            return 12;
        }

        options.cancelled = false;
        options.tab_bounds = nullptr;
        options.tab_count = 0U;
        options.pointer = { 80.0f, 24.0f };
        const auto empty = make_dock_tab_strip_layout(options);
        if (!empty.direct_drop_available || empty.insertion_index != 0U)
            return 13;
        return 0;
    }

    [[nodiscard]] int check_same_group_reorder()
    {
        using namespace epochengine::gui_lib;

        DockTabGroup group{};
        group.id = 11U;
        group.count = 4U;
        group.tabs[0] = { 101U, 11U, 0U, false, true };
        group.tabs[1] = { 102U, 11U, 1U, true, false };
        group.tabs[2] = { 103U, 11U, 2U, false, true };
        group.tabs[3] = { 104U, 11U, 3U, false, true };

        const auto moved = move_dock_tab(group, group, 1U, 4U);
        if (moved.code != DockTabMoveCode::moved || moved.target_index != 3U
            || moved.active_tab_id != 102U || group.count != 4U
            || group.tabs[0].id != 101U || group.tabs[1].id != 103U
            || group.tabs[2].id != 104U || group.tabs[3].id != 102U
            || !group.tabs[3].active || group.tabs[3].closable
            || group.tabs[3].remembered_group_id != 11U)
        {
            return 14;
        }
        for (std::uint32_t index = 0U; index < group.count; ++index)
        {
            if (group.tabs[index].keyboard_order != index)
                return 15;
        }

        const auto noOp = move_dock_tab(group, group, 3U, 4U);
        if (noOp.code != DockTabMoveCode::unchanged || group.tabs[3].id != 102U)
            return 16;
        return 0;
    }

    [[nodiscard]] int check_cross_group_move()
    {
        using namespace epochengine::gui_lib;

        DockTabGroup source{};
        source.id = 20U;
        source.count = 3U;
        source.tabs[0] = { 201U, 20U, 0U, false, true };
        source.tabs[1] = { 202U, 20U, 1U, true, false };
        source.tabs[2] = { 203U, 20U, 2U, false, true };

        DockTabGroup target{};
        target.id = 30U;
        target.count = 2U;
        target.tabs[0] = { 301U, 30U, 0U, true, true };
        target.tabs[1] = { 302U, 30U, 1U, false, true };

        const auto moved = move_dock_tab(source, target, 1U, 1U);
        if (moved.code != DockTabMoveCode::moved || moved.target_index != 1U
            || source.count != 2U || source.tabs[0].id != 201U
            || source.tabs[1].id != 203U || !source.tabs[1].active
            || target.count != 3U || target.tabs[0].id != 301U
            || target.tabs[1].id != 202U || target.tabs[2].id != 302U
            || !target.tabs[1].active || target.tabs[0].active
            || target.tabs[1].closable
            || target.tabs[1].remembered_group_id != 30U)
        {
            return 17;
        }
        for (std::uint32_t index = 0U; index < source.count; ++index)
            if (source.tabs[index].keyboard_order != index) return 18;
        for (std::uint32_t index = 0U; index < target.count; ++index)
            if (target.tabs[index].keyboard_order != index) return 19;

        DockTabGroup duplicateTarget = target;
        duplicateTarget.tabs[0].id = source.tabs[0].id;
        const auto duplicate = move_dock_tab(source, duplicateTarget, 0U, 0U);
        if (duplicate.code != DockTabMoveCode::duplicate_tab
            || source.count != 2U || duplicateTarget.count != 3U)
        {
            return 20;
        }
        return 0;
    }
}

int main()
{
#if !defined(EPOCHGUI_TEST_MODULE_IMPORT)
    if (const int result = check_three_by_two_grid(); result != 0)
        return result;
    if (const int result = check_general_item_count(); result != 0)
        return result;
    if (const int result = check_overlapping_targets_choose_nearest_edge(); result != 0)
        return result;
#endif
    if (const int result = check_direct_tab_strip_slots(); result != 0)
        return result;
    if (const int result = check_same_group_reorder(); result != 0)
        return result;
    return check_cross_group_move();
}
