#pragma once

#include "floating_window.hpp"

#include <cstdint>

namespace epochengine::gui_lib
{
    enum class SplitterAxis : std::uint8_t
    {
        vertical,
        horizontal
    };

    struct SplitterLayoutOptions
    {
        Rect area{};
        SplitterAxis axis{ SplitterAxis::vertical };
        float split_fraction{ 0.5f };
        float thickness{ 7.0f };
        float min_before{ 64.0f };
        float min_after{ 64.0f };
    };

    struct SplitterLayout
    {
        Rect before{};
        Rect handle{};
        Rect after{};
        float split_fraction{ 0.5f };
        float split_offset{};
        bool fits_minimums{};
    };

    enum class ProgressBarDirection : std::uint8_t
    {
        left_to_right,
        right_to_left,
        top_to_bottom,
        bottom_to_top
    };

    struct ProgressBarLayoutOptions
    {
        Rect track{};
        float value{};
        float minimum{};
        float maximum{ 1.0f };
        float padding{};
        ProgressBarDirection direction{ ProgressBarDirection::left_to_right };
    };

    struct ProgressBarLayout
    {
        Rect track{};
        Rect inner{};
        Rect fill{};
        float fraction{};
        bool has_range{};
    };

    struct LoadingScreenLayoutOptions
    {
        Rect viewport{};
        Vec2 preferred_panel_size{ 640.0f, 300.0f };
        Vec2 minimum_panel_size{ 360.0f, 220.0f };
        float margin{ 32.0f };
        float padding{ 28.0f };
        float gap{ 14.0f };
        float title_height{ 30.0f };
        float message_height{ 72.0f };
        float progress_height{ 24.0f };
        float status_height{ 22.0f };
        float action_height{ 64.0f };
        float progress_padding{ 2.0f };
        float progress_value{};
    };

    struct LoadingScreenLayout
    {
        Rect viewport{};
        Rect panel{};
        Rect title{};
        Rect message{};
        ProgressBarLayout progress{};
        Rect status{};
        Rect action{};
        float progress_fraction{};
        bool visible{};
    };

    inline constexpr std::uint32_t invalid_selectable_row_index = 0xffffffffU;

    struct SelectableListLayoutOptions
    {
        Rect viewport{};
        std::uint32_t row_count{};
        float row_height{ 22.0f };
        float row_gap{};
        float scroll_offset{};
        float content_padding_x{};
        float content_padding_y{};
    };

    struct SelectableListVisibleRange
    {
        std::uint32_t first_index{};
        std::uint32_t past_last_index{};
        float content_height{};
        bool has_visible_rows{};
    };

    struct SelectableRowLayout
    {
        std::uint32_t index{ invalid_selectable_row_index };
        Rect row{};
        Rect content{};
        bool visible{};
        bool hovered{};
        bool selected{};
    };

    class LayoutPrimitiveController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] SplitterLayout make_splitter(const SplitterLayoutOptions& options) const noexcept;
        [[nodiscard]] float splitter_fraction_from(Vec2 point, const SplitterLayoutOptions& options) const noexcept;
        [[nodiscard]] bool splitter_hit_test(const SplitterLayout& layout, Vec2 point, float hit_padding = 0.0f) const noexcept;
        [[nodiscard]] ProgressBarLayout make_progress_bar(const ProgressBarLayoutOptions& options) const noexcept;
        [[nodiscard]] LoadingScreenLayout make_loading_screen(const LoadingScreenLayoutOptions& options) const noexcept;
        [[nodiscard]] SelectableListVisibleRange visible_range(const SelectableListLayoutOptions& options) const noexcept;
        [[nodiscard]] SelectableRowLayout make_selectable_row(
            const SelectableListLayoutOptions& options,
            std::uint32_t index,
            Vec2 mouse_position,
            bool selected = false) const noexcept;
        [[nodiscard]] std::uint32_t selectable_row_at(const SelectableListLayoutOptions& options, Vec2 point) const noexcept;
    };

    [[nodiscard]] const LayoutPrimitiveController& layout_primitive_controller() noexcept;
    [[nodiscard]] SplitterLayout make_splitter_layout(const SplitterLayoutOptions& options) noexcept;
    [[nodiscard]] float splitter_fraction_from_point(const SplitterLayoutOptions& options, Vec2 point) noexcept;
    [[nodiscard]] bool splitter_hit_test(const SplitterLayout& layout, Vec2 point, float hit_padding = 0.0f) noexcept;

    [[nodiscard]] ProgressBarLayout make_progress_bar_layout(const ProgressBarLayoutOptions& options) noexcept;
    [[nodiscard]] LoadingScreenLayout make_loading_screen_layout(const LoadingScreenLayoutOptions& options) noexcept;

    [[nodiscard]] SelectableListVisibleRange selectable_list_visible_range(const SelectableListLayoutOptions& options) noexcept;
    [[nodiscard]] SelectableRowLayout make_selectable_row_layout(
        const SelectableListLayoutOptions& options,
        std::uint32_t index,
        Vec2 mouse_position,
        bool selected = false) noexcept;
    [[nodiscard]] std::uint32_t selectable_row_index_at(const SelectableListLayoutOptions& options, Vec2 point) noexcept;
}
