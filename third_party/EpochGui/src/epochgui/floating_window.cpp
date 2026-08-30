module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

module epoch.gui;

namespace epochengine::gui_lib
{
    namespace
    {
        [[nodiscard]] float sane_or(float value, float fallback) noexcept
        {
            return std::isfinite(value) ? value : fallback;
        }

        [[nodiscard]] float clamp_min(float value, float minimum) noexcept
        {
            return (std::max)(sane_or(value, minimum), minimum);
        }

        [[nodiscard]] Vec2 clamp_size(Vec2 size, Vec2 minimum) noexcept
        {
            return {
                clamp_min(size.x, (std::max)(1.0f, minimum.x)),
                clamp_min(size.y, (std::max)(1.0f, minimum.y))
            };
        }

        [[nodiscard]] Rect sane_rect(Rect rect) noexcept
        {
            rect.position = { sane_or(rect.position.x, 0.0f), sane_or(rect.position.y, 0.0f) };
            rect.size = { clamp_min(rect.size.x, 1.0f), clamp_min(rect.size.y, 1.0f) };
            return rect;
        }

        [[nodiscard]] Rect inset_rect(Rect rect, float inset_x, float inset_y) noexcept
        {
            const float x = (std::min)(
                (std::max)(0.0f, sane_or(inset_x, 0.0f)),
                (std::max)(0.0f, (rect.size.x - 1.0f) * 0.5f));
            const float y = (std::min)(
                (std::max)(0.0f, sane_or(inset_y, 0.0f)),
                (std::max)(0.0f, (rect.size.y - 1.0f) * 0.5f));
            return Rect{
                { rect.position.x + x, rect.position.y + y },
                {
                    (std::max)(1.0f, rect.size.x - 2.0f * x),
                    (std::max)(1.0f, rect.size.y - 2.0f * y)
                }
            };
        }

        [[nodiscard]] Vec2 clamp_position(Vec2 position, Vec2 size, Vec2 viewport) noexcept
        {
            if (viewport.x <= 0.0f || viewport.y <= 0.0f)
                return position;

            const float visible_margin = 48.0f;
            const float min_x = (std::min)(0.0f, viewport.x - visible_margin);
            const float min_y = 0.0f;
            const float max_x = (std::max)(visible_margin, viewport.x - visible_margin);
            const float max_y = (std::max)(visible_margin, viewport.y - visible_margin);

            return {
                std::clamp(sane_or(position.x, 0.0f), min_x, max_x),
                std::clamp(sane_or(position.y, 0.0f), min_y, max_y)
            };
        }

        [[nodiscard]] FloatingWindowLayout make_layout(
            const FloatingWindowState& state,
            const FloatingWindowOptions& options) noexcept
        {
            FloatingWindowLayout layout{};
            layout.visible = state.open;
            layout.window = Rect{ state.position, state.size };

            const float title_h = (std::max)(18.0f, sane_or(options.title_bar_height, 32.0f));
            const float padding = (std::max)(0.0f, sane_or(options.content_padding, 6.0f));
            layout.title_bar = Rect{ state.position, { state.size.x, (std::min)(title_h, state.size.y) } };
            layout.content = Rect{
                { state.position.x + padding, state.position.y + layout.title_bar.size.y + padding },
                {
                    (std::max)(1.0f, state.size.x - 2.0f * padding),
                    (std::max)(1.0f, state.size.y - layout.title_bar.size.y - 2.0f * padding)
                }
            };

            constexpr float close_w = 24.0f;
            constexpr float close_h = 22.0f;
            layout.close_button = Rect{
                {
                    state.position.x + (std::max)(0.0f, state.size.x - close_w - padding),
                    state.position.y + (std::max)(2.0f, (layout.title_bar.size.y - close_h) * 0.5f)
                },
                { close_w, close_h }
            };

            constexpr float resize_size = 16.0f;
            layout.resize_handle = Rect{
                {
                    state.position.x + (std::max)(0.0f, state.size.x - resize_size),
                    state.position.y + (std::max)(0.0f, state.size.y - resize_size)
                },
                { resize_size, resize_size }
            };

            return layout;
        }

        [[nodiscard]] float axis_extent(Rect rect, SplitterAxis axis) noexcept
        {
            return axis == SplitterAxis::vertical ? rect.size.x : rect.size.y;
        }

        [[nodiscard]] float point_axis(Vec2 point, SplitterAxis axis) noexcept
        {
            return axis == SplitterAxis::vertical ? point.x : point.y;
        }

        [[nodiscard]] float origin_axis(Rect rect, SplitterAxis axis) noexcept
        {
            return axis == SplitterAxis::vertical ? rect.position.x : rect.position.y;
        }

        [[nodiscard]] float clamp_split_offset(
            float desired,
            float available,
            float min_before,
            float min_after) noexcept
        {
            if (available <= 0.0f)
                return 0.0f;

            const float before = (std::max)(0.0f, sane_or(min_before, 0.0f));
            const float after = (std::max)(0.0f, sane_or(min_after, 0.0f));
            if (before + after <= available)
                return std::clamp(sane_or(desired, available * 0.5f), before, available - after);

            return std::clamp(sane_or(desired, available * 0.5f), 0.0f, available);
        }

        [[nodiscard]] float row_stride(const SelectableListLayoutOptions& options) noexcept
        {
            return (std::max)(1.0f, sane_or(options.row_height, 22.0f))
                + (std::max)(0.0f, sane_or(options.row_gap, 0.0f));
        }

        [[nodiscard]] Rect selectable_row_rect(
            const SelectableListLayoutOptions& options,
            std::uint32_t index) noexcept
        {
            const Rect viewport = sane_rect(options.viewport);
            const float padding_y = (std::max)(0.0f, sane_or(options.content_padding_y, 0.0f));
            const float y = viewport.position.y
                + padding_y
                - (std::max)(0.0f, sane_or(options.scroll_offset, 0.0f))
                + static_cast<float>(index) * row_stride(options);

            return Rect{
                { viewport.position.x, y },
                {
                    viewport.size.x,
                    (std::max)(1.0f, sane_or(options.row_height, 22.0f))
                }
            };
        }

        [[nodiscard]] bool rect_overlaps_y(Rect a, Rect b) noexcept
        {
            return a.position.y + a.size.y >= b.position.y
                && a.position.y <= b.position.y + b.size.y;
        }
    }

    void normalize_floating_window(FloatingWindowState& state, const FloatingWindowOptions& options) noexcept
    {
        if (!state.initialized)
        {
            state.position = options.default_position;
            state.size = options.default_size;
            state.initialized = true;
        }

        state.size = clamp_size(state.size, options.min_size);
        state.position = clamp_position(state.position, state.size, options.viewport_size);
    }

    FloatingWindowLayout update_floating_window(
        FloatingWindowState& state,
        const FloatingWindowOptions& options,
        const FloatingWindowInput& input) noexcept
    {
        normalize_floating_window(state, options);
        FloatingWindowLayout layout = make_layout(state, options);
        if (!state.open)
            return layout;

        layout.hovered = contains(layout.window, input.mouse_position);
        layout.title_hovered = contains(layout.title_bar, input.mouse_position);
        layout.close_hovered = options.closable && contains(layout.close_button, input.mouse_position);
        layout.resize_hovered = options.resizable && contains(layout.resize_handle, input.mouse_position);

        if (input.mouse_pressed && layout.hovered)
        {
            layout.focused = true;
            if (layout.close_hovered)
            {
                state.close_pressed = true;
            }
            else if (layout.resize_hovered)
            {
                state.resizing = true;
                state.resize_origin_mouse = input.mouse_position;
                state.resize_origin_size = state.size;
            }
            else if (options.movable && layout.title_hovered)
            {
                state.dragging = true;
                state.drag_offset = {
                    input.mouse_position.x - state.position.x,
                    input.mouse_position.y - state.position.y
                };
            }
        }

        if (input.mouse_down && state.dragging)
        {
            state.position = {
                input.mouse_position.x - state.drag_offset.x,
                input.mouse_position.y - state.drag_offset.y
            };
            state.position = clamp_position(state.position, state.size, options.viewport_size);
            layout.moved = true;
        }

        if (input.mouse_down && state.resizing)
        {
            state.size = clamp_size({
                state.resize_origin_size.x + input.mouse_position.x - state.resize_origin_mouse.x,
                state.resize_origin_size.y + input.mouse_position.y - state.resize_origin_mouse.y
            }, options.min_size);
            state.position = clamp_position(state.position, state.size, options.viewport_size);
            layout.resized = true;
        }

        if (input.mouse_released)
        {
            if (state.close_pressed && layout.close_hovered)
            {
                layout.close_requested = true;
                state.open = false;
            }

            state.dragging = false;
            state.resizing = false;
            state.close_pressed = false;
        }

        FloatingWindowLayout updated = make_layout(state, options);
        updated.hovered = contains(updated.window, input.mouse_position);
        updated.title_hovered = contains(updated.title_bar, input.mouse_position);
        updated.close_hovered = options.closable && contains(updated.close_button, input.mouse_position);
        updated.resize_hovered = options.resizable && contains(updated.resize_handle, input.mouse_position);
        updated.focused = layout.focused;
        updated.moved = layout.moved;
        updated.resized = layout.resized;
        updated.close_requested = layout.close_requested;
        return updated;
    }

    SplitterLayout make_splitter_layout(const SplitterLayoutOptions& options) noexcept
    {
        const Rect area = sane_rect(options.area);
        const SplitterAxis axis = options.axis;
        const float extent = axis_extent(area, axis);
        const float thickness = (std::min)(
            extent,
            (std::max)(1.0f, sane_or(options.thickness, 7.0f)));
        const float available = (std::max)(0.0f, extent - thickness);
        const float desired = available * std::clamp(sane_or(options.split_fraction, 0.5f), 0.0f, 1.0f);
        const float split_offset = clamp_split_offset(
            desired,
            available,
            options.min_before,
            options.min_after);
        const float after_offset = split_offset + thickness;

        SplitterLayout layout{};
        layout.split_offset = split_offset;
        layout.split_fraction = available > 0.0f ? split_offset / available : 0.5f;
        layout.fits_minimums =
            (std::max)(0.0f, sane_or(options.min_before, 0.0f))
            + (std::max)(0.0f, sane_or(options.min_after, 0.0f)) <= available;

        if (axis == SplitterAxis::vertical)
        {
            layout.before = Rect{ area.position, { split_offset, area.size.y } };
            layout.handle = Rect{
                { area.position.x + split_offset, area.position.y },
                { thickness, area.size.y }
            };
            layout.after = Rect{
                { area.position.x + after_offset, area.position.y },
                { (std::max)(0.0f, area.size.x - after_offset), area.size.y }
            };
            return layout;
        }

        layout.before = Rect{ area.position, { area.size.x, split_offset } };
        layout.handle = Rect{
            { area.position.x, area.position.y + split_offset },
            { area.size.x, thickness }
        };
        layout.after = Rect{
            { area.position.x, area.position.y + after_offset },
            { area.size.x, (std::max)(0.0f, area.size.y - after_offset) }
        };
        return layout;
    }

    float splitter_fraction_from_point(const SplitterLayoutOptions& options, Vec2 point) noexcept
    {
        const Rect area = sane_rect(options.area);
        const SplitterAxis axis = options.axis;
        const float extent = axis_extent(area, axis);
        const float thickness = (std::min)(
            extent,
            (std::max)(1.0f, sane_or(options.thickness, 7.0f)));
        const float available = (std::max)(0.0f, extent - thickness);
        if (available <= 0.0f)
            return 0.5f;

        const float desired = point_axis(point, axis) - origin_axis(area, axis) - thickness * 0.5f;
        const float split_offset = clamp_split_offset(
            desired,
            available,
            options.min_before,
            options.min_after);
        return split_offset / available;
    }

    bool splitter_hit_test(const SplitterLayout& layout, Vec2 point, float hit_padding) noexcept
    {
        const float padding = (std::max)(0.0f, sane_or(hit_padding, 0.0f));
        const Rect hit_rect{
            { layout.handle.position.x - padding, layout.handle.position.y - padding },
            { layout.handle.size.x + 2.0f * padding, layout.handle.size.y + 2.0f * padding }
        };
        return contains(hit_rect, point);
    }

    ProgressBarLayout make_progress_bar_layout(const ProgressBarLayoutOptions& options) noexcept
    {
        ProgressBarLayout layout{};
        layout.track = sane_rect(options.track);
        const float padding = (std::max)(0.0f, sane_or(options.padding, 0.0f));
        layout.inner = inset_rect(layout.track, padding, padding);

        const float minimum = sane_or(options.minimum, 0.0f);
        const float maximum = sane_or(options.maximum, 1.0f);
        layout.has_range = maximum > minimum;
        if (layout.has_range)
        {
            const float value = std::clamp(sane_or(options.value, minimum), minimum, maximum);
            layout.fraction = (value - minimum) / (maximum - minimum);
        }

        layout.fill = layout.inner;
        switch (options.direction)
        {
        case ProgressBarDirection::right_to_left:
            layout.fill.size.x = layout.inner.size.x * layout.fraction;
            layout.fill.position.x = layout.inner.position.x + layout.inner.size.x - layout.fill.size.x;
            break;
        case ProgressBarDirection::top_to_bottom:
            layout.fill.size.y = layout.inner.size.y * layout.fraction;
            break;
        case ProgressBarDirection::bottom_to_top:
            layout.fill.size.y = layout.inner.size.y * layout.fraction;
            layout.fill.position.y = layout.inner.position.y + layout.inner.size.y - layout.fill.size.y;
            break;
        case ProgressBarDirection::left_to_right:
        default:
            layout.fill.size.x = layout.inner.size.x * layout.fraction;
            break;
        }

        return layout;
    }

    LoadingScreenLayout make_loading_screen_layout(const LoadingScreenLayoutOptions& options) noexcept
    {
        LoadingScreenLayout layout{};
        layout.viewport = sane_rect(options.viewport);
        layout.visible = layout.viewport.size.x > 1.0f && layout.viewport.size.y > 1.0f;
        if (!layout.visible)
            return layout;

        const float margin = (std::max)(0.0f, sane_or(options.margin, 32.0f));
        const float padding = (std::max)(8.0f, sane_or(options.padding, 28.0f));
        const float gap = (std::max)(0.0f, sane_or(options.gap, 14.0f));
        const float title_height = (std::max)(12.0f, sane_or(options.title_height, 30.0f));
        const float message_height = (std::max)(24.0f, sane_or(options.message_height, 72.0f));
        const float progress_height = (std::max)(12.0f, sane_or(options.progress_height, 24.0f));
        const float status_height = (std::max)(12.0f, sane_or(options.status_height, 22.0f));
        const float action_height = (std::max)(0.0f, sane_or(options.action_height, 64.0f));

        const Vec2 minimum_panel = clamp_size(options.minimum_panel_size, { 240.0f, 160.0f });
        Vec2 preferred_panel = clamp_size(options.preferred_panel_size, minimum_panel);
        const float available_width = (std::max)(minimum_panel.x, layout.viewport.size.x - 2.0f * margin);
        const float available_height = (std::max)(minimum_panel.y, layout.viewport.size.y - 2.0f * margin);

        preferred_panel.x = (std::max)(minimum_panel.x, (std::min)(preferred_panel.x, available_width));
        preferred_panel.y = (std::max)(minimum_panel.y, (std::min)(preferred_panel.y, available_height));

        const float required_height =
            padding * 2.0f
            + title_height
            + gap
            + message_height
            + gap
            + progress_height
            + gap * 0.5f
            + status_height
            + (action_height > 0.0f ? gap + action_height : 0.0f);
        preferred_panel.y = (std::max)(preferred_panel.y, (std::min)(available_height, required_height));

        layout.panel = Rect{
            {
                layout.viewport.position.x + std::floor((layout.viewport.size.x - preferred_panel.x) * 0.5f),
                layout.viewport.position.y + std::floor((layout.viewport.size.y - preferred_panel.y) * 0.5f)
            },
            preferred_panel
        };

        const float content_x = layout.panel.position.x + padding;
        const float content_width = (std::max)(1.0f, layout.panel.size.x - 2.0f * padding);
        float y = layout.panel.position.y + padding;

        layout.title = Rect{ { content_x, y }, { content_width, title_height } };
        y += title_height + gap;

        layout.message = Rect{ { content_x, y }, { content_width, message_height } };
        y += message_height + gap;

        const Rect progress_track{ { content_x, y }, { content_width, progress_height } };
        layout.progress = make_progress_bar_layout(ProgressBarLayoutOptions{
            .track = progress_track,
            .value = options.progress_value,
            .minimum = 0.0f,
            .maximum = 1.0f,
            .padding = options.progress_padding,
            .direction = ProgressBarDirection::left_to_right
        });
        layout.progress_fraction = layout.progress.fraction;
        y += progress_height + gap * 0.5f;

        layout.status = Rect{ { content_x, y }, { content_width, status_height } };
        y += status_height + gap;

        if (action_height > 0.0f)
        {
            const float action_width = (std::max)(160.0f, (std::min)(content_width, content_width * 0.72f));
            layout.action = Rect{
                { content_x + std::floor((content_width - action_width) * 0.5f), y },
                { action_width, action_height }
            };
        }

        return layout;
    }

    SelectableListVisibleRange selectable_list_visible_range(const SelectableListLayoutOptions& options) noexcept
    {
        SelectableListVisibleRange range{};
        const Rect viewport = sane_rect(options.viewport);
        const float stride = row_stride(options);
        const float row_height = (std::max)(1.0f, sane_or(options.row_height, 22.0f));
        const float padding_y = (std::max)(0.0f, sane_or(options.content_padding_y, 0.0f));
        const float scroll = (std::max)(0.0f, sane_or(options.scroll_offset, 0.0f));

        range.content_height =
            padding_y * 2.0f
            + static_cast<float>(options.row_count) * row_height
            + static_cast<float>(options.row_count > 0U ? options.row_count - 1U : 0U)
                * (std::max)(0.0f, sane_or(options.row_gap, 0.0f));

        if (options.row_count == 0U)
            return range;

        if (scroll >= range.content_height)
        {
            range.first_index = options.row_count;
            range.past_last_index = options.row_count;
            return range;
        }

        const float first_y = (scroll - padding_y - row_height) / stride;
        std::uint32_t first = first_y > 0.0f
            ? static_cast<std::uint32_t>(std::floor(first_y)) + 1U
            : 0U;

        const float visible_bottom = scroll + viewport.size.y;
        const float last_y = (std::max)(0.0f, visible_bottom - padding_y);
        std::uint32_t past_last = static_cast<std::uint32_t>(last_y / stride) + 1U;
        if (past_last > options.row_count)
            past_last = options.row_count;
        if (past_last < first)
            past_last = first;

        range.first_index = first;
        range.past_last_index = past_last;
        range.has_visible_rows = first < past_last;
        return range;
    }

    SelectableRowLayout make_selectable_row_layout(
        const SelectableListLayoutOptions& options,
        std::uint32_t index,
        Vec2 mouse_position,
        bool selected) noexcept
    {
        SelectableRowLayout layout{};
        layout.selected = selected;

        if (index >= options.row_count)
            return layout;

        layout.index = index;
        const Rect viewport = sane_rect(options.viewport);
        layout.row = selectable_row_rect(options, index);
        layout.content = inset_rect(
            layout.row,
            (std::max)(0.0f, sane_or(options.content_padding_x, 0.0f)),
            0.0f);
        layout.visible = rect_overlaps_y(layout.row, viewport);
        layout.hovered = layout.visible && contains(layout.row, mouse_position) && contains(viewport, mouse_position);
        return layout;
    }

    std::uint32_t selectable_row_index_at(const SelectableListLayoutOptions& options, Vec2 point) noexcept
    {
        const Rect viewport = sane_rect(options.viewport);
        if (options.row_count == 0U || !contains(viewport, point))
            return invalid_selectable_row_index;

        const float padding_y = (std::max)(0.0f, sane_or(options.content_padding_y, 0.0f));
        const float local_y = point.y
            - viewport.position.y
            + (std::max)(0.0f, sane_or(options.scroll_offset, 0.0f))
            - padding_y;
        if (local_y < 0.0f)
            return invalid_selectable_row_index;

        const float stride = row_stride(options);
        const std::uint32_t index = static_cast<std::uint32_t>(local_y / stride);
        if (index >= options.row_count)
            return invalid_selectable_row_index;

        const float y_in_row = local_y - static_cast<float>(index) * stride;
        if (y_in_row > (std::max)(1.0f, sane_or(options.row_height, 22.0f)))
            return invalid_selectable_row_index;

        return index;
    }

    std::string_view FloatingWindowController::name() const noexcept
    {
        return "floating_window";
    }

    void FloatingWindowController::normalize(
        FloatingWindowState& state,
        const FloatingWindowOptions& options) const noexcept
    {
        normalize_floating_window(state, options);
    }

    FloatingWindowLayout FloatingWindowController::update(
        FloatingWindowState& state,
        const FloatingWindowOptions& options,
        const FloatingWindowInput& input) const noexcept
    {
        return update_floating_window(state, options, input);
    }

    const FloatingWindowController& floating_window_controller() noexcept
    {
        static const FloatingWindowController controller{};
        return controller;
    }

    std::string_view LayoutPrimitiveController::name() const noexcept
    {
        return "layout_primitives";
    }

    SplitterLayout LayoutPrimitiveController::make_splitter(const SplitterLayoutOptions& options) const noexcept
    {
        return make_splitter_layout(options);
    }

    float LayoutPrimitiveController::splitter_fraction_from(
        Vec2 point,
        const SplitterLayoutOptions& options) const noexcept
    {
        return splitter_fraction_from_point(options, point);
    }

    bool LayoutPrimitiveController::splitter_hit_test(
        const SplitterLayout& layout,
        Vec2 point,
        float hit_padding) const noexcept
    {
        return epochengine::gui_lib::splitter_hit_test(layout, point, hit_padding);
    }

    ProgressBarLayout LayoutPrimitiveController::make_progress_bar(
        const ProgressBarLayoutOptions& options) const noexcept
    {
        return make_progress_bar_layout(options);
    }

    LoadingScreenLayout LayoutPrimitiveController::make_loading_screen(
        const LoadingScreenLayoutOptions& options) const noexcept
    {
        return make_loading_screen_layout(options);
    }

    SelectableListVisibleRange LayoutPrimitiveController::visible_range(
        const SelectableListLayoutOptions& options) const noexcept
    {
        return selectable_list_visible_range(options);
    }

    SelectableRowLayout LayoutPrimitiveController::make_selectable_row(
        const SelectableListLayoutOptions& options,
        std::uint32_t index,
        Vec2 mouse_position,
        bool selected) const noexcept
    {
        return make_selectable_row_layout(options, index, mouse_position, selected);
    }

    std::uint32_t LayoutPrimitiveController::selectable_row_at(
        const SelectableListLayoutOptions& options,
        Vec2 point) const noexcept
    {
        return selectable_row_index_at(options, point);
    }

    const LayoutPrimitiveController& layout_primitive_controller() noexcept
    {
        static const LayoutPrimitiveController controller{};
        return controller;
    }
}
