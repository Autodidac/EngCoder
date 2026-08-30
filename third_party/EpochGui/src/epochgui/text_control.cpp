module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

module epoch.gui;

namespace epochengine::gui_lib
{
    namespace
    {
        enum class CharacterClass : std::uint8_t
        {
            whitespace,
            word,
            punctuation
        };

        [[nodiscard]] bool is_continuation_byte(unsigned char value) noexcept
        {
            return (value & 0xc0U) == 0x80U;
        }

        [[nodiscard]] std::size_t clamp_text_index(std::string_view text, std::size_t index) noexcept
        {
            index = (std::min)(index, text.size());
            while (index > 0 && index < text.size()
                && is_continuation_byte(static_cast<unsigned char>(text[index])))
            {
                --index;
            }
            return index;
        }

        [[nodiscard]] std::size_t previous_text_index(std::string_view text, std::size_t index) noexcept
        {
            index = clamp_text_index(text, index);
            if (index == 0)
                return 0;

            --index;
            while (index > 0 && is_continuation_byte(static_cast<unsigned char>(text[index])))
                --index;
            return index;
        }

        [[nodiscard]] std::size_t next_text_index(std::string_view text, std::size_t index) noexcept
        {
            index = clamp_text_index(text, index);
            if (index >= text.size())
                return text.size();

            ++index;
            while (index < text.size() && is_continuation_byte(static_cast<unsigned char>(text[index])))
                ++index;
            return index;
        }

        [[nodiscard]] CharacterClass character_class(std::string_view text, std::size_t index) noexcept
        {
            if (index >= text.size())
                return CharacterClass::whitespace;

            const unsigned char value = static_cast<unsigned char>(text[index]);
            if (value >= 0x80U
                || (value >= static_cast<unsigned char>('a') && value <= static_cast<unsigned char>('z'))
                || (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z'))
                || (value >= static_cast<unsigned char>('0') && value <= static_cast<unsigned char>('9'))
                || value == static_cast<unsigned char>('_'))
            {
                return CharacterClass::word;
            }
            if (value <= static_cast<unsigned char>(' ') || value == 0x7fU)
                return CharacterClass::whitespace;
            return CharacterClass::punctuation;
        }

        [[nodiscard]] std::size_t word_left_index(std::string_view text, std::size_t index) noexcept
        {
            index = clamp_text_index(text, index);
            while (index > 0)
            {
                const std::size_t previous = previous_text_index(text, index);
                if (character_class(text, previous) != CharacterClass::whitespace)
                    break;
                index = previous;
            }
            if (index == 0)
                return 0;

            const CharacterClass target = character_class(text, previous_text_index(text, index));
            while (index > 0)
            {
                const std::size_t previous = previous_text_index(text, index);
                if (character_class(text, previous) != target)
                    break;
                index = previous;
            }
            return index;
        }

        [[nodiscard]] std::size_t word_right_index(std::string_view text, std::size_t index) noexcept
        {
            index = clamp_text_index(text, index);
            if (index < text.size())
            {
                const CharacterClass target = character_class(text, index);
                while (index < text.size() && character_class(text, index) == target)
                    index = next_text_index(text, index);
            }
            while (index < text.size() && character_class(text, index) == CharacterClass::whitespace)
                index = next_text_index(text, index);
            return index;
        }

        [[nodiscard]] std::size_t line_start_index(std::string_view text, std::size_t index) noexcept
        {
            index = clamp_text_index(text, index);
            if (index == 0)
                return 0;
            const std::size_t newline = text.rfind('\n', index - 1);
            return newline == std::string_view::npos ? 0 : newline + 1;
        }

        [[nodiscard]] std::size_t line_end_index(std::string_view text, std::size_t index) noexcept
        {
            index = clamp_text_index(text, index);
            const std::size_t newline = text.find('\n', index);
            return newline == std::string_view::npos ? text.size() : newline;
        }

        [[nodiscard]] std::size_t column_at(std::string_view text, std::size_t index) noexcept
        {
            const std::size_t start = line_start_index(text, index);
            std::size_t column = 0;
            for (std::size_t cursor = start; cursor < index; cursor = next_text_index(text, cursor))
                ++column;
            return column;
        }

        [[nodiscard]] std::size_t index_at_column(
            std::string_view text,
            std::size_t line_start,
            std::size_t line_end,
            std::size_t column) noexcept
        {
            std::size_t index = line_start;
            while (index < line_end && column > 0)
            {
                index = next_text_index(text, index);
                --column;
            }
            return index;
        }

        [[nodiscard]] std::size_t vertical_move_index(
            std::string_view text,
            std::size_t caret,
            std::size_t column,
            bool down) noexcept
        {
            const std::size_t current_start = line_start_index(text, caret);
            const std::size_t current_end = line_end_index(text, caret);
            if (down)
            {
                if (current_end >= text.size())
                    return text.size();
                const std::size_t next_start = current_end + 1;
                return index_at_column(text, next_start, line_end_index(text, next_start), column);
            }

            if (current_start == 0)
                return 0;
            const std::size_t previous_end = current_start - 1;
            const std::size_t previous_start = line_start_index(text, previous_end);
            return index_at_column(text, previous_start, previous_end, column);
        }

        void move_caret(TextControlState& state, std::size_t target, bool extend_selection) noexcept
        {
            target = clamp_text_index(state.text, target);
            state.caret = target;
            if (!extend_selection)
                state.anchor = target;
        }

        [[nodiscard]] std::string filter_inserted_text(
            std::string_view input,
            const TextControlOptions& options)
        {
            std::string filtered;
            filtered.reserve(input.size());
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                const char value = input[index];
                if (value == '\r')
                {
                    if (index + 1 < input.size() && input[index + 1] == '\n')
                        ++index;
                    if (options.multiline)
                        filtered.push_back('\n');
                    continue;
                }
                if (value == '\n')
                {
                    if (options.multiline)
                        filtered.push_back(value);
                    continue;
                }
                if (value == '\t')
                {
                    if (options.accept_tab)
                        filtered.push_back(value);
                    continue;
                }
                const unsigned char byte = static_cast<unsigned char>(value);
                if (byte < 0x20U || byte == 0x7fU)
                    continue;
                filtered.push_back(value);
            }
            return filtered;
        }

        void limit_inserted_text(std::string& text, std::size_t maximum_bytes) noexcept
        {
            if (text.size() <= maximum_bytes)
                return;

            std::size_t prefix = maximum_bytes;
            while (prefix > 0 && prefix < text.size()
                && is_continuation_byte(static_cast<unsigned char>(text[prefix])))
            {
                --prefix;
            }
            text.resize(prefix);
        }

        [[nodiscard]] float finite_nonnegative(float value) noexcept
        {
            return std::isfinite(value) ? (std::max)(0.0f, value) : 0.0f;
        }

        [[nodiscard]] bool different(Vec2 left, Vec2 right) noexcept
        {
            return left.x != right.x || left.y != right.y;
        }
    }

    std::string_view TextControlController::name() const noexcept
    {
        return "text_control";
    }

    void TextControlController::normalize(TextControlState& state) const noexcept
    {
        state.anchor = clamp_text_index(state.text, state.anchor);
        state.caret = clamp_text_index(state.text, state.caret);
        state.scroll.x = finite_nonnegative(state.scroll.x);
        state.scroll.y = finite_nonnegative(state.scroll.y);
    }

    TextSelectionRange TextControlController::selection(const TextControlState& state) const noexcept
    {
        const std::size_t anchor = clamp_text_index(state.text, state.anchor);
        const std::size_t caret = clamp_text_index(state.text, state.caret);
        return TextSelectionRange{ (std::min)(anchor, caret), (std::max)(anchor, caret) };
    }

    TextPosition TextControlController::position(const TextControlState& state) const noexcept
    {
        const std::size_t caret = clamp_text_index(state.text, state.caret);
        std::size_t line = 0;
        for (std::size_t index = 0; index < caret; ++index)
        {
            if (state.text[index] == '\n')
                ++line;
        }
        return TextPosition{ caret, line, column_at(state.text, caret) };
    }

    std::string TextControlController::selected_text(const TextControlState& state) const
    {
        const TextSelectionRange range = selection(state);
        return state.text.substr(range.first, range.size());
    }

    bool TextControlController::replace_selection(
        TextControlState& state,
        const TextControlOptions& options,
        std::string_view text) const
    {
        normalize(state);
        if (options.read_only)
            return false;

        const TextSelectionRange range = selection(state);
        std::string inserted = filter_inserted_text(text, options);
        if (options.maximum_bytes > 0)
        {
            const std::size_t retained = state.text.size() - range.size();
            const std::size_t available = retained < options.maximum_bytes
                ? options.maximum_bytes - retained
                : 0;
            limit_inserted_text(inserted, available);
        }

        if (range.empty() && inserted.empty())
            return false;

        state.text.replace(range.first, range.size(), inserted);
        state.caret = range.first + inserted.size();
        state.anchor = state.caret;
        state.preferred_column = invalid_text_index;
        state.changed_this_frame = true;
        return true;
    }

    void TextControlController::update_scroll(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlMetrics& metrics,
        Vec2 scroll_delta) const noexcept
    {
        normalize(state);
        state.scroll.x = finite_nonnegative(state.scroll.x + scroll_delta.x);
        state.scroll.y = finite_nonnegative(state.scroll.y + scroll_delta.y);

        if (!metrics.valid)
        {
            if (!options.multiline)
                state.scroll.y = 0.0f;
            return;
        }

        const float viewport_width = finite_nonnegative(
            options.viewport_size.x - 2.0f * finite_nonnegative(options.content_padding.x));
        const float viewport_height = finite_nonnegative(
            options.viewport_size.y - 2.0f * finite_nonnegative(options.content_padding.y));
        const float content_width = finite_nonnegative(
            metrics.content_size.x + 2.0f * finite_nonnegative(options.content_padding.x));
        const float content_height = finite_nonnegative(
            metrics.content_size.y + 2.0f * finite_nonnegative(options.content_padding.y));
        const float maximum_x = (std::max)(0.0f, content_width - finite_nonnegative(options.viewport_size.x));
        const float maximum_y = options.multiline
            ? (std::max)(0.0f, content_height - finite_nonnegative(options.viewport_size.y))
            : 0.0f;

        const float caret_left = finite_nonnegative(metrics.caret_position.x);
        const float caret_top = finite_nonnegative(metrics.caret_position.y);
        const float caret_right = caret_left + (std::max)(1.0f, finite_nonnegative(metrics.caret_size.x));
        const float caret_bottom = caret_top + (std::max)(1.0f, finite_nonnegative(metrics.caret_size.y));

        if (caret_left < state.scroll.x)
            state.scroll.x = caret_left;
        else if (viewport_width > 0.0f && caret_right > state.scroll.x + viewport_width)
            state.scroll.x = caret_right - viewport_width;

        if (options.multiline)
        {
            if (caret_top < state.scroll.y)
                state.scroll.y = caret_top;
            else if (viewport_height > 0.0f && caret_bottom > state.scroll.y + viewport_height)
                state.scroll.y = caret_bottom - viewport_height;
        }

        state.scroll.x = std::clamp(state.scroll.x, 0.0f, maximum_x);
        state.scroll.y = std::clamp(state.scroll.y, 0.0f, maximum_y);
    }

    TextControlResult TextControlController::update(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlInput& input) const
    {
        normalize(state);
        state.changed_this_frame = false;
        const TextSelectionRange before_selection = selection(state);
        const Vec2 before_scroll = state.scroll;
        const bool before_focus = state.focused;
        const bool clipboard_requested =
            input.command == TextControlCommand::copy_selection
            || (input.command == TextControlCommand::cut_selection && !options.read_only);
        std::string clipboard;
        if (clipboard_requested && !before_selection.empty())
            clipboard = state.text.substr(before_selection.first, before_selection.size());
        bool text_changed = false;

        if (input.blur_requested)
            state.focused = false;
        else if (input.focus_requested)
            state.focused = true;

        switch (input.command)
        {
        case TextControlCommand::set_caret:
            if (input.requested_caret != invalid_text_index)
                move_caret(state, input.requested_caret, input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::move_left:
        {
            const TextSelectionRange range = selection(state);
            const std::size_t target = !input.extend_selection && !range.empty()
                ? range.first
                : previous_text_index(state.text, state.caret);
            move_caret(state, target, input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        }
        case TextControlCommand::move_right:
        {
            const TextSelectionRange range = selection(state);
            const std::size_t target = !input.extend_selection && !range.empty()
                ? range.past_last
                : next_text_index(state.text, state.caret);
            move_caret(state, target, input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        }
        case TextControlCommand::move_up:
        case TextControlCommand::move_down:
            if (state.preferred_column == invalid_text_index)
                state.preferred_column = column_at(state.text, state.caret);
            move_caret(
                state,
                vertical_move_index(
                    state.text,
                    state.caret,
                    state.preferred_column,
                    input.command == TextControlCommand::move_down),
                input.extend_selection);
            break;
        case TextControlCommand::move_line_start:
            move_caret(state, line_start_index(state.text, state.caret), input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::move_line_end:
            move_caret(state, line_end_index(state.text, state.caret), input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::move_document_start:
            move_caret(state, 0, input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::move_document_end:
            move_caret(state, state.text.size(), input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::move_word_left:
            move_caret(state, word_left_index(state.text, state.caret), input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::move_word_right:
            move_caret(state, word_right_index(state.text, state.caret), input.extend_selection);
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::select_all:
            state.anchor = 0;
            state.caret = state.text.size();
            state.preferred_column = invalid_text_index;
            break;
        case TextControlCommand::erase_backward:
            if (!options.read_only)
            {
                if (selection(state).empty() && state.caret > 0)
                    state.anchor = previous_text_index(state.text, state.caret);
                text_changed = replace_selection(state, options, {});
            }
            break;
        case TextControlCommand::erase_forward:
            if (!options.read_only)
            {
                if (selection(state).empty() && state.caret < state.text.size())
                    state.anchor = next_text_index(state.text, state.caret);
                text_changed = replace_selection(state, options, {});
            }
            break;
        case TextControlCommand::insert_text:
        case TextControlCommand::paste_text:
            text_changed = replace_selection(state, options, input.text);
            break;
        case TextControlCommand::copy_selection:
            break;
        case TextControlCommand::cut_selection:
            if (!options.read_only && !selection(state).empty())
                text_changed = replace_selection(state, options, {});
            break;
        case TextControlCommand::none:
        default:
            break;
        }

        update_scroll(state, options, input.metrics, input.scroll_delta);
        const TextSelectionRange after_selection = selection(state);

        TextControlResult result{};
        result.selection = after_selection;
        result.caret = position(state);
        result.clipboard_text = std::move(clipboard);
        result.scroll = state.scroll;
        result.text_changed = text_changed;
        result.selection_changed = before_selection.first != after_selection.first
            || before_selection.past_last != after_selection.past_last;
        result.focus_changed = before_focus != state.focused;
        result.scroll_changed = different(before_scroll, state.scroll);
        result.clipboard_write_requested = clipboard_requested && !result.clipboard_text.empty();
        result.changed = result.text_changed
            || result.selection_changed
            || result.focus_changed
            || result.scroll_changed;
        state.changed_this_frame = result.text_changed;
        return result;
    }

    const TextControlController& text_control_controller() noexcept
    {
        static const TextControlController controller{};
        return controller;
    }

    void normalize_text_control(TextControlState& state) noexcept
    {
        text_control_controller().normalize(state);
    }

    TextSelectionRange text_selection(const TextControlState& state) noexcept
    {
        return text_control_controller().selection(state);
    }

    TextPosition text_position(const TextControlState& state) noexcept
    {
        return text_control_controller().position(state);
    }

    std::string selected_text(const TextControlState& state)
    {
        return text_control_controller().selected_text(state);
    }

    bool replace_text_selection(
        TextControlState& state,
        const TextControlOptions& options,
        std::string_view text)
    {
        return text_control_controller().replace_selection(state, options, text);
    }

    void update_text_control_scroll(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlMetrics& metrics,
        Vec2 scroll_delta) noexcept
    {
        text_control_controller().update_scroll(state, options, metrics, scroll_delta);
    }

    TextControlResult update_text_control(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlInput& input)
    {
        return text_control_controller().update(state, options, input);
    }
}
