#pragma once

#include "floating_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace epochengine::gui_lib
{
    enum class DockSlot : std::uint8_t
    {
        none,
        left,
        right,
        top,
        bottom,
        center
    };

    enum class DockGuideTarget : std::uint8_t
    {
        none = 0,
        left_tabs = 1,
        right_tabs = 2,
        bottom_left_tabs = 3,
        bottom_right_tabs = 4,
        before_context = 5,
        after_context = 6,
        float_window = 7,

        // Compatibility aliases for existing hosts. New code uses insertion order.
        left_context = before_context,
        right_context = after_context
    };

    struct DockGuideOptions
    {
        Rect guide_bounds{};
        Rect left_tabs_preview{};
        Rect right_tabs_preview{};
        Rect bottom_left_tabs_preview{};
        Rect bottom_right_tabs_preview{};
        Rect left_context_preview{};
        Rect right_context_preview{};
        Rect floating_preview{};
        Vec2 pointer{};
        float guide_extent{ 94.0f };
        float guide_gap{ 8.0f };
        bool allow_side_tabs{ true };
        bool allow_bottom_tabs{ true };
        bool allow_contexts{};
        bool allow_float{ true };
        bool center_context_guides_in_previews{};
    };

    struct DockGuide
    {
        DockGuideTarget target{ DockGuideTarget::none };
        Rect target_bounds{};
        Rect preview_bounds{};
        bool hovered{};
    };

    struct DockGuideLayout
    {
        DockGuide guides[7]{};
        std::uint32_t count{};
        DockGuideTarget hovered_target{ DockGuideTarget::none };
        Rect hovered_preview{};
    };

    inline constexpr std::uint32_t maximum_dock_tabs{ 64U };
    inline constexpr std::uint32_t invalid_dock_tab_index{
        (std::numeric_limits<std::uint32_t>::max)()
    };

    struct DockTabItem
    {
        std::uint64_t id{};
        std::uint64_t remembered_group_id{};
        std::uint32_t keyboard_order{};
        bool active{};
        bool closable{ true };
    };

    struct DockTabGroup
    {
        std::uint64_t id{};
        DockTabItem tabs[maximum_dock_tabs]{};
        std::uint32_t count{};
    };

    struct DockTabStripOptions
    {
        Rect strip_bounds{};
        const Rect* tab_bounds{};
        std::uint32_t tab_count{};
        Vec2 pointer{};
        Vec2 dragged_tab_size{ 120.0f, 28.0f };
        std::uint64_t source_group_id{};
        std::uint64_t target_group_id{};
        std::uint32_t source_index{ invalid_dock_tab_index };
        float marker_extent{ 3.0f };
        bool drag_active{};
        bool target_compatible{ true };
        bool cancelled{};
    };

    struct DockTabStripLayout
    {
        Rect insertion_marker{};
        Rect insertion_ghost{};
        std::uint32_t insertion_index{ invalid_dock_tab_index };
        bool target_hovered{};
        bool direct_drop_available{};
        bool suppress_outer_guides{};
        bool no_op{};
        bool cancelled{};
    };

    enum class DockTabMoveCode : std::uint8_t
    {
        moved,
        unchanged,
        invalid_group,
        invalid_source,
        invalid_insertion,
        duplicate_tab,
        target_full
    };

    struct DockTabMoveResult
    {
        DockTabMoveCode code{ DockTabMoveCode::invalid_source };
        std::uint32_t source_index{ invalid_dock_tab_index };
        std::uint32_t target_index{ invalid_dock_tab_index };
        std::uint64_t active_tab_id{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return code == DockTabMoveCode::moved
                || code == DockTabMoveCode::unchanged;
        }
    };

    [[nodiscard]] DockTabStripLayout make_dock_tab_strip_layout(
        const DockTabStripOptions& options) noexcept;
    [[nodiscard]] DockTabMoveResult move_dock_tab(
        DockTabGroup& source,
        DockTabGroup& target,
        std::uint32_t source_index,
        std::uint32_t insertion_index) noexcept;

    inline constexpr std::uint32_t maximum_context_grid_items{ 64U };
    inline constexpr std::uint32_t maximum_context_insertion_highlights{
        maximum_context_grid_items * 2U
    };
    inline constexpr std::uint32_t invalid_context_grid_index{
        (std::numeric_limits<std::uint32_t>::max)()
    };

    enum class DockInsertionPosition : std::uint8_t
    {
        before,
        after
    };

    struct DockContextGridOptions
    {
        const Rect* item_previews{};
        std::uint32_t item_count{};
        Vec2 pointer{};
        float highlight_extent{ 12.0f };
        std::uint32_t column_count{ 3U };
    };

    struct DockContextInsertionHighlight
    {
        DockInsertionPosition position{ DockInsertionPosition::before };
        Rect target_bounds{};
        Rect preview_bounds{};
        std::uint32_t context_item_index{ invalid_context_grid_index };
        std::uint32_t insertion_index{ invalid_context_grid_index };
        std::uint32_t row_index{ invalid_context_grid_index };
        std::uint32_t column_index{ invalid_context_grid_index };
        bool hovered{};
    };

    struct DockContextGridLayout
    {
        DockContextInsertionHighlight highlights[maximum_context_insertion_highlights]{};
        std::uint32_t count{};
        std::uint32_t row_count{};
        std::uint32_t column_count{};
        std::uint32_t hovered_context_item_index{ invalid_context_grid_index };
        std::uint32_t hovered_insertion_index{ invalid_context_grid_index };
        std::uint32_t hovered_row_index{ invalid_context_grid_index };
        std::uint32_t hovered_column_index{ invalid_context_grid_index };
        DockInsertionPosition hovered_position{ DockInsertionPosition::before };
        bool has_hovered_insertion{};
    };

    [[nodiscard]] inline DockContextGridLayout make_dock_context_grid_layout(
        const DockContextGridOptions& options) noexcept
    {
        DockContextGridLayout layout{};
        if (options.item_previews == nullptr || options.item_count == 0U)
            return layout;

        const std::uint32_t itemCount =
            std::min(options.item_count, maximum_context_grid_items);
        const std::uint32_t requestedColumns =
            options.column_count == 0U ? 1U : options.column_count;
        layout.column_count = std::min(requestedColumns, itemCount);
        layout.row_count =
            (itemCount + layout.column_count - 1U) / layout.column_count;

        const float extent = std::max(1.0f, options.highlight_extent);
        const auto append = [&](DockInsertionPosition position,
                                const Rect& target,
                                const Rect& preview,
                                std::uint32_t contextItemIndex,
                                std::uint32_t insertionIndex,
                                std::uint32_t rowIndex,
                                std::uint32_t columnIndex) noexcept
        {
            if (layout.count >= maximum_context_insertion_highlights)
                return;

            layout.highlights[layout.count++] = DockContextInsertionHighlight{
                position,
                target,
                preview,
                contextItemIndex,
                insertionIndex,
                rowIndex,
                columnIndex,
                false
            };
        };

        for (std::uint32_t index = 0U; index < itemCount; ++index)
        {
            const Rect preview = options.item_previews[index];
            if (preview.size.x <= 0.0f || preview.size.y <= 0.0f)
                continue;

            const std::uint32_t rowIndex = index / layout.column_count;
            const std::uint32_t columnIndex = index % layout.column_count;
            const float beforeWidth = std::min(extent, preview.size.x);
            const float afterWidth = std::min(extent, preview.size.x);
            const Rect beforeTarget{
                preview.position,
                { beforeWidth, preview.size.y }
            };
            const Rect afterTarget{
                {
                    preview.position.x + preview.size.x - afterWidth,
                    preview.position.y
                },
                { afterWidth, preview.size.y }
            };

            append(DockInsertionPosition::before,
                   beforeTarget,
                   preview,
                   index,
                   index,
                   rowIndex,
                   columnIndex);
            append(DockInsertionPosition::after,
                   afterTarget,
                   preview,
                   index,
                   index + 1U,
                   rowIndex,
                   columnIndex);
        }

        float bestDistance = (std::numeric_limits<float>::max)();
        std::uint32_t hoveredHighlightIndex = invalid_context_grid_index;
        for (std::uint32_t index = 0U; index < layout.count; ++index)
        {
            const auto& highlight = layout.highlights[index];
            if (!contains(highlight.target_bounds, options.pointer))
                continue;

            const float edgeX =
                highlight.position == DockInsertionPosition::before
                    ? highlight.preview_bounds.position.x
                    : highlight.preview_bounds.position.x
                        + highlight.preview_bounds.size.x;
            const float distance = std::abs(options.pointer.x - edgeX);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                hoveredHighlightIndex = index;
            }
        }

        if (hoveredHighlightIndex != invalid_context_grid_index)
        {
            auto& hovered = layout.highlights[hoveredHighlightIndex];
            hovered.hovered = true;
            layout.hovered_context_item_index = hovered.context_item_index;
            layout.hovered_insertion_index = hovered.insertion_index;
            layout.hovered_row_index = hovered.row_index;
            layout.hovered_column_index = hovered.column_index;
            layout.hovered_position = hovered.position;
            layout.has_hovered_insertion = true;
        }

        return layout;
    }
    struct DockPaneState
    {
        std::uint32_t id{};
        DockSlot slot{ DockSlot::center };
        Rect docked_rect{};
        Rect popout_rect{};
        Vec2 min_size{ 120.0f, 80.0f };
        float weight{ 1.0f };
        bool visible{ true };
        bool initialized{};
        bool popped_out{};
        bool popout_context_open{};
        bool active{};
        std::uint32_t focus_order{};
    };

    struct DockLayoutState
    {
        std::uint32_t active_pane_id{};
        std::uint32_t next_focus_order{ 1 };
        bool initialized{};
    };

    struct DockLayoutOptions
    {
        Rect workspace{};
        Vec2 min_pane_size{ 96.0f, 64.0f };
        Vec2 default_popout_size{ 360.0f, 260.0f };
        float edge_fraction{ 0.24f };
        float title_bar_height{ 28.0f };
        float content_padding{ 6.0f };
        float popout_spacing{ 24.0f };
        float visible_margin{ 48.0f };
    };

    struct DockLayoutInput
    {
        Vec2 mouse_position{};
        bool mouse_pressed{};
        bool mouse_released{};
        std::uint32_t activate_pane_id{};
        std::uint32_t popout_pane_id{};
        std::uint32_t redock_pane_id{};
        std::uint32_t toggle_popout_pane_id{};
        DockSlot redock_slot{ DockSlot::none };
    };

    struct DockPaneLayout
    {
        std::uint32_t id{};
        DockSlot slot{ DockSlot::center };
        Rect frame{};
        Rect title_bar{};
        Rect content{};
        bool visible{};
        bool hovered{};
        bool docked{};
        bool popped_out{};
        bool context_window_requested{};
        bool active{};
    };

    struct DockLayoutResult
    {
        bool changed{};
        bool focus_changed{};
        bool context_windows_changed{};
        std::uint32_t active_pane_id{};
        std::uint32_t context_window_count{};
    };

    class DockLayoutController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] bool is_valid_slot(DockSlot slot) const noexcept;
        [[nodiscard]] bool pane_requests_context_window(const DockPaneState& pane) const noexcept;
        [[nodiscard]] DockPaneLayout make_pane_layout(
            const DockPaneState& pane,
            const DockLayoutOptions& options,
            const DockLayoutInput& input) const noexcept;
        void activate_pane(
            DockLayoutState& state,
            DockPaneState* panes,
            std::uint32_t pane_count,
            std::uint32_t pane_id) const noexcept;
        void normalize(
            DockLayoutState& state,
            DockPaneState* panes,
            std::uint32_t pane_count,
            const DockLayoutOptions& options) const noexcept;
        [[nodiscard]] DockLayoutResult update(
            DockLayoutState& state,
            DockPaneState* panes,
            std::uint32_t pane_count,
            const DockLayoutOptions& options,
            const DockLayoutInput& input) const noexcept;
    };

    [[nodiscard]] const DockLayoutController& dock_layout_controller() noexcept;
    [[nodiscard]] bool is_valid_dock_slot(DockSlot slot) noexcept;
    [[nodiscard]] DockGuideLayout make_dock_guide_layout(
        const DockGuideOptions& options) noexcept;
    [[nodiscard]] bool dock_pane_requests_context_window(const DockPaneState& pane) noexcept;
    [[nodiscard]] DockPaneLayout make_dock_pane_layout(
        const DockPaneState& pane,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) noexcept;
    void activate_dock_pane(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        std::uint32_t pane_id) noexcept;
    void normalize_dock_layout(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options) noexcept;
    DockLayoutResult update_dock_layout(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) noexcept;
}
