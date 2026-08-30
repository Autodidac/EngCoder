module;

#include <algorithm>
#include <cmath>
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

        [[nodiscard]] Vec2 sane_size(Vec2 size) noexcept
        {
            return {
                clamp_min(size.x, 1.0f),
                clamp_min(size.y, 1.0f)
            };
        }

        [[nodiscard]] Rect sane_rect(Rect rect) noexcept
        {
            rect.position = { sane_or(rect.position.x, 0.0f), sane_or(rect.position.y, 0.0f) };
            rect.size = sane_size(rect.size);
            return rect;
        }

        [[nodiscard]] bool has_viewport(Vec2 viewport) noexcept
        {
            return viewport.x > 0.0f && viewport.y > 0.0f;
        }

        [[nodiscard]] Rect candidate_rect(
            const PopupOptions& options,
            const PopupInput& input,
            PopupPlacement placement,
            Vec2 size) noexcept
        {
            const Rect owner = sane_rect(options.owner);
            const float gap = (std::max)(0.0f, sane_or(options.gap, 4.0f));

            switch (placement)
            {
            case PopupPlacement::above:
                return Rect{ { owner.position.x, owner.position.y - size.y - gap }, size };
            case PopupPlacement::right:
                return Rect{ { owner.position.x + owner.size.x + gap, owner.position.y }, size };
            case PopupPlacement::left:
                return Rect{ { owner.position.x - size.x - gap, owner.position.y }, size };
            case PopupPlacement::centered:
            {
                if (has_viewport(options.viewport_size))
                {
                    return Rect{
                        {
                            (options.viewport_size.x - size.x) * 0.5f,
                            (options.viewport_size.y - size.y) * 0.5f
                        },
                        size
                    };
                }

                return Rect{
                    {
                        owner.position.x + (owner.size.x - size.x) * 0.5f,
                        owner.position.y + (owner.size.y - size.y) * 0.5f
                    },
                    size
                };
            }
            case PopupPlacement::cursor:
                return Rect{
                    {
                        input.mouse_position.x + sane_or(options.cursor_offset.x, 12.0f),
                        input.mouse_position.y + sane_or(options.cursor_offset.y, 12.0f)
                    },
                    size
                };
            case PopupPlacement::below:
            default:
                return Rect{ { owner.position.x, owner.position.y + owner.size.y + gap }, size };
            }
        }

        [[nodiscard]] PopupPlacement opposite(PopupPlacement placement) noexcept
        {
            switch (placement)
            {
            case PopupPlacement::below:
                return PopupPlacement::above;
            case PopupPlacement::above:
                return PopupPlacement::below;
            case PopupPlacement::right:
                return PopupPlacement::left;
            case PopupPlacement::left:
                return PopupPlacement::right;
            case PopupPlacement::centered:
            case PopupPlacement::cursor:
            default:
                return placement;
            }
        }

        [[nodiscard]] bool overflows(Rect rect, Vec2 viewport, float margin) noexcept
        {
            if (!has_viewport(viewport))
                return false;

            return rect.position.x < margin
                || rect.position.y < margin
                || rect.position.x + rect.size.x > viewport.x - margin
                || rect.position.y + rect.size.y > viewport.y - margin;
        }

        [[nodiscard]] Rect clamp_to_viewport(Rect rect, Vec2 viewport, float margin, bool* clamped) noexcept
        {
            if (clamped)
                *clamped = false;

            if (!has_viewport(viewport))
                return rect;

            const float sane_margin = (std::max)(0.0f, sane_or(margin, 4.0f));
            const float max_width = (std::max)(1.0f, viewport.x - 2.0f * sane_margin);
            const float max_height = (std::max)(1.0f, viewport.y - 2.0f * sane_margin);

            Rect result = rect;
            result.size.x = (std::min)(result.size.x, max_width);
            result.size.y = (std::min)(result.size.y, max_height);

            const float min_x = sane_margin;
            const float min_y = sane_margin;
            const float max_x = (std::max)(min_x, viewport.x - sane_margin - result.size.x);
            const float max_y = (std::max)(min_y, viewport.y - sane_margin - result.size.y);
            result.position.x = std::clamp(sane_or(result.position.x, min_x), min_x, max_x);
            result.position.y = std::clamp(sane_or(result.position.y, min_y), min_y, max_y);

            if (clamped)
            {
                *clamped = result.position.x != rect.position.x
                    || result.position.y != rect.position.y
                    || result.size.x != rect.size.x
                    || result.size.y != rect.size.y;
            }

            return result;
        }
    }

    Rect place_popup(
        const PopupOptions& options,
        const PopupInput& input,
        PopupPlacement* used_placement,
        bool* flipped,
        bool* clamped) noexcept
    {
        if (flipped)
            *flipped = false;
        if (clamped)
            *clamped = false;

        Vec2 size = sane_size(options.preferred_size);
        if (options.clamp_to_viewport && has_viewport(options.viewport_size))
        {
            const float margin = (std::max)(0.0f, sane_or(options.margin, 4.0f));
            size.x = (std::min)(size.x, (std::max)(1.0f, options.viewport_size.x - 2.0f * margin));
            size.y = (std::min)(size.y, (std::max)(1.0f, options.viewport_size.y - 2.0f * margin));
        }

        PopupPlacement placement = options.placement;
        Rect rect = candidate_rect(options, input, placement, size);

        if (options.flip_to_fit && overflows(rect, options.viewport_size, sane_or(options.margin, 4.0f)))
        {
            const PopupPlacement alternate = opposite(placement);
            if (alternate != placement)
            {
                const Rect alternate_rect = candidate_rect(options, input, alternate, size);
                if (!overflows(alternate_rect, options.viewport_size, sane_or(options.margin, 4.0f))
                    || overflows(rect, options.viewport_size, sane_or(options.margin, 4.0f)))
                {
                    rect = alternate_rect;
                    placement = alternate;
                    if (flipped)
                        *flipped = true;
                }
            }
        }

        if (options.clamp_to_viewport)
            rect = clamp_to_viewport(rect, options.viewport_size, sane_or(options.margin, 4.0f), clamped);

        if (used_placement)
            *used_placement = placement;

        return rect;
    }

    void normalize_popup(
        PopupState& state,
        const PopupOptions& options,
        const PopupInput& input) noexcept
    {
        PopupPlacement placement{};
        state.rect = place_popup(options, input, &placement);
        state.placement = placement;
        state.initialized = true;
    }

    PopupLayout update_popup(
        PopupState& state,
        const PopupOptions& options,
        const PopupInput& input) noexcept
    {
        const bool was_open = state.open;

        if (input.toggle_requested)
            state.open = !state.open;
        else if (input.open_requested || input.owner_pressed)
            state.open = true;

        bool flipped = false;
        bool clamped = false;
        PopupPlacement placement{};
        state.rect = place_popup(options, input, &placement, &flipped, &clamped);
        state.placement = placement;
        state.initialized = true;

        PopupLayout layout{};
        layout.popup = state.rect;
        layout.owner = sane_rect(options.owner);
        layout.placement = placement;
        layout.visible = state.open;
        layout.hovered = state.open && contains(layout.popup, input.mouse_position);
        layout.owner_hovered = contains(layout.owner, input.mouse_position);
        layout.flipped = flipped;
        layout.clamped = clamped;

        if (state.open && input.mouse_pressed)
        {
            state.pressed_inside = layout.hovered;
            state.pressed_owner = layout.owner_hovered;
        }

        const bool outside_press = state.open
            && options.close_on_outside_press
            && input.mouse_pressed
            && !layout.hovered
            && !layout.owner_hovered;
        if (input.close_requested || input.escape_pressed || outside_press)
        {
            state.open = false;
            state.pressed_inside = false;
            state.pressed_owner = false;
        }

        if (input.mouse_released)
        {
            state.pressed_inside = false;
            state.pressed_owner = false;
        }

        layout.visible = state.open;
        layout.opened = state.open && !was_open;
        layout.closed = !state.open && was_open;
        layout.hovered = state.open && contains(layout.popup, input.mouse_position);
        return layout;
    }

    std::string_view PopupLayoutController::name() const noexcept
    {
        return "popup_layout";
    }

    Rect PopupLayoutController::place(
        const PopupOptions& options,
        const PopupInput& input,
        PopupPlacement* used_placement,
        bool* flipped,
        bool* clamped) const noexcept
    {
        return place_popup(options, input, used_placement, flipped, clamped);
    }

    void PopupLayoutController::normalize(
        PopupState& state,
        const PopupOptions& options,
        const PopupInput& input) const noexcept
    {
        normalize_popup(state, options, input);
    }

    PopupLayout PopupLayoutController::update(
        PopupState& state,
        const PopupOptions& options,
        const PopupInput& input) const noexcept
    {
        return update_popup(state, options, input);
    }

    const PopupLayoutController& popup_layout_controller() noexcept
    {
        static const PopupLayoutController controller{};
        return controller;
    }
}
