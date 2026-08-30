// SPDX-License-Identifier: LicenseRef-MIT-NoSell
#pragma once

#include "text_control.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace epochengine::gui_lib
{
    struct TextEditorLineRange
    {
        std::size_t first{};
        std::size_t past_last{};

        [[nodiscard]] bool empty() const noexcept { return first == past_last; }
        [[nodiscard]] std::size_t size() const noexcept { return past_last - first; }
    };

    struct TextEditorState
    {
        TextControlState text{};
        std::vector<std::size_t> line_starts{ 0u };
        std::string find_query{};
        TextEditorLineRange active_match{};
        std::size_t find_cursor{};
        std::uint64_t revision{ 1u };
        bool dirty{};
    };

    struct TextEditorSnapshot
    {
        TextSelectionRange selection{};
        TextPosition caret{};
        TextEditorLineRange active_match{};
        std::size_t line_count{ 1u };
        std::uint64_t revision{ 1u };
        bool dirty{};
    };

    struct TextEditorResult
    {
        TextControlResult text{};
        TextEditorSnapshot snapshot{};
        bool document_changed{};
        bool find_changed{};
        bool find_wrapped{};
    };

    class TextEditorController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void normalize(TextEditorState& state) const;
        void set_text(TextEditorState& state, std::string value) const;
        [[nodiscard]] TextEditorResult update(
            TextEditorState& state,
            const TextControlOptions& options,
            const TextControlInput& input) const;
        [[nodiscard]] TextEditorSnapshot snapshot(const TextEditorState& state) const noexcept;
        [[nodiscard]] TextEditorLineRange line_range(const TextEditorState& state, std::size_t line) const noexcept;
        [[nodiscard]] std::string_view line(const TextEditorState& state, std::size_t line) const noexcept;
        [[nodiscard]] bool find_next(TextEditorState& state, std::string_view query, bool case_sensitive = true) const;
        [[nodiscard]] bool replace_active_match(
            TextEditorState& state,
            const TextControlOptions& options,
            std::string_view replacement) const;
        void mark_saved(TextEditorState& state) const noexcept;
    };

    [[nodiscard]] const TextEditorController& text_editor_controller() noexcept;
    void normalize_text_editor(TextEditorState& state);
    void set_text_editor_text(TextEditorState& state, std::string value);
    [[nodiscard]] TextEditorResult update_text_editor(
        TextEditorState& state,
        const TextControlOptions& options,
        const TextControlInput& input);
    [[nodiscard]] TextEditorSnapshot text_editor_snapshot(const TextEditorState& state) noexcept;
    [[nodiscard]] TextEditorLineRange text_editor_line_range(const TextEditorState& state, std::size_t line) noexcept;
    [[nodiscard]] std::string_view text_editor_line(const TextEditorState& state, std::size_t line) noexcept;
    [[nodiscard]] bool find_next_text_editor_match(
        TextEditorState& state,
        std::string_view query,
        bool case_sensitive = true);
    [[nodiscard]] bool replace_text_editor_match(
        TextEditorState& state,
        const TextControlOptions& options,
        std::string_view replacement);
    void mark_text_editor_saved(TextEditorState& state) noexcept;
}