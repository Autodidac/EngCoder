module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

module epoch.gui.input;

namespace epochengine::gui_lib::input
{
    namespace
    {
        [[nodiscard]] constexpr std::size_t pointer_index(PointerButton button) noexcept
        {
            const auto index = static_cast<std::size_t>(button);
            const auto count = static_cast<std::size_t>(PointerButton::count);
            return index < count ? index : 0U;
        }

        [[nodiscard]] constexpr std::size_t key_index(Key key_code) noexcept
        {
            const auto index = static_cast<std::size_t>(key_code);
            const auto count = static_cast<std::size_t>(Key::count);
            return index < count ? index : 0U;
        }

        [[nodiscard]] float finite_or(float value, float fallback) noexcept
        {
            return std::isfinite(value) ? value : fallback;
        }

        [[nodiscard]] float positive_or(float value, float fallback) noexcept
        {
            const float sane = finite_or(value, fallback);
            return sane > 0.0f ? sane : fallback;
        }

        [[nodiscard]] Rect sanitize_rect(Rect rect) noexcept
        {
            rect.position.x = finite_or(rect.position.x, 0.0f);
            rect.position.y = finite_or(rect.position.y, 0.0f);
            rect.size.x = (std::max)(0.0f, finite_or(rect.size.x, 0.0f));
            rect.size.y = (std::max)(0.0f, finite_or(rect.size.y, 0.0f));
            return rect;
        }

        [[nodiscard]] Rect make_rect(float x, float y, float width, float height) noexcept
        {
            return Rect{
                Vec2{ x, y },
                Vec2{ (std::max)(0.0f, width), (std::max)(0.0f, height) }
            };
        }

        [[nodiscard]] bool in_horizontal_band(Rect bounds, Vec2 point, float thickness, bool left) noexcept
        {
            if (left)
                return point.x >= bounds.position.x && point.x < bounds.position.x + thickness;
            return point.x <= bounds.position.x + bounds.size.x
                && point.x > bounds.position.x + bounds.size.x - thickness;
        }

        [[nodiscard]] bool in_vertical_band(Rect bounds, Vec2 point, float thickness, bool top) noexcept
        {
            if (top)
                return point.y >= bounds.position.y && point.y < bounds.position.y + thickness;
            return point.y <= bounds.position.y + bounds.size.y
                && point.y > bounds.position.y + bounds.size.y - thickness;
        }
    }

    const DigitalState& InputFrame::pointer(PointerButton button) const noexcept
    {
        return pointer_buttons[pointer_index(button)];
    }

    const DigitalState& InputFrame::key(Key key_code) const noexcept
    {
        return keys[key_index(key_code)];
    }

    void InputTracker::reset() noexcept
    {
        frame_ = {};
    }

    void InputTracker::finish_frame() noexcept
    {
        frame_.pointer_delta = {};
        frame_.wheel_delta = {};

        for (DigitalState& state : frame_.pointer_buttons)
        {
            state.pressed = false;
            state.released = false;
            state.repeated = false;
            state.click_count = 0;
        }

        for (DigitalState& state : frame_.keys)
        {
            state.pressed = false;
            state.released = false;
            state.repeated = false;
            state.click_count = 0;
        }

        ++frame_.sequence;
    }

    void InputTracker::set_pointer_position(Vec2 position) noexcept
    {
        position.x = finite_or(position.x, frame_.pointer_position.x);
        position.y = finite_or(position.y, frame_.pointer_position.y);
        frame_.pointer_delta.x += position.x - frame_.pointer_position.x;
        frame_.pointer_delta.y += position.y - frame_.pointer_position.y;
        frame_.pointer_position = position;
    }

    void InputTracker::add_wheel_delta(Vec2 delta) noexcept
    {
        frame_.wheel_delta.x += finite_or(delta.x, 0.0f);
        frame_.wheel_delta.y += finite_or(delta.y, 0.0f);
    }

    void InputTracker::set_pointer_button(
        PointerButton button,
        bool down,
        std::uint8_t click_count) noexcept
    {
        DigitalState& state = frame_.pointer_buttons[pointer_index(button)];
        if (down != state.down)
        {
            state.pressed = down;
            state.released = !down;
            state.down = down;
        }
        if (down)
            state.click_count = (std::max)(state.click_count, click_count);
    }

    void InputTracker::set_key(Key key_code, bool down, bool repeated) noexcept
    {
        DigitalState& state = frame_.keys[key_index(key_code)];
        if (down != state.down)
        {
            state.pressed = down;
            state.released = !down;
            state.down = down;
        }
        state.repeated = repeated;
    }

    void InputTracker::set_modifiers(ModifierState modifiers) noexcept
    {
        frame_.modifiers = modifiers;
    }

    const InputFrame& InputTracker::frame() const noexcept
    {
        return frame_;
    }

    void ModalInputArbiter::begin(Rect bounds) noexcept
    {
        bounds_ = sanitize_rect(bounds);
        mode_ = bounds_.size.x > 0.0f && bounds_.size.y > 0.0f
            ? ModalInputMode::bounded
            : ModalInputMode::inactive;
    }

    void ModalInputArbiter::block_until_clear() noexcept
    {
        bounds_ = {};
        mode_ = ModalInputMode::blocked_until_clear;
    }

    void ModalInputArbiter::clear() noexcept
    {
        bounds_ = {};
        mode_ = ModalInputMode::inactive;
    }

    ModalInputMode ModalInputArbiter::mode() const noexcept
    {
        return mode_;
    }

    Rect ModalInputArbiter::bounds() const noexcept
    {
        return bounds_;
    }

    bool ModalInputArbiter::active() const noexcept
    {
        return mode_ != ModalInputMode::inactive;
    }

    bool ModalInputArbiter::keyboard_captured() const noexcept
    {
        return active();
    }

    bool ModalInputArbiter::pointer_allowed(Vec2 position) const noexcept
    {
        switch (mode_)
        {
        case ModalInputMode::inactive:
            return true;
        case ModalInputMode::bounded:
            return contains(bounds_, position);
        case ModalInputMode::blocked_until_clear:
            return false;
        }
        return false;
    }

    bool ModalInputArbiter::background_input_allowed() const noexcept
    {
        return mode_ == ModalInputMode::inactive;
    }

    ContextMenuRequest context_menu_request(
        const InputFrame& frame,
        Rect region,
        bool trigger_on_press) noexcept
    {
        const DigitalState& right = frame.pointer(PointerButton::right);
        const bool transition = trigger_on_press ? right.pressed : right.released;
        return ContextMenuRequest{
            .position = frame.pointer_position,
            .requested = transition && contains(region, frame.pointer_position)
        };
    }

    FloatingWindowInput floating_window_input(const InputFrame& frame) noexcept
    {
        const DigitalState& left = frame.pointer(PointerButton::left);
        return FloatingWindowInput{
            .mouse_position = frame.pointer_position,
            .mouse_down = left.down,
            .mouse_pressed = left.pressed,
            .mouse_released = left.released
        };
    }

    PopupInput popup_input(
        const InputFrame& frame,
        bool open_requested,
        bool toggle_requested,
        bool close_requested,
        bool owner_pressed) noexcept
    {
        const DigitalState& left = frame.pointer(PointerButton::left);
        return PopupInput{
            .mouse_position = frame.pointer_position,
            .mouse_pressed = left.pressed,
            .mouse_released = left.released,
            .open_requested = open_requested,
            .toggle_requested = toggle_requested,
            .close_requested = close_requested,
            .owner_pressed = owner_pressed,
            .escape_pressed = frame.key(Key::escape).pressed
        };
    }

    BorderlessWindowChromeLayout make_borderless_window_chrome_layout(
        const BorderlessWindowChromeOptions& options) noexcept
    {
        BorderlessWindowChromeLayout layout{};
        layout.bounds = sanitize_rect(options.bounds);
        if (layout.bounds.size.x <= 0.0f || layout.bounds.size.y <= 0.0f)
            return layout;

        const float title_height = (std::min)(
            positive_or(options.title_bar_height, 42.0f),
            layout.bounds.size.y);
        const float border = (std::min)(
            positive_or(options.resize_border, 7.0f),
            0.5f * (std::min)(layout.bounds.size.x, layout.bounds.size.y));
        const float button_width = positive_or(options.button_width, 46.0f);
        const float right = layout.bounds.position.x + layout.bounds.size.x;
        const float top = layout.bounds.position.y;

        float button_right = right;
        if (options.closable)
        {
            layout.close_button = make_rect(button_right - button_width, top, button_width, title_height);
            button_right -= button_width;
        }
        if (options.maximizable)
        {
            layout.maximize_button = make_rect(button_right - button_width, top, button_width, title_height);
            button_right -= button_width;
        }
        if (options.minimizable)
        {
            layout.minimize_button = make_rect(button_right - button_width, top, button_width, title_height);
            button_right -= button_width;
        }

        layout.title_bar = make_rect(
            layout.bounds.position.x,
            top,
            layout.bounds.size.x,
            title_height);
        layout.caption = make_rect(
            layout.bounds.position.x + (std::max)(0.0f, finite_or(options.caption_padding_left, 12.0f)),
            top,
            (std::max)(0.0f, button_right - layout.bounds.position.x
                - (std::max)(0.0f, finite_or(options.caption_padding_left, 12.0f))),
            title_height);
        layout.client = make_rect(
            layout.bounds.position.x,
            top + title_height,
            layout.bounds.size.x,
            layout.bounds.size.y - title_height);
        layout.resize_border = border;
        layout.movable = options.movable;
        layout.resizable = options.resizable;
        layout.valid = true;
        return layout;
    }

    WindowChromeRegion hit_test_borderless_window_chrome(
        const BorderlessWindowChromeLayout& layout,
        Vec2 point) noexcept
    {
        if (!layout.valid || !contains(layout.bounds, point))
            return WindowChromeRegion::outside;

        if (layout.resizable)
        {
            const bool left = in_horizontal_band(layout.bounds, point, layout.resize_border, true);
            const bool right = in_horizontal_band(layout.bounds, point, layout.resize_border, false);
            const bool top = in_vertical_band(layout.bounds, point, layout.resize_border, true);
            const bool bottom = in_vertical_band(layout.bounds, point, layout.resize_border, false);

            if (left && top) return WindowChromeRegion::resize_top_left;
            if (right && top) return WindowChromeRegion::resize_top_right;
            if (left && bottom) return WindowChromeRegion::resize_bottom_left;
            if (right && bottom) return WindowChromeRegion::resize_bottom_right;
            if (left) return WindowChromeRegion::resize_left;
            if (right) return WindowChromeRegion::resize_right;
            if (top) return WindowChromeRegion::resize_top;
            if (bottom) return WindowChromeRegion::resize_bottom;
        }

        if (layout.close_button.size.x > 0.0f && contains(layout.close_button, point))
            return WindowChromeRegion::close_button;
        if (layout.maximize_button.size.x > 0.0f && contains(layout.maximize_button, point))
            return WindowChromeRegion::maximize_button;
        if (layout.minimize_button.size.x > 0.0f && contains(layout.minimize_button, point))
            return WindowChromeRegion::minimize_button;
        if (layout.movable && contains(layout.caption, point))
            return WindowChromeRegion::caption;
        return WindowChromeRegion::client;
    }

    WindowCommand borderless_window_command(
        const BorderlessWindowChromeLayout& layout,
        const InputFrame& frame) noexcept
    {
        if (!frame.pointer(PointerButton::left).released)
            return WindowCommand::none;

        switch (hit_test_borderless_window_chrome(layout, frame.pointer_position))
        {
        case WindowChromeRegion::minimize_button:
            return WindowCommand::minimize;
        case WindowChromeRegion::maximize_button:
            return WindowCommand::toggle_maximize;
        case WindowChromeRegion::close_button:
            return WindowCommand::close;
        default:
            return WindowCommand::none;
        }
    }
}
