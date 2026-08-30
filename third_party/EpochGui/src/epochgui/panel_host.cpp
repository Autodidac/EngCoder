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

        [[nodiscard]] float positive_or(float value, float fallback) noexcept
        {
            return (std::max)(1.0f, sane_or(value, fallback));
        }

        [[nodiscard]] Rect sane_rect(Rect rect) noexcept
        {
            rect.position = { sane_or(rect.position.x, 0.0f), sane_or(rect.position.y, 0.0f) };
            rect.size = { positive_or(rect.size.x, 1.0f), positive_or(rect.size.y, 1.0f) };
            return rect;
        }

        [[nodiscard]] DockSlot resolved_dock_slot(DockSlot requested, DockSlot fallback) noexcept
        {
            if (is_valid_dock_slot(requested))
                return requested;
            if (is_valid_dock_slot(fallback))
                return fallback;
            return DockSlot::right;
        }

        [[nodiscard]] Rect default_external_rect(const PanelHostOptions& options) noexcept
        {
            if (options.default_external_frame.size.x > 0.0f && options.default_external_frame.size.y > 0.0f)
                return sane_rect(options.default_external_frame);

            if (options.floating.default_size.x > 0.0f && options.floating.default_size.y > 0.0f)
            {
                return sane_rect(Rect{
                    options.floating.default_position,
                    options.floating.default_size
                });
            }

            return sane_rect(options.docked_frame);
        }

        [[nodiscard]] Rect mode_frame(const PanelHostState& state) noexcept
        {
            switch (state.mode)
            {
            case PanelHostMode::floating:
                return sane_rect(Rect{ state.floating.position, state.floating.size });
            case PanelHostMode::popup:
                return sane_rect(state.popup.rect);
            case PanelHostMode::external_host:
                return sane_rect(state.external_frame);
            case PanelHostMode::docked:
            default:
                return sane_rect(state.docked_frame);
            }
        }

        [[nodiscard]] bool same_rect(Rect lhs, Rect rhs) noexcept
        {
            lhs = sane_rect(lhs);
            rhs = sane_rect(rhs);
            return lhs.position.x == rhs.position.x
                && lhs.position.y == rhs.position.y
                && lhs.size.x == rhs.size.x
                && lhs.size.y == rhs.size.y;
        }

        void set_mode(
            PanelHostState& state,
            PanelHostMode mode,
            const PanelHostOptions& options,
            DockSlot requested_slot) noexcept
        {
            state.mode = mode;
            switch (mode)
            {
            case PanelHostMode::docked:
                state.dock_slot = resolved_dock_slot(requested_slot, options.fallback_dock_slot);
                state.external_host_requested = false;
                state.external_host_active = false;
                state.popup.open = false;
                state.visible = true;
                break;
            case PanelHostMode::floating:
                state.external_host_requested = false;
                state.external_host_active = false;
                state.popup.open = false;
                state.floating.open = true;
                state.visible = true;
                normalize_floating_window(state.floating, options.floating);
                break;
            case PanelHostMode::popup:
                state.external_host_requested = false;
                state.external_host_active = false;
                state.popup.open = true;
                state.visible = true;
                break;
            case PanelHostMode::external_host:
                state.external_host_requested = true;
                state.visible = true;
                state.popup.open = false;
                if (state.external_frame.size.x <= 0.0f || state.external_frame.size.y <= 0.0f)
                    state.external_frame = default_external_rect(options);
                break;
            default:
                break;
            }
        }
    }

    std::string_view PanelHostController::name() const noexcept
    {
        return "panel_host";
    }

    void PanelHostController::focus(
        PanelHostState& state,
        DockableWindowHostState* host) const noexcept
    {
        if (!state.visible)
            return;

        if (state.id == 0U)
            state.id = 1U;

        state.active = true;
        if (host)
        {
            if (host->next_focus_order == 0U)
                host->next_focus_order = 1U;
            host->active_window_id = state.id;
            state.focus_order = host->next_focus_order++;
            host->changed_this_frame = true;
        }
        else
        {
            state.focus_order = state.focus_order == 0U ? 1U : state.focus_order + 1U;
        }
    }

    void PanelHostController::normalize(
        PanelHostState& state,
        const PanelHostOptions& options) const noexcept
    {
        if (state.id == 0U)
            state.id = 1U;

        if (!state.initialized)
        {
            state.initialized = true;
            state.visible = true;
            state.docked_frame = sane_rect(options.docked_frame);
            state.dock_slot = resolved_dock_slot(state.dock_slot, options.fallback_dock_slot);
            normalize_floating_window(state.floating, options.floating);
            normalize_popup(state.popup, options.popup, PopupInput{});
            state.external_frame = default_external_rect(options);
        }

        state.docked_frame = sane_rect(options.docked_frame);
        state.dock_slot = resolved_dock_slot(state.dock_slot, options.fallback_dock_slot);
        if (state.mode == PanelHostMode::floating)
            normalize_floating_window(state.floating, options.floating);
        if (state.mode == PanelHostMode::popup)
            normalize_popup(state.popup, options.popup, PopupInput{ .mouse_position = state.popup.rect.position });
        if (state.external_frame.size.x <= 0.0f || state.external_frame.size.y <= 0.0f)
            state.external_frame = default_external_rect(options);
        else
            state.external_frame = sane_rect(state.external_frame);
    }

    PanelHostMetadata PanelHostController::metadata(const PanelHostState& state) const noexcept
    {
        return PanelHostMetadata{
            .id = state.id,
            .mode = state.mode,
            .dock_slot = state.dock_slot,
            .frame = mode_frame(state),
            .visible = state.visible,
            .active = state.active,
            .wants_external_host = state.visible
                && state.mode == PanelHostMode::external_host
                && state.external_host_requested,
            .external_host_active = state.external_host_active,
            .external_host_token = state.external_host_token
        };
    }

    PanelHostResult PanelHostController::update(
        PanelHostState& state,
        const PanelHostOptions& options,
        const PanelHostInput& input) const noexcept
    {
        normalize(state, options);

        PanelHostResult result{};
        const PanelHostMode previous_mode = state.mode;
        const DockSlot previous_slot = state.dock_slot;
        const Rect previous_frame = mode_frame(state);
        const bool previous_visible = state.visible;
        const bool previous_external_requested = state.external_host_requested;
        const bool previous_external_active = state.external_host_active;
        const std::uint64_t previous_external_token = state.external_host_token;

        PanelHostAction action = input.requested_action;
        if (action == PanelHostAction::focus && state.visible)
        {
            focus(state, input.focus_host);
            result.focus_changed = true;
        }

        switch (action)
        {
        case PanelHostAction::dock:
            if (options.allow_dock)
                set_mode(state, PanelHostMode::docked, options, input.requested_dock_slot);
            break;
        case PanelHostAction::float_panel:
            if (options.allow_float)
                set_mode(state, PanelHostMode::floating, options, input.requested_dock_slot);
            break;
        case PanelHostAction::show_popup:
            if (options.allow_popup)
                set_mode(state, PanelHostMode::popup, options, input.requested_dock_slot);
            break;
        case PanelHostAction::request_external_host:
            if (options.allow_external_host)
                set_mode(state, PanelHostMode::external_host, options, input.requested_dock_slot);
            break;
        case PanelHostAction::redock_from_external_host:
            if (options.allow_dock)
                set_mode(state, PanelHostMode::docked, options, input.requested_dock_slot);
            break;
        case PanelHostAction::close:
            if (options.allow_close)
            {
                state.visible = false;
                state.floating.open = false;
                state.popup.open = false;
                state.external_host_requested = false;
                state.external_host_active = false;
                result.close_requested = true;
            }
            break;
        case PanelHostAction::focus:
        case PanelHostAction::none:
        default:
            break;
        }

        if (state.mode == PanelHostMode::floating && state.visible)
        {
            const FloatingWindowLayout floating = update_floating_window(
                state.floating,
                options.floating,
                FloatingWindowInput{
                    .mouse_position = input.mouse_position,
                    .mouse_down = input.mouse_down,
                    .mouse_pressed = input.mouse_pressed,
                    .mouse_released = input.mouse_released
                });
            if (floating.focused)
            {
                focus(state, input.focus_host);
                result.focus_changed = true;
            }
            if (floating.close_requested)
            {
                state.visible = false;
                result.close_requested = true;
            }
        }

        if (state.mode == PanelHostMode::popup && state.visible)
        {
            const PopupLayout popup = update_popup(
                state.popup,
                options.popup,
                PopupInput{
                    .mouse_position = input.mouse_position,
                    .mouse_pressed = input.mouse_pressed,
                    .mouse_released = input.mouse_released,
                    .close_requested = action == PanelHostAction::close,
                    .escape_pressed = input.escape_pressed
                });
            if (popup.closed)
                state.visible = false;
        }

        if (state.mode == PanelHostMode::external_host)
        {
            if (input.external_host_confirmed)
            {
                state.external_host_active = true;
                state.external_host_requested = false;
                state.external_host_token = input.external_host_token;
            }
            if (input.external_host_closed)
            {
                state.external_host_active = false;
                state.external_host_requested = false;
                if (options.allow_dock)
                    set_mode(state, PanelHostMode::docked, options, input.requested_dock_slot);
            }
        }

        result.action = action;
        result.placement_changed = previous_mode != state.mode
            || previous_slot != state.dock_slot
            || !same_rect(previous_frame, mode_frame(state));
        result.external_host_changed = previous_external_requested != state.external_host_requested
            || previous_external_active != state.external_host_active
            || previous_external_token != state.external_host_token;
        result.changed = result.focus_changed
            || result.placement_changed
            || result.external_host_changed
            || previous_visible != state.visible
            || result.close_requested;
        result.metadata = metadata(state);
        return result;
    }

    const PanelHostController& panel_host_controller() noexcept
    {
        static const PanelHostController controller{};
        return controller;
    }

    void focus_panel_host(
        PanelHostState& state,
        DockableWindowHostState* host) noexcept
    {
        panel_host_controller().focus(state, host);
    }

    void normalize_panel_host(
        PanelHostState& state,
        const PanelHostOptions& options) noexcept
    {
        panel_host_controller().normalize(state, options);
    }

    PanelHostMetadata panel_host_metadata(
        const PanelHostState& state) noexcept
    {
        return panel_host_controller().metadata(state);
    }

    PanelHostResult update_panel_host(
        PanelHostState& state,
        const PanelHostOptions& options,
        const PanelHostInput& input) noexcept
    {
        return panel_host_controller().update(state, options, input);
    }
}
