#pragma once

#include "floating_window.hpp"

#include <cstdint>

namespace epochengine::gui_lib
{
    enum class PopupPlacement : std::uint8_t
    {
        below,
        above,
        right,
        left,
        centered,
        cursor
    };

    struct PopupState
    {
        Rect rect{};
        PopupPlacement placement{ PopupPlacement::below };
        bool open{};
        bool initialized{};
        bool pressed_inside{};
        bool pressed_owner{};
        std::uint32_t focus_order{};
    };

    struct PopupOptions
    {
        Rect owner{};
        Vec2 preferred_size{ 240.0f, 160.0f };
        Vec2 viewport_size{};
        Vec2 cursor_offset{ 12.0f, 12.0f };
        PopupPlacement placement{ PopupPlacement::below };
        float gap{ 4.0f };
        float margin{ 4.0f };
        bool flip_to_fit{ true };
        bool clamp_to_viewport{ true };
        bool close_on_outside_press{ true };
    };

    struct PopupInput
    {
        Vec2 mouse_position{};
        bool mouse_pressed{};
        bool mouse_released{};
        bool open_requested{};
        bool toggle_requested{};
        bool close_requested{};
        bool owner_pressed{};
        bool escape_pressed{};
    };

    struct PopupLayout
    {
        Rect popup{};
        Rect owner{};
        PopupPlacement placement{ PopupPlacement::below };
        bool visible{};
        bool hovered{};
        bool owner_hovered{};
        bool opened{};
        bool closed{};
        bool flipped{};
        bool clamped{};
    };

    class PopupLayoutController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] Rect place(
            const PopupOptions& options,
            const PopupInput& input,
            PopupPlacement* used_placement = nullptr,
            bool* flipped = nullptr,
            bool* clamped = nullptr) const noexcept;
        void normalize(PopupState& state, const PopupOptions& options, const PopupInput& input) const noexcept;
        [[nodiscard]] PopupLayout update(PopupState& state, const PopupOptions& options, const PopupInput& input) const noexcept;
    };

    [[nodiscard]] const PopupLayoutController& popup_layout_controller() noexcept;
    [[nodiscard]] Rect place_popup(
        const PopupOptions& options,
        const PopupInput& input,
        PopupPlacement* used_placement = nullptr,
        bool* flipped = nullptr,
        bool* clamped = nullptr) noexcept;
    void normalize_popup(
        PopupState& state,
        const PopupOptions& options,
        const PopupInput& input) noexcept;
    [[nodiscard]] PopupLayout update_popup(
        PopupState& state,
        const PopupOptions& options,
        const PopupInput& input) noexcept;
}
