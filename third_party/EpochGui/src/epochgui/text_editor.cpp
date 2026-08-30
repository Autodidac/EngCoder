// SPDX-License-Identifier: LicenseRef-MIT-NoSell
module;

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module epoch.gui;

namespace epochengine::gui_lib
{
    namespace
    {
        [[nodiscard]] std::uint64_t next_revision(std::uint64_t value) noexcept
        {
            return value == (std::numeric_limits<std::uint64_t>::max)() ? 1u : value + 1u;
        }

        void rebuild_line_index(TextEditorState& state)
        {
            state.line_starts.clear();
            state.line_starts.push_back(0u);
            for (std::size_t index = 0u; index < state.text.text.size(); ++index)
            {
                if (state.text.text[index] == '\n')
                    state.line_starts.push_back(index + 1u);
            }
        }

        [[nodiscard]] char folded_ascii(char value) noexcept
        {
            const unsigned char raw = static_cast<unsigned char>(value);
            return raw < 0x80u
                ? static_cast<char>(std::tolower(raw))
                : value;
        }

        [[nodiscard]] bool equal_at(
            std::string_view text,
            std::size_t offset,
            std::string_view query,
            bool caseSensitive) noexcept
        {
            if (offset > text.size() || query.size() > text.size() - offset)
                return false;
            for (std::size_t index = 0u; index < query.size(); ++index)
            {
                const char left = text[offset + index];
                const char right = query[index];
                if (caseSensitive ? left != right : folded_ascii(left) != folded_ascii(right))
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::size_t find_from(
            std::string_view text,
            std::string_view query,
            std::size_t start,
            bool caseSensitive) noexcept
        {
            if (query.empty() || query.size() > text.size())
                return invalid_text_index;
            const std::size_t last = text.size() - query.size();
            for (std::size_t index = (std::min)(start, text.size()); index <= last; ++index)
            {
                if (equal_at(text, index, query, caseSensitive))
                    return index;
            }
            return invalid_text_index;
        }
    }

    std::string_view TextEditorController::name() const noexcept
    {
        return "text_editor";
    }

    void TextEditorController::normalize(TextEditorState& state) const
    {
        normalize_text_control(state.text);
        rebuild_line_index(state);
        state.find_cursor = (std::min)(state.find_cursor, state.text.text.size());
        state.active_match.first = (std::min)(state.active_match.first, state.text.text.size());
        state.active_match.past_last =
            (std::min)((std::max)(state.active_match.first, state.active_match.past_last), state.text.text.size());
    }

    void TextEditorController::set_text(TextEditorState& state, std::string value) const
    {
        if (state.text.text == value)
        {
            normalize(state);
            return;
        }

        state.text.text = std::move(value);
        state.text.anchor = 0u;
        state.text.caret = 0u;
        state.text.preferred_column = invalid_text_index;
        state.text.scroll = {};
        state.find_cursor = 0u;
        state.active_match = {};
        state.revision = next_revision(state.revision);
        state.dirty = false;
        normalize(state);
    }

    TextEditorResult TextEditorController::update(
        TextEditorState& state,
        const TextControlOptions& options,
        const TextControlInput& input) const
    {
        normalize(state);
        TextEditorResult result{};
        result.text = update_text_control(state.text, options, input);
        if (result.text.text_changed)
        {
            state.revision = next_revision(state.revision);
            state.dirty = true;
            state.active_match = {};
            state.find_cursor = (std::min)(state.find_cursor, state.text.text.size());
            rebuild_line_index(state);
            result.document_changed = true;
        }
        result.snapshot = snapshot(state);
        return result;
    }

    TextEditorSnapshot TextEditorController::snapshot(const TextEditorState& state) const noexcept
    {
        return TextEditorSnapshot{
            .selection = text_selection(state.text),
            .caret = text_position(state.text),
            .active_match = state.active_match,
            .line_count = (std::max)(std::size_t{1u}, state.line_starts.size()),
            .revision = state.revision,
            .dirty = state.dirty
        };
    }

    TextEditorLineRange TextEditorController::line_range(
        const TextEditorState& state,
        std::size_t lineIndex) const noexcept
    {
        if (lineIndex >= state.line_starts.size())
            return { state.text.text.size(), state.text.text.size() };

        const std::size_t first = state.line_starts[lineIndex];
        std::size_t pastLast = lineIndex + 1u < state.line_starts.size()
            ? state.line_starts[lineIndex + 1u] - 1u
            : state.text.text.size();
        pastLast = (std::max)(first, (std::min)(pastLast, state.text.text.size()));
        return { first, pastLast };
    }

    std::string_view TextEditorController::line(
        const TextEditorState& state,
        std::size_t lineIndex) const noexcept
    {
        const TextEditorLineRange range = line_range(state, lineIndex);
        return std::string_view{ state.text.text }.substr(range.first, range.size());
    }

    bool TextEditorController::find_next(
        TextEditorState& state,
        std::string_view query,
        bool caseSensitive) const
    {
        normalize(state);
        const bool queryChanged = state.find_query != query;
        state.find_query.assign(query);
        if (query.empty())
        {
            state.active_match = {};
            state.find_cursor = 0u;
            return false;
        }

        std::size_t start = queryChanged ? state.text.caret : state.find_cursor;
        std::size_t found = find_from(state.text.text, query, start, caseSensitive);
        if (found == invalid_text_index && start > 0u)
            found = find_from(state.text.text, query, 0u, caseSensitive);
        if (found == invalid_text_index)
        {
            state.active_match = {};
            return false;
        }

        state.active_match = { found, found + query.size() };
        state.text.anchor = state.active_match.first;
        state.text.caret = state.active_match.past_last;
        state.find_cursor = state.active_match.past_last < state.text.text.size()
            ? state.active_match.past_last
            : 0u;
        return true;
    }

    bool TextEditorController::replace_active_match(
        TextEditorState& state,
        const TextControlOptions& options,
        std::string_view replacement) const
    {
        normalize(state);
        if (state.active_match.empty() || options.read_only)
            return false;

        state.text.anchor = state.active_match.first;
        state.text.caret = state.active_match.past_last;
        if (!replace_text_selection(state.text, options, replacement))
            return false;

        state.revision = next_revision(state.revision);
        state.dirty = true;
        state.find_cursor = state.text.caret;
        state.active_match = {};
        rebuild_line_index(state);
        return true;
    }

    void TextEditorController::mark_saved(TextEditorState& state) const noexcept
    {
        state.dirty = false;
    }

    const TextEditorController& text_editor_controller() noexcept
    {
        static const TextEditorController controller{};
        return controller;
    }

    void normalize_text_editor(TextEditorState& state)
    {
        text_editor_controller().normalize(state);
    }

    void set_text_editor_text(TextEditorState& state, std::string value)
    {
        text_editor_controller().set_text(state, std::move(value));
    }

    TextEditorResult update_text_editor(
        TextEditorState& state,
        const TextControlOptions& options,
        const TextControlInput& input)
    {
        return text_editor_controller().update(state, options, input);
    }

    TextEditorSnapshot text_editor_snapshot(const TextEditorState& state) noexcept
    {
        return text_editor_controller().snapshot(state);
    }

    TextEditorLineRange text_editor_line_range(
        const TextEditorState& state,
        std::size_t line) noexcept
    {
        return text_editor_controller().line_range(state, line);
    }

    std::string_view text_editor_line(
        const TextEditorState& state,
        std::size_t line) noexcept
    {
        return text_editor_controller().line(state, line);
    }

    bool find_next_text_editor_match(
        TextEditorState& state,
        std::string_view query,
        bool caseSensitive)
    {
        return text_editor_controller().find_next(state, query, caseSensitive);
    }

    bool replace_text_editor_match(
        TextEditorState& state,
        const TextControlOptions& options,
        std::string_view replacement)
    {
        return text_editor_controller().replace_active_match(state, options, replacement);
    }

    void mark_text_editor_saved(TextEditorState& state) noexcept
    {
        text_editor_controller().mark_saved(state);
    }
}