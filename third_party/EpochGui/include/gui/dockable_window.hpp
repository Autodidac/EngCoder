#pragma once

#include "dock_layout.hpp"

#include <cstdint>
#include <string_view>

namespace epochengine::gui_lib
{
    enum class DockableWindowMode : std::uint8_t
    {
        docked,
        floating,
        detached
    };

    enum class DockableWindowAction : std::uint8_t
    {
        none,
        focus,
        dock,
        float_window,
        detach,
        close
    };

    struct DockableWindowHostState
    {
        std::uint32_t active_window_id{};
        std::uint32_t next_focus_order{ 1 };
        bool changed_this_frame{};
    };

    struct DockableWindowState
    {
        std::uint32_t id{};
        DockableWindowMode mode{ DockableWindowMode::floating };
        DockSlot dock_slot{ DockSlot::right };
        FloatingWindowState floating{};
        bool visible{ true };
        bool initialized{};
        bool active{};
        bool detach_requested{};
        bool close_requested{};
        std::uint32_t focus_order{};
    };

    struct DockableWindowOptions
    {
        std::string_view title{};
        Rect docked_frame{};
        FloatingWindowOptions floating{};
        Vec2 viewport_size{};
        float title_bar_height{ 30.0f };
        float content_padding{ 6.0f };
        float action_button_width{ 72.0f };
        float action_button_gap{ 4.0f };
        bool allow_dock{ true };
        bool allow_float{ true };
        bool allow_detach{ true };
        bool allow_close{ true };
        DockSlot fallback_dock_slot{ DockSlot::right };
    };

    struct DockableWindowInput
    {
        Vec2 mouse_position{};
        bool mouse_down{};
        bool mouse_pressed{};
        bool mouse_released{};
        DockableWindowAction requested_action{ DockableWindowAction::none };
        DockSlot requested_dock_slot{ DockSlot::none };
    };

    struct DockableWindowChrome
    {
        Rect frame{};
        Rect title_bar{};
        Rect content{};
        Rect dock_button{};
        Rect float_button{};
        Rect detach_button{};
        Rect close_button{};
        bool visible{};
        bool hovered{};
        bool title_hovered{};
        bool dock_hovered{};
        bool float_hovered{};
        bool detach_hovered{};
        bool close_hovered{};
        bool active{};
    };

    struct DockableWindowResult
    {
        DockableWindowChrome chrome{};
        DockableWindowMode mode{ DockableWindowMode::floating };
        DockableWindowAction action{ DockableWindowAction::none };
        DockSlot dock_slot{ DockSlot::none };
        bool changed{};
        bool focused{};
        bool dock_requested{};
        bool float_requested{};
        bool detach_requested{};
        bool close_requested{};
    };

    class DockableWindowController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void focus(
            DockableWindowHostState& host,
            DockableWindowState& state) const noexcept;
        void normalize(
            DockableWindowHostState& host,
            DockableWindowState& state,
            const DockableWindowOptions& options) const noexcept;
        [[nodiscard]] DockableWindowChrome make_chrome(
            const DockableWindowState& state,
            const DockableWindowOptions& options,
            const DockableWindowInput& input) const noexcept;
        [[nodiscard]] DockableWindowResult update(
            DockableWindowHostState& host,
            DockableWindowState& state,
            const DockableWindowOptions& options,
            const DockableWindowInput& input) const noexcept;
    };

    [[nodiscard]] const DockableWindowController& dockable_window_controller() noexcept;
    void focus_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state) noexcept;
    void normalize_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options) noexcept;
    [[nodiscard]] DockableWindowChrome make_dockable_window_chrome(
        const DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) noexcept;
    [[nodiscard]] DockableWindowResult update_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) noexcept;
}
