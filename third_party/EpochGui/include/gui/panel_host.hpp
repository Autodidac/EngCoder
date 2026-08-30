#pragma once

#include "dockable_window.hpp"
#include "popup_layout.hpp"

#include <cstdint>
#include <string_view>

namespace epochengine::gui_lib
{
    enum class PanelHostMode : std::uint8_t
    {
        docked,
        floating,
        popup,
        external_host
    };

    enum class PanelHostAction : std::uint8_t
    {
        none,
        focus,
        dock,
        float_panel,
        show_popup,
        request_external_host,
        redock_from_external_host,
        close
    };

    struct PanelHostState
    {
        std::uint32_t id{};
        PanelHostMode mode{ PanelHostMode::docked };
        DockSlot dock_slot{ DockSlot::right };
        Rect docked_frame{};
        FloatingWindowState floating{};
        PopupState popup{};
        Rect external_frame{};
        bool visible{ true };
        bool initialized{};
        bool active{};
        bool external_host_requested{};
        bool external_host_active{};
        std::uint64_t external_host_token{};
        std::uint32_t focus_order{};
    };

    struct PanelHostOptions
    {
        std::string_view title{};
        Rect docked_frame{};
        FloatingWindowOptions floating{};
        PopupOptions popup{};
        Rect default_external_frame{};
        DockSlot fallback_dock_slot{ DockSlot::right };
        bool allow_dock{ true };
        bool allow_float{ true };
        bool allow_popup{ true };
        bool allow_external_host{ true };
        bool allow_close{ true };
    };

    struct PanelHostInput
    {
        DockableWindowHostState* focus_host{};
        Vec2 mouse_position{};
        bool mouse_down{};
        bool mouse_pressed{};
        bool mouse_released{};
        PanelHostAction requested_action{ PanelHostAction::none };
        DockSlot requested_dock_slot{ DockSlot::none };
        std::uint64_t external_host_token{};
        bool external_host_confirmed{};
        bool external_host_closed{};
        bool escape_pressed{};
    };

    struct PanelHostMetadata
    {
        std::uint32_t id{};
        PanelHostMode mode{ PanelHostMode::docked };
        DockSlot dock_slot{ DockSlot::none };
        Rect frame{};
        bool visible{};
        bool active{};
        bool wants_external_host{};
        bool external_host_active{};
        std::uint64_t external_host_token{};
    };

    struct PanelHostResult
    {
        PanelHostMetadata metadata{};
        PanelHostAction action{ PanelHostAction::none };
        bool changed{};
        bool focus_changed{};
        bool placement_changed{};
        bool external_host_changed{};
        bool close_requested{};
    };

    class PanelHostController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void focus(PanelHostState& state, DockableWindowHostState* host = nullptr) const noexcept;
        void normalize(PanelHostState& state, const PanelHostOptions& options) const noexcept;
        [[nodiscard]] PanelHostMetadata metadata(const PanelHostState& state) const noexcept;
        [[nodiscard]] PanelHostResult update(
            PanelHostState& state,
            const PanelHostOptions& options,
            const PanelHostInput& input) const noexcept;
    };

    [[nodiscard]] const PanelHostController& panel_host_controller() noexcept;
    void focus_panel_host(
        PanelHostState& state,
        DockableWindowHostState* host = nullptr) noexcept;
    void normalize_panel_host(
        PanelHostState& state,
        const PanelHostOptions& options) noexcept;
    [[nodiscard]] PanelHostMetadata panel_host_metadata(
        const PanelHostState& state) noexcept;
    [[nodiscard]] PanelHostResult update_panel_host(
        PanelHostState& state,
        const PanelHostOptions& options,
        const PanelHostInput& input) noexcept;
}
