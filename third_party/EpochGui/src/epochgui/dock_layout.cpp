module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

        [[nodiscard]] float dock_fraction(const DockPaneState& pane, const DockLayoutOptions& options) noexcept
        {
            const float base = std::clamp(sane_or(options.edge_fraction, 0.24f), 0.08f, 0.92f);
            const float weight = std::clamp(sane_or(pane.weight, 1.0f), 0.10f, 4.0f);
            return std::clamp(base * weight, 0.05f, 0.95f);
        }

        [[nodiscard]] Rect clamp_rect_to_workspace(Rect rect, const DockLayoutOptions& options) noexcept
        {
            rect = sane_rect(rect);
            const Rect workspace = sane_rect(options.workspace);
            const float visible_margin = (std::max)(1.0f, sane_or(options.visible_margin, 48.0f));

            const float min_x = workspace.position.x + (std::min)(0.0f, workspace.size.x - visible_margin);
            const float min_y = workspace.position.y;
            const float max_x = workspace.position.x + (std::max)(visible_margin, workspace.size.x - visible_margin);
            const float max_y = workspace.position.y + (std::max)(visible_margin, workspace.size.y - visible_margin);

            rect.position.x = std::clamp(sane_or(rect.position.x, workspace.position.x), min_x, max_x);
            rect.position.y = std::clamp(sane_or(rect.position.y, workspace.position.y), min_y, max_y);
            return rect;
        }

        [[nodiscard]] Rect slot_rect(const DockPaneState& pane, const DockLayoutOptions& options) noexcept
        {
            const Rect workspace = sane_rect(options.workspace);
            const Vec2 minimum = clamp_size(pane.min_size, options.min_pane_size);
            const float fraction = dock_fraction(pane, options);

            switch (pane.slot)
            {
            case DockSlot::left:
            {
                const float width = (std::max)(minimum.x, workspace.size.x * fraction);
                return Rect{ workspace.position, { (std::min)(width, workspace.size.x), workspace.size.y } };
            }
            case DockSlot::right:
            {
                const float width = (std::max)(minimum.x, workspace.size.x * fraction);
                const float clamped_width = (std::min)(width, workspace.size.x);
                return Rect{
                    { workspace.position.x + workspace.size.x - clamped_width, workspace.position.y },
                    { clamped_width, workspace.size.y }
                };
            }
            case DockSlot::top:
            {
                const float height = (std::max)(minimum.y, workspace.size.y * fraction);
                return Rect{ workspace.position, { workspace.size.x, (std::min)(height, workspace.size.y) } };
            }
            case DockSlot::bottom:
            {
                const float height = (std::max)(minimum.y, workspace.size.y * fraction);
                const float clamped_height = (std::min)(height, workspace.size.y);
                return Rect{
                    { workspace.position.x, workspace.position.y + workspace.size.y - clamped_height },
                    { workspace.size.x, clamped_height }
                };
            }
            case DockSlot::center:
            case DockSlot::none:
            default:
                return workspace;
            }
        }

        [[nodiscard]] Rect default_popout_rect(
            const DockPaneState& pane,
            const DockLayoutOptions& options,
            std::uint32_t index) noexcept
        {
            const Rect workspace = sane_rect(options.workspace);
            const Vec2 size = clamp_size(options.default_popout_size, pane.min_size);
            const float spacing = (std::max)(0.0f, sane_or(options.popout_spacing, 24.0f));
            const float cascade = static_cast<float>(index % 8U) * spacing;
            return clamp_rect_to_workspace(Rect{
                { workspace.position.x + spacing + cascade, workspace.position.y + spacing + cascade },
                size
            }, options);
        }

        [[nodiscard]] DockPaneState* find_pane(
            DockPaneState* panes,
            std::uint32_t pane_count,
            std::uint32_t pane_id) noexcept
        {
            if (!panes || pane_id == 0U)
                return nullptr;

            for (std::uint32_t i = 0; i < pane_count; ++i)
            {
                if (panes[i].id == pane_id)
                    return &panes[i];
            }

            return nullptr;
        }

        [[nodiscard]] std::uint32_t count_context_windows(
            const DockPaneState* panes,
            std::uint32_t pane_count) noexcept
        {
            if (!panes)
                return 0U;

            std::uint32_t count = 0U;
            for (std::uint32_t i = 0; i < pane_count; ++i)
            {
                if (dock_pane_requests_context_window(panes[i]))
                    ++count;
            }

            return count;
        }

        [[nodiscard]] std::uint32_t hovered_pane_id(
            const DockPaneState* panes,
            std::uint32_t pane_count,
            const DockLayoutOptions& options,
            const DockLayoutInput& input) noexcept
        {
            if (!panes)
                return 0U;

            std::uint32_t best_id = 0U;
            std::uint32_t best_focus = 0U;
            for (std::uint32_t i = 0; i < pane_count; ++i)
            {
                DockPaneLayout layout = make_dock_pane_layout(panes[i], options, input);
                if (!layout.visible || !layout.hovered)
                    continue;

                if (best_id == 0U || panes[i].focus_order >= best_focus)
                {
                    best_id = panes[i].id;
                    best_focus = panes[i].focus_order;
                }
            }

            return best_id;
        }
    }

    bool is_valid_dock_slot(DockSlot slot) noexcept
    {
        return slot == DockSlot::left
            || slot == DockSlot::right
            || slot == DockSlot::top
            || slot == DockSlot::bottom
            || slot == DockSlot::center;
    }

    DockGuideLayout make_dock_guide_layout(const DockGuideOptions& options) noexcept
    {
        DockGuideLayout layout{};
        const Rect guideBounds = sane_rect(options.guide_bounds);
        const float extent = (std::max)(
            44.0f,
            sane_or(options.guide_extent, 94.0f));
        const float gap = (std::max)(
            0.0f,
            sane_or(options.guide_gap, 8.0f));
        const Vec2 center{
            guideBounds.position.x + guideBounds.size.x * 0.5f,
            guideBounds.position.y + guideBounds.size.y * 0.5f
        };

        const auto append = [&](DockGuideTarget target, Vec2 position, Vec2 size, Rect preview)
        {
            if (layout.count >= 7U)
                return;

            DockGuide& guide = layout.guides[layout.count++];
            guide.target = target;
            guide.target_bounds = Rect{ position, size };
            guide.preview_bounds = sane_rect(preview);
            guide.hovered = contains(guide.target_bounds, options.pointer);
            if (guide.hovered && layout.hovered_target == DockGuideTarget::none)
            {
                layout.hovered_target = target;
                layout.hovered_preview = guide.preview_bounds;
            }
        };

        const float leftX = center.x - extent - gap;
        const float middleX = center.x - extent * 0.5f;
        const float rightX = center.x + gap;
        const float topY = center.y - extent * 1.5f - gap;
        const float middleY = center.y - extent * 0.5f;
        const float bottomY = center.y + extent * 0.5f + gap;
        if (options.allow_side_tabs)
        {
            append(
                DockGuideTarget::left_tabs,
                { leftX, middleY },
                { extent, extent },
                options.left_tabs_preview);
            append(
                DockGuideTarget::right_tabs,
                { rightX, middleY },
                { extent, extent },
                options.right_tabs_preview);
        }
        if (options.allow_float)
        {
            append(
                DockGuideTarget::float_window,
                { middleX, middleY },
                { extent, extent },
                options.floating_preview);
        }
        if (options.allow_bottom_tabs)
        {
            append(
                DockGuideTarget::bottom_left_tabs,
                { leftX, bottomY },
                { extent, extent },
                options.bottom_left_tabs_preview);
            append(
                DockGuideTarget::bottom_right_tabs,
                { rightX, bottomY },
                { extent, extent },
                options.bottom_right_tabs_preview);
        }
        if (options.allow_contexts)
        {
            Vec2 leftContextPosition{ leftX, topY };
            Vec2 rightContextPosition{ rightX, topY };
            if (options.center_context_guides_in_previews)
            {
                const Rect leftPreview = sane_rect(options.left_context_preview);
                const Rect rightPreview = sane_rect(options.right_context_preview);
                leftContextPosition = {
                    leftPreview.position.x + (leftPreview.size.x - extent) * 0.5f,
                    leftPreview.position.y + (leftPreview.size.y - extent) * 0.5f
                };
                rightContextPosition = {
                    rightPreview.position.x + (rightPreview.size.x - extent) * 0.5f,
                    rightPreview.position.y + (rightPreview.size.y - extent) * 0.5f
                };
            }

            append(
                DockGuideTarget::left_context,
                leftContextPosition,
                { extent, extent },
                options.left_context_preview);
            append(
                DockGuideTarget::right_context,
                rightContextPosition,
                { extent, extent },
                options.right_context_preview);
        }

        return layout;
    }

    DockTabStripLayout make_dock_tab_strip_layout(
        const DockTabStripOptions& options) noexcept
    {
        DockTabStripLayout layout{};
        layout.cancelled = options.cancelled;
        if (!options.drag_active || options.cancelled || !options.target_compatible
            || options.source_group_id == 0U || options.target_group_id == 0U)
        {
            return layout;
        }

        const Rect strip = sane_rect(options.strip_bounds);
        if (!contains(strip, options.pointer))
            return layout;

        const std::uint32_t count = (std::min)(options.tab_count, maximum_dock_tabs);
        if (count > 0U && options.tab_bounds == nullptr)
            return layout;

        layout.target_hovered = true;
        layout.direct_drop_available = true;
        layout.suppress_outer_guides = true;

        float insertionX = strip.position.x;
        float bestDistance = (std::numeric_limits<float>::max)();
        layout.insertion_index = 0U;
        for (std::uint32_t index = 0U; index <= count; ++index)
        {
            float candidateX = strip.position.x;
            if (count > 0U)
            {
                if (index == 0U)
                    candidateX = options.tab_bounds[0U].position.x;
                else if (index == count)
                {
                    const Rect& last = options.tab_bounds[count - 1U];
                    candidateX = last.position.x + last.size.x;
                }
                else
                {
                    const Rect& previous = options.tab_bounds[index - 1U];
                    const Rect& next = options.tab_bounds[index];
                    const float previousEnd = previous.position.x + previous.size.x;
                    candidateX = (previousEnd + next.position.x) * 0.5f;
                }
            }

            const float distance = std::abs(options.pointer.x - candidateX);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                insertionX = candidateX;
                layout.insertion_index = index;
            }
        }

        const float markerExtent = (std::max)(
            1.0f, sane_or(options.marker_extent, 3.0f));
        layout.insertion_marker = {
            { insertionX - markerExtent * 0.5f, strip.position.y },
            { markerExtent, strip.size.y }
        };

        const float ghostWidth = std::clamp(
            sane_or(options.dragged_tab_size.x, 120.0f),
            1.0f,
            strip.size.x);
        const float ghostHeight = std::clamp(
            sane_or(options.dragged_tab_size.y, strip.size.y),
            1.0f,
            strip.size.y);
        const float ghostX = std::clamp(
            insertionX,
            strip.position.x,
            strip.position.x + strip.size.x - ghostWidth);
        layout.insertion_ghost = {
            { ghostX, strip.position.y + (strip.size.y - ghostHeight) * 0.5f },
            { ghostWidth, ghostHeight }
        };

        if (options.source_group_id == options.target_group_id
            && options.source_index < count)
        {
            const std::uint32_t finalIndex = layout.insertion_index > options.source_index
                ? layout.insertion_index - 1U
                : layout.insertion_index;
            layout.no_op = finalIndex == options.source_index;
        }
        return layout;
    }

    DockTabMoveResult move_dock_tab(
        DockTabGroup& source,
        DockTabGroup& target,
        std::uint32_t source_index,
        std::uint32_t insertion_index) noexcept
    {
        DockTabMoveResult result{};
        result.source_index = source_index;
        if (source.id == 0U || target.id == 0U)
        {
            result.code = DockTabMoveCode::invalid_group;
            return result;
        }
        if (source.count > maximum_dock_tabs || target.count > maximum_dock_tabs
            || source_index >= source.count || source.tabs[source_index].id == 0U)
        {
            result.code = DockTabMoveCode::invalid_source;
            return result;
        }
        if (insertion_index > target.count)
        {
            result.code = DockTabMoveCode::invalid_insertion;
            return result;
        }

        const bool sameGroup = &source == &target;
        const DockTabItem moving = source.tabs[source_index];
        if (!sameGroup)
        {
            if (target.count >= maximum_dock_tabs)
            {
                result.code = DockTabMoveCode::target_full;
                return result;
            }
            for (std::uint32_t index = 0U; index < target.count; ++index)
            {
                if (target.tabs[index].id == moving.id)
                {
                    result.code = DockTabMoveCode::duplicate_tab;
                    return result;
                }
            }
        }

        std::uint32_t targetIndex = insertion_index;
        if (sameGroup && targetIndex > source_index)
            --targetIndex;
        result.target_index = targetIndex;
        if (sameGroup && targetIndex == source_index)
        {
            result.code = DockTabMoveCode::unchanged;
            result.active_tab_id = moving.active ? moving.id : 0U;
            return result;
        }

        const bool movingWasActive = moving.active;
        for (std::uint32_t index = source_index; index + 1U < source.count; ++index)
            source.tabs[index] = source.tabs[index + 1U];
        --source.count;
        source.tabs[source.count] = {};

        if (sameGroup)
        {
            for (std::uint32_t index = source.count; index > targetIndex; --index)
                source.tabs[index] = source.tabs[index - 1U];
            source.tabs[targetIndex] = moving;
            ++source.count;
        }
        else
        {
            for (std::uint32_t index = target.count; index > targetIndex; --index)
                target.tabs[index] = target.tabs[index - 1U];
            DockTabItem moved = moving;
            moved.remembered_group_id = target.id;
            if (movingWasActive)
            {
                for (std::uint32_t index = 0U; index < target.count; ++index)
                    target.tabs[index].active = false;
                moved.active = true;
            }
            target.tabs[targetIndex] = moved;
            ++target.count;

            if (movingWasActive && source.count > 0U)
            {
                const std::uint32_t fallback = (std::min)(source_index, source.count - 1U);
                source.tabs[fallback].active = true;
            }
        }

        const auto normalizeKeyboardOrder = [](DockTabGroup& group) noexcept
        {
            for (std::uint32_t index = 0U; index < group.count; ++index)
                group.tabs[index].keyboard_order = index;
        };
        normalizeKeyboardOrder(source);
        if (!sameGroup)
            normalizeKeyboardOrder(target);

        result.code = DockTabMoveCode::moved;
        result.active_tab_id = movingWasActive ? moving.id : 0U;
        return result;
    }

    bool dock_pane_requests_context_window(const DockPaneState& pane) noexcept
    {
        return pane.visible && pane.popped_out;
    }

    DockPaneLayout make_dock_pane_layout(
        const DockPaneState& pane,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) noexcept
    {
        DockPaneLayout layout{};
        layout.id = pane.id;
        layout.slot = pane.slot == DockSlot::none ? DockSlot::center : pane.slot;
        layout.visible = pane.visible;
        layout.popped_out = pane.visible && pane.popped_out;
        layout.docked = pane.visible && !pane.popped_out;
        layout.context_window_requested = dock_pane_requests_context_window(pane);
        layout.active = pane.active;

        if (!layout.visible)
            return layout;

        layout.frame = pane.popped_out ? clamp_rect_to_workspace(pane.popout_rect, options) : slot_rect(pane, options);

        const float title_h = (std::max)(18.0f, sane_or(options.title_bar_height, 28.0f));
        const float padding = (std::max)(0.0f, sane_or(options.content_padding, 6.0f));
        layout.title_bar = Rect{
            layout.frame.position,
            { layout.frame.size.x, (std::min)(title_h, layout.frame.size.y) }
        };
        layout.content = Rect{
            { layout.frame.position.x + padding, layout.frame.position.y + layout.title_bar.size.y + padding },
            {
                (std::max)(1.0f, layout.frame.size.x - 2.0f * padding),
                (std::max)(1.0f, layout.frame.size.y - layout.title_bar.size.y - 2.0f * padding)
            }
        };
        layout.hovered = contains(layout.frame, input.mouse_position);
        return layout;
    }

    void activate_dock_pane(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        std::uint32_t pane_id) noexcept
    {
        if (!panes || pane_id == 0U)
            return;

        if (state.next_focus_order == 0U)
            state.next_focus_order = 1U;

        for (std::uint32_t i = 0; i < pane_count; ++i)
        {
            DockPaneState& pane = panes[i];
            if (!pane.visible)
            {
                pane.active = false;
                continue;
            }

            const bool active = pane.id == pane_id;
            pane.active = active;
            if (active)
            {
                pane.focus_order = state.next_focus_order++;
                state.active_pane_id = pane.id;
            }
        }
    }

    void normalize_dock_layout(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options) noexcept
    {
        if (state.next_focus_order == 0U)
            state.next_focus_order = 1U;

        if (!panes || pane_count == 0U)
        {
            state.active_pane_id = 0U;
            state.initialized = true;
            return;
        }

        std::uint32_t best_focus = 0U;
        std::uint32_t best_id = 0U;
        for (std::uint32_t i = 0; i < pane_count; ++i)
        {
            DockPaneState& pane = panes[i];
            if (pane.id == 0U)
                pane.id = i + 1U;

            if (!is_valid_dock_slot(pane.slot))
                pane.slot = DockSlot::center;

            pane.min_size = clamp_size(pane.min_size, options.min_pane_size);
            pane.weight = std::clamp(sane_or(pane.weight, 1.0f), 0.10f, 4.0f);

            if (!pane.initialized)
            {
                pane.docked_rect = slot_rect(pane, options);
                pane.popout_rect = default_popout_rect(pane, options, i);
                pane.initialized = true;
            }

            pane.docked_rect = slot_rect(pane, options);
            pane.popout_rect.size = clamp_size(pane.popout_rect.size, pane.min_size);
            pane.popout_rect = clamp_rect_to_workspace(pane.popout_rect, options);
            pane.popout_context_open = dock_pane_requests_context_window(pane);

            if (!pane.visible)
            {
                pane.active = false;
                continue;
            }

            if (pane.focus_order >= state.next_focus_order)
                state.next_focus_order = pane.focus_order + 1U;

            if (pane.active || pane.id == state.active_pane_id || pane.focus_order > best_focus)
            {
                best_focus = pane.focus_order;
                best_id = pane.id;
            }
        }

        if (best_id == 0U)
        {
            for (std::uint32_t i = 0; i < pane_count; ++i)
            {
                if (panes[i].visible)
                {
                    best_id = panes[i].id;
                    break;
                }
            }
        }

        activate_dock_pane(state, panes, pane_count, best_id);
        state.initialized = true;
    }

    DockLayoutResult update_dock_layout(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) noexcept
    {
        DockLayoutResult result{};
        normalize_dock_layout(state, panes, pane_count, options);
        const std::uint32_t previous_active = state.active_pane_id;
        const std::uint32_t previous_context_count = count_context_windows(panes, pane_count);

        if (!panes || pane_count == 0U)
            return result;

        std::uint32_t requested_active = input.activate_pane_id;
        if (requested_active == 0U && input.mouse_pressed)
            requested_active = hovered_pane_id(panes, pane_count, options, input);

        if (requested_active != 0U)
        {
            activate_dock_pane(state, panes, pane_count, requested_active);
            result.focus_changed = state.active_pane_id != previous_active;
        }

        if (input.toggle_popout_pane_id != 0U)
        {
            if (DockPaneState* pane = find_pane(panes, pane_count, input.toggle_popout_pane_id))
            {
                if (pane->visible)
                {
                    pane->popped_out = !pane->popped_out;
                    pane->popout_context_open = pane->popped_out;
                    result.changed = true;
                    result.context_windows_changed = true;
                    activate_dock_pane(state, panes, pane_count, pane->id);
                }
            }
        }

        if (input.popout_pane_id != 0U)
        {
            if (DockPaneState* pane = find_pane(panes, pane_count, input.popout_pane_id))
            {
                if (pane->visible && !pane->popped_out)
                {
                    pane->popped_out = true;
                    pane->popout_context_open = true;
                    result.changed = true;
                    result.context_windows_changed = true;
                    activate_dock_pane(state, panes, pane_count, pane->id);
                }
            }
        }

        if (input.redock_pane_id != 0U)
        {
            if (DockPaneState* pane = find_pane(panes, pane_count, input.redock_pane_id))
            {
                if (pane->visible && pane->popped_out)
                {
                    pane->popped_out = false;
                    pane->popout_context_open = false;
                    if (is_valid_dock_slot(input.redock_slot))
                        pane->slot = input.redock_slot;
                    result.changed = true;
                    result.context_windows_changed = true;
                    activate_dock_pane(state, panes, pane_count, pane->id);
                }
            }
        }

        normalize_dock_layout(state, panes, pane_count, options);
        result.active_pane_id = state.active_pane_id;
        result.context_window_count = count_context_windows(panes, pane_count);
        result.focus_changed = result.focus_changed || state.active_pane_id != previous_active;
        result.context_windows_changed = result.context_windows_changed
            || result.context_window_count != previous_context_count;
        result.changed = result.changed || result.focus_changed || result.context_windows_changed;
        return result;
    }

    std::string_view DockLayoutController::name() const noexcept
    {
        return "dock_layout";
    }

    bool DockLayoutController::is_valid_slot(DockSlot slot) const noexcept
    {
        return is_valid_dock_slot(slot);
    }

    bool DockLayoutController::pane_requests_context_window(const DockPaneState& pane) const noexcept
    {
        return dock_pane_requests_context_window(pane);
    }

    DockPaneLayout DockLayoutController::make_pane_layout(
        const DockPaneState& pane,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) const noexcept
    {
        return make_dock_pane_layout(pane, options, input);
    }

    void DockLayoutController::activate_pane(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        std::uint32_t pane_id) const noexcept
    {
        activate_dock_pane(state, panes, pane_count, pane_id);
    }

    void DockLayoutController::normalize(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options) const noexcept
    {
        normalize_dock_layout(state, panes, pane_count, options);
    }

    DockLayoutResult DockLayoutController::update(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) const noexcept
    {
        return update_dock_layout(state, panes, pane_count, options, input);
    }

    const DockLayoutController& dock_layout_controller() noexcept
    {
        static const DockLayoutController controller{};
        return controller;
    }
}
