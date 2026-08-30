#pragma once

#include "floating_window.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace epochengine::gui_lib
{
    inline constexpr std::size_t invalid_text_index = (std::numeric_limits<std::size_t>::max)();

    enum class TextControlCommand : std::uint8_t
    {
        none,
        set_caret,
        move_left,
        move_right,
        move_up,
        move_down,
        move_line_start,
        move_line_end,
        move_document_start,
        move_document_end,
        move_word_left,
        move_word_right,
        select_all,
        erase_backward,
        erase_forward,
        insert_text,
        copy_selection,
        cut_selection,
        paste_text
    };

    struct TextSelectionRange
    {
        std::size_t first{};
        std::size_t past_last{};

        [[nodiscard]] bool empty() const noexcept
        {
            return first == past_last;
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return past_last - first;
        }
    };

    struct TextPosition
    {
        std::size_t byte_index{};
        std::size_t line{};
        std::size_t column{};
    };

    struct TextControlState
    {
        std::string text{};
        std::size_t anchor{};
        std::size_t caret{};
        std::size_t preferred_column{ invalid_text_index };
        Vec2 scroll{};
        bool focused{};
        bool changed_this_frame{};
    };

    struct TextControlOptions
    {
        Vec2 viewport_size{};
        Vec2 content_padding{ 4.0f, 4.0f };
        std::size_t maximum_bytes{};
        bool multiline{ true };
        bool read_only{};
        bool accept_tab{};
    };

    struct TextControlMetrics
    {
        Vec2 content_size{};
        Vec2 caret_position{};
        Vec2 caret_size{ 1.0f, 20.0f };
        bool valid{};
    };

    struct TextControlInput
    {
        TextControlCommand command{ TextControlCommand::none };
        std::string_view text{};
        std::size_t requested_caret{ invalid_text_index };
        Vec2 scroll_delta{};
        TextControlMetrics metrics{};
        bool extend_selection{};
        bool focus_requested{};
        bool blur_requested{};
    };

    struct TextControlResult
    {
        TextSelectionRange selection{};
        TextPosition caret{};
        std::string clipboard_text{};
        Vec2 scroll{};
        bool changed{};
        bool text_changed{};
        bool selection_changed{};
        bool focus_changed{};
        bool scroll_changed{};
        bool clipboard_write_requested{};
    };

    class TextControlController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void normalize(TextControlState& state) const noexcept;
        [[nodiscard]] TextSelectionRange selection(const TextControlState& state) const noexcept;
        [[nodiscard]] TextPosition position(const TextControlState& state) const noexcept;
        [[nodiscard]] std::string selected_text(const TextControlState& state) const;
        [[nodiscard]] bool replace_selection(
            TextControlState& state,
            const TextControlOptions& options,
            std::string_view text) const;
        void update_scroll(
            TextControlState& state,
            const TextControlOptions& options,
            const TextControlMetrics& metrics,
            Vec2 scroll_delta = {}) const noexcept;
        [[nodiscard]] TextControlResult update(
            TextControlState& state,
            const TextControlOptions& options,
            const TextControlInput& input) const;
    };

    [[nodiscard]] const TextControlController& text_control_controller() noexcept;
    void normalize_text_control(TextControlState& state) noexcept;
    [[nodiscard]] TextSelectionRange text_selection(const TextControlState& state) noexcept;
    [[nodiscard]] TextPosition text_position(const TextControlState& state) noexcept;
    [[nodiscard]] std::string selected_text(const TextControlState& state);
    [[nodiscard]] bool replace_text_selection(
        TextControlState& state,
        const TextControlOptions& options,
        std::string_view text);
    void update_text_control_scroll(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlMetrics& metrics,
        Vec2 scroll_delta = {}) noexcept;
    [[nodiscard]] TextControlResult update_text_control(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlInput& input);
}
