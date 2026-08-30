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

        [[nodiscard]] bool dockable_slot(DockSlot slot) noexcept
        {
            switch (slot)
            {
            case DockSlot::left:
            case DockSlot::right:
            case DockSlot::top:
            case DockSlot::bottom:
            case DockSlot::center:
                return true;
            case DockSlot::none:
            default:
                return false;
            }
        }

        [[nodiscard]] DockSlot resolved_dock_slot(DockSlot requested, DockSlot fallback) noexcept
        {
            if (dockable_slot(requested))
                return requested;
            if (dockable_slot(fallback))
                return fallback;
            return DockSlot::right;
        }

        [[nodiscard]] Rect docked_frame(const DockableWindowOptions& options) noexcept
        {
            Rect frame = sane_rect(options.docked_frame);
            if (options.viewport_size.x > 0.0f && options.viewport_size.y > 0.0f)
            {
                frame.position.x = std::clamp(
                    sane_or(frame.position.x, 0.0f),
                    0.0f,
                    (std::max)(0.0f, options.viewport_size.x - 48.0f));
                frame.position.y = std::clamp(
                    sane_or(frame.position.y, 0.0f),
                    0.0f,
                    (std::max)(0.0f, options.viewport_size.y - 48.0f));
            }
            return frame;
        }

        [[nodiscard]] Rect action_rect(Rect title_bar, float width, float gap, std::uint32_t index) noexcept
        {
            const float w = (std::max)(24.0f, sane_or(width, 72.0f));
            const float g = (std::max)(0.0f, sane_or(gap, 4.0f));
            const float h = (std::max)(18.0f, title_bar.size.y - 6.0f);
            const float x = title_bar.position.x
                + title_bar.size.x
                - (static_cast<float>(index) + 1.0f) * w
                - static_cast<float>(index) * g
                - g;
            const float y = title_bar.position.y + (title_bar.size.y - h) * 0.5f;
            return Rect{ { x, y }, { w, h } };
        }

        [[nodiscard]] DockableWindowChrome make_base_chrome(
            const DockableWindowState& state,
            const DockableWindowOptions& options) noexcept
        {
            DockableWindowChrome chrome{};
            chrome.visible = state.visible;
            chrome.active = state.active;

            if (!state.visible)
                return chrome;

            if (state.mode == DockableWindowMode::floating || state.mode == DockableWindowMode::detached)
            {
                FloatingWindowState floatingCopy = state.floating;
                const FloatingWindowLayout floating = update_floating_window(
                    floatingCopy,
                    options.floating,
                    FloatingWindowInput{});
                chrome.frame = floating.window;
                chrome.title_bar = floating.title_bar;
                chrome.content = floating.content;
            }
            else
            {
                chrome.frame = docked_frame(options);
                const float title_h = (std::min)(
                    chrome.frame.size.y,
                    (std::max)(18.0f, sane_or(options.title_bar_height, 30.0f)));
                const float padding = (std::max)(0.0f, sane_or(options.content_padding, 6.0f));
                chrome.title_bar = Rect{ chrome.frame.position, { chrome.frame.size.x, title_h } };
                chrome.content = inset_rect(
                    Rect{
                        { chrome.frame.position.x, chrome.frame.position.y + title_h },
                        { chrome.frame.size.x, (std::max)(1.0f, chrome.frame.size.y - title_h) }
                    },
                    padding,
                    padding);
            }

            std::uint32_t actionIndex = 0;
            if (options.allow_close)
                chrome.close_button = action_rect(chrome.title_bar, options.action_button_width, options.action_button_gap, actionIndex++);
            if (options.allow_detach)
                chrome.detach_button = action_rect(chrome.title_bar, options.action_button_width, options.action_button_gap, actionIndex++);
            if (options.allow_float)
                chrome.float_button = action_rect(chrome.title_bar, options.action_button_width, options.action_button_gap, actionIndex++);
            if (options.allow_dock)
                chrome.dock_button = action_rect(chrome.title_bar, options.action_button_width, options.action_button_gap, actionIndex++);

            return chrome;
        }

        [[nodiscard]] DockableWindowAction action_from_hit(
            const DockableWindowChrome& chrome,
            const DockableWindowOptions& options,
            Vec2 point) noexcept
        {
            if (options.allow_close && contains(chrome.close_button, point))
                return DockableWindowAction::close;
            if (options.allow_detach && contains(chrome.detach_button, point))
                return DockableWindowAction::detach;
            if (options.allow_float && contains(chrome.float_button, point))
                return DockableWindowAction::float_window;
            if (options.allow_dock && contains(chrome.dock_button, point))
                return DockableWindowAction::dock;
            if (contains(chrome.frame, point))
                return DockableWindowAction::focus;
            return DockableWindowAction::none;
        }

        void apply_action(
            DockableWindowHostState& host,
            DockableWindowState& state,
            const DockableWindowOptions& options,
            DockableWindowAction action,
            DockSlot requestedSlot,
            DockableWindowResult& result) noexcept
        {
            result.action = action;
            switch (action)
            {
            case DockableWindowAction::focus:
                focus_dockable_window(host, state);
                result.focused = true;
                break;
            case DockableWindowAction::dock:
                if (options.allow_dock)
                {
                    state.mode = DockableWindowMode::docked;
                    state.dock_slot = resolved_dock_slot(requestedSlot, options.fallback_dock_slot);
                    state.detach_requested = false;
                    state.close_requested = false;
                    result.dock_requested = true;
                    result.changed = true;
                    host.changed_this_frame = true;
                    focus_dockable_window(host, state);
                }
                break;
            case DockableWindowAction::float_window:
                if (options.allow_float)
                {
                    state.mode = DockableWindowMode::floating;
                    state.floating.open = true;
                    state.visible = true;
                    state.detach_requested = false;
                    state.close_requested = false;
                    result.float_requested = true;
                    result.changed = true;
                    host.changed_this_frame = true;
                    focus_dockable_window(host, state);
                }
                break;
            case DockableWindowAction::detach:
                if (options.allow_detach)
                {
                    state.mode = DockableWindowMode::detached;
                    state.detach_requested = true;
                    state.close_requested = false;
                    result.detach_requested = true;
                    result.changed = true;
                    host.changed_this_frame = true;
                    focus_dockable_window(host, state);
                }
                break;
            case DockableWindowAction::close:
                if (options.allow_close)
                {
                    state.visible = false;
                    state.floating.open = false;
                    state.close_requested = true;
                    state.detach_requested = false;
                    result.close_requested = true;
                    result.changed = true;
                    host.changed_this_frame = true;
                    if (host.active_window_id == state.id)
                        host.active_window_id = 0;
                }
                break;
            case DockableWindowAction::none:
            default:
                break;
            }
        }
    }

    std::string_view DockableWindowController::name() const noexcept
    {
        return "dockable_window";
    }

    void DockableWindowController::focus(
        DockableWindowHostState& host,
        DockableWindowState& state) const noexcept
    {
        if (!state.visible)
            return;

        if (state.id == 0)
            state.id = host.active_window_id != 0 ? host.active_window_id : 1;

        state.active = true;
        host.active_window_id = state.id;
        state.focus_order = host.next_focus_order++;
        if (host.next_focus_order == 0)
            host.next_focus_order = 1;
    }

    void DockableWindowController::normalize(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options) const noexcept
    {
        host.changed_this_frame = false;
        if (state.id == 0)
            state.id = 1;

        if (!state.initialized)
        {
            state.initialized = true;
            state.visible = true;
            state.dock_slot = resolved_dock_slot(state.dock_slot, options.fallback_dock_slot);
            state.floating.open = true;
            normalize_floating_window(state.floating, options.floating);
        }

        state.dock_slot = resolved_dock_slot(state.dock_slot, options.fallback_dock_slot);

        if (!state.visible)
        {
            state.active = false;
            if (host.active_window_id == state.id)
                host.active_window_id = 0;
            return;
        }

        if (state.mode == DockableWindowMode::floating || state.mode == DockableWindowMode::detached)
        {
            state.floating.open = true;
            normalize_floating_window(state.floating, options.floating);
        }

        state.active = host.active_window_id == state.id;
    }

    DockableWindowChrome DockableWindowController::make_chrome(
        const DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) const noexcept
    {
        DockableWindowState copy = state;
        DockableWindowChrome chrome = make_base_chrome(copy, options);
        if (!chrome.visible)
            return chrome;

        chrome.hovered = contains(chrome.frame, input.mouse_position);
        chrome.title_hovered = contains(chrome.title_bar, input.mouse_position);
        chrome.dock_hovered = options.allow_dock && contains(chrome.dock_button, input.mouse_position);
        chrome.float_hovered = options.allow_float && contains(chrome.float_button, input.mouse_position);
        chrome.detach_hovered = options.allow_detach && contains(chrome.detach_button, input.mouse_position);
        chrome.close_hovered = options.allow_close && contains(chrome.close_button, input.mouse_position);
        return chrome;
    }

    DockableWindowResult DockableWindowController::update(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) const noexcept
    {
        normalize(host, state, options);

        DockableWindowResult result{};
        result.mode = state.mode;
        result.dock_slot = state.dock_slot;
        if (!state.visible)
        {
            result.chrome = make_chrome(state, options, input);
            return result;
        }

        DockableWindowAction action = input.requested_action;
        DockableWindowChrome chrome = make_chrome(state, options, input);
        if (action == DockableWindowAction::none && input.mouse_pressed)
            action = action_from_hit(chrome, options, input.mouse_position);

        if (action != DockableWindowAction::none)
            apply_action(host, state, options, action, input.requested_dock_slot, result);

        if ((state.mode == DockableWindowMode::floating || state.mode == DockableWindowMode::detached)
            && state.visible)
        {
            const bool chromeActionPressed =
                input.mouse_pressed
                && action_from_hit(chrome, options, input.mouse_position) != DockableWindowAction::focus
                && action_from_hit(chrome, options, input.mouse_position) != DockableWindowAction::none;
            FloatingWindowInput floatingInput = {
                .mouse_position = input.mouse_position,
                .mouse_down = input.mouse_down,
                .mouse_pressed = input.mouse_pressed && !chromeActionPressed,
                .mouse_released = input.mouse_released
            };
            FloatingWindowState before = state.floating;
            const FloatingWindowLayout floating = update_floating_window(
                state.floating,
                options.floating,
                floatingInput);
            if (floating.focused)
            {
                focus(host, state);
                result.focused = true;
            }
            if (floating.moved || floating.resized || before.open != state.floating.open)
            {
                result.changed = true;
                host.changed_this_frame = true;
            }
            if (floating.close_requested)
            {
                state.visible = false;
                state.close_requested = true;
                result.close_requested = true;
                result.changed = true;
                host.changed_this_frame = true;
            }
        }

        result.mode = state.mode;
        result.dock_slot = state.dock_slot;
        result.chrome = make_chrome(state, options, input);
        result.chrome.active = state.active;
        return result;
    }

    const DockableWindowController& dockable_window_controller() noexcept
    {
        static const DockableWindowController controller{};
        return controller;
    }

    void focus_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state) noexcept
    {
        dockable_window_controller().focus(host, state);
    }

    void normalize_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options) noexcept
    {
        dockable_window_controller().normalize(host, state, options);
    }

    DockableWindowChrome make_dockable_window_chrome(
        const DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) noexcept
    {
        return dockable_window_controller().make_chrome(state, options, input);
    }

    DockableWindowResult update_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) noexcept
    {
        return dockable_window_controller().update(host, state, options, input);
    }
}
