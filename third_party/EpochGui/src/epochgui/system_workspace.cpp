// SPDX-License-Identifier: LicenseRef-MIT-NoSell

#include "../../include/gui/system_workspace.hpp"

#include <algorithm>


#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace epochengine::gui_lib::system_workspace
{
    namespace
    {
        inline constexpr std::size_t invalid_index =
            (std::numeric_limits<std::size_t>::max)();

        [[nodiscard]] constexpr bool valid_kind(RowKind kind) noexcept
        {
            return kind == RowKind::system || kind == RowKind::task;
        }

        [[nodiscard]] constexpr bool valid_status(RowStatus status) noexcept
        {
            return static_cast<std::size_t>(status) < row_status_count;
        }

        [[nodiscard]] constexpr std::size_t status_index(RowStatus status) noexcept
        {
            return static_cast<std::size_t>(status);
        }

        [[nodiscard]] unsigned char ascii_fold(char value) noexcept
        {
            const unsigned char byte = static_cast<unsigned char>(value);
            if (byte >= static_cast<unsigned char>('A')
                && byte <= static_cast<unsigned char>('Z'))
            {
                return byte + static_cast<unsigned char>('a' - 'A');
            }
            return byte;
        }

        [[nodiscard]] int compare_folded(
            std::string_view left,
            std::string_view right) noexcept
        {
            const std::size_t common = (std::min)(left.size(), right.size());
            for (std::size_t index = 0; index < common; ++index)
            {
                const unsigned char left_value = ascii_fold(left[index]);
                const unsigned char right_value = ascii_fold(right[index]);
                if (left_value < right_value)
                    return -1;
                if (left_value > right_value)
                    return 1;
            }
            if (left.size() < right.size())
                return -1;
            if (left.size() > right.size())
                return 1;
            for (std::size_t index = 0; index < common; ++index)
            {
                const auto left_value = static_cast<unsigned char>(left[index]);
                const auto right_value = static_cast<unsigned char>(right[index]);
                if (left_value < right_value)
                    return -1;
                if (left_value > right_value)
                    return 1;
            }
            return 0;
        }

        [[nodiscard]] bool equal_folded(
            std::string_view left,
            std::string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (ascii_fold(left[index]) != ascii_fold(right[index]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool contains_folded(
            std::string_view text,
            std::string_view query) noexcept
        {
            if (query.empty())
                return true;
            if (query.size() > text.size())
                return false;

            const std::size_t last = text.size() - query.size();
            for (std::size_t offset = 0; offset <= last; ++offset)
            {
                bool matches = true;
                for (std::size_t index = 0; index < query.size(); ++index)
                {
                    if (ascii_fold(text[offset + index]) != ascii_fold(query[index]))
                    {
                        matches = false;
                        break;
                    }
                }
                if (matches)
                    return true;
            }
            return false;
        }

        [[nodiscard]] std::string bounded_message(
            std::string_view message,
            std::size_t maximum_bytes)
        {
            const std::size_t size = (std::min)(message.size(), maximum_bytes);
            return std::string{message.substr(0u, size)};
        }

        struct Candidate final
        {
            std::vector<Row> rows{};
            std::unordered_map<std::string, std::size_t> indices{};
            std::vector<std::size_t> parents{};
            std::vector<std::vector<std::size_t>> children{};
            std::vector<std::size_t> roots{};
        };

        struct CandidateResult final
        {
            Candidate candidate{};
            Error error{};
        };

        [[nodiscard]] CandidateResult build_candidate(
            std::span<const Row> supplied,
            const Limits& limits)
        {
            CandidateResult result{};
            if (!limits.valid())
            {
                result.error = {
                    .code = ErrorCode::invalid_limits,
                    .message = "System workspace limits are invalid."
                };
                return result;
            }
            if (supplied.size() > limits.maximum_rows)
            {
                result.error = {
                    .code = ErrorCode::row_limit_exceeded,
                    .message = "Adapter supplied more rows than the workspace limit."
                };
                return result;
            }

            Candidate& candidate = result.candidate;
            candidate.rows.assign(supplied.begin(), supplied.end());
            candidate.indices.reserve(candidate.rows.size());
            candidate.parents.assign(candidate.rows.size(), invalid_index);
            candidate.children.resize(candidate.rows.size());
            candidate.roots.reserve(candidate.rows.size());

            for (std::size_t index = 0; index < candidate.rows.size(); ++index)
            {
                const Row& row = candidate.rows[index];
                const auto fail = [&](ErrorCode code, std::string message)
                {
                    result.error = {
                        .code = code,
                        .message = std::move(message),
                        .row_id = row.id
                    };
                };

                if (row.id.empty())
                {
                    fail(ErrorCode::empty_id, "Every workspace row requires a stable ID.");
                    return result;
                }
                if (row.label.empty())
                {
                    fail(ErrorCode::empty_label, "Every workspace row requires a label.");
                    return result;
                }
                if (row.id.size() > limits.maximum_id_bytes
                    || row.parent_id.size() > limits.maximum_id_bytes
                    || row.label.size() > limits.maximum_label_bytes
                    || row.category.size() > limits.maximum_category_bytes
                    || row.detail.size() > limits.maximum_detail_bytes)
                {
                    fail(ErrorCode::field_too_long, "A workspace row field exceeds its configured bound.");
                    return result;
                }
                if (!valid_kind(row.kind))
                {
                    fail(ErrorCode::invalid_kind, "A workspace row has an invalid kind.");
                    return result;
                }
                if (!valid_status(row.status))
                {
                    fail(ErrorCode::invalid_status, "A workspace row has an invalid status.");
                    return result;
                }
                if (row.parent_id == row.id)
                {
                    fail(ErrorCode::self_parent, "A workspace row cannot parent itself.");
                    return result;
                }
                if (!candidate.indices.emplace(row.id, index).second)
                {
                    fail(ErrorCode::duplicate_id, "Workspace row IDs must be unique.");
                    return result;
                }
            }

            for (std::size_t index = 0; index < candidate.rows.size(); ++index)
            {
                const Row& row = candidate.rows[index];
                if (row.parent_id.empty())
                {
                    candidate.roots.push_back(index);
                    continue;
                }

                const auto parent = candidate.indices.find(row.parent_id);
                if (parent == candidate.indices.end())
                {
                    result.error = {
                        .code = ErrorCode::missing_parent,
                        .message = "A workspace row references a missing parent.",
                        .row_id = row.id
                    };
                    return result;
                }
                candidate.parents[index] = parent->second;
                candidate.children[parent->second].push_back(index);
            }

            std::vector<std::size_t> visit_token(candidate.rows.size(), 0u);
            for (std::size_t start = 0; start < candidate.rows.size(); ++start)
            {
                const std::size_t token = start + 1u;
                std::size_t depth = 0u;
                std::size_t current = start;
                while (current != invalid_index)
                {
                    if (visit_token[current] == token)
                    {
                        result.error = {
                            .code = ErrorCode::hierarchy_cycle,
                            .message = "Workspace row hierarchy contains a cycle.",
                            .row_id = candidate.rows[start].id
                        };
                        return result;
                    }
                    visit_token[current] = token;
                    if (depth > limits.maximum_hierarchy_depth)
                    {
                        result.error = {
                            .code = ErrorCode::hierarchy_depth_exceeded,
                            .message = "Workspace row hierarchy exceeds its configured depth.",
                            .row_id = candidate.rows[start].id
                        };
                        return result;
                    }
                    current = candidate.parents[current];
                    ++depth;
                }
            }
            return result;
        }
    }

    struct Controller::Implementation final
    {
        explicit Implementation(Limits requested_limits)
            : limits{requested_limits}
        {
            if (!limits.valid())
            {
                current_error = {
                    .code = ErrorCode::invalid_limits,
                    .message = "System workspace limits are invalid."
                };
                state = ContentState::error;
            }
        }

        Limits limits{};
        std::vector<Row> rows{};
        std::unordered_map<std::string, std::size_t> indices{};
        std::vector<std::size_t> parents{};
        std::vector<std::vector<std::size_t>> children{};
        std::vector<std::size_t> roots{};
        std::vector<VisibleRow> visible{};
        std::vector<CategorySummary> categories{};
        std::unordered_set<std::string> expanded{};
        std::vector<std::string> category_filters{};
        std::array<bool, row_status_count> status_filters{};
        std::string filter{};
        std::string selection{};
        Sort ordering{};
        Summary counts{};
        Error current_error{};
        ContentState state{ContentState::empty};
        std::uint64_t source_revision{};
        std::uint64_t view_revision{};
        bool stale_rows{};

        [[nodiscard]] bool filters_active() const noexcept
        {
            if (!filter.empty() || !category_filters.empty())
                return true;
            return std::ranges::any_of(status_filters, [](bool enabled) { return enabled; });
        }

        [[nodiscard]] bool status_filter_active() const noexcept
        {
            return std::ranges::any_of(status_filters, [](bool enabled) { return enabled; });
        }

        [[nodiscard]] bool category_enabled(std::string_view category) const noexcept
        {
            return std::ranges::any_of(
                category_filters,
                [&](const std::string& active)
                {
                    return equal_folded(active, category);
                });
        }

        [[nodiscard]] bool row_matches(const Row& row) const noexcept
        {
            if (!category_filters.empty() && !category_enabled(row.category))
                return false;
            if (status_filter_active() && !status_filters[status_index(row.status)])
                return false;
            if (filter.empty())
                return true;
            return contains_folded(row.id, filter)
                || contains_folded(row.label, filter)
                || contains_folded(row.category, filter)
                || contains_folded(row.detail, filter);
        }

        [[nodiscard]] bool has_children(std::size_t index) const noexcept
        {
            return rows[index].expandable || !children[index].empty();
        }

        [[nodiscard]] bool row_less(std::size_t left_index, std::size_t right_index) const noexcept
        {
            const Row& left = rows[left_index];
            const Row& right = rows[right_index];
            if (ordering.systems_first && left.kind != right.kind)
                return left.kind == RowKind::system;

            int primary = 0;
            switch (ordering.key)
            {
            case SortKey::adapter_order:
                primary = left.adapter_order < right.adapter_order
                    ? -1
                    : (left.adapter_order > right.adapter_order ? 1 : 0);
                break;
            case SortKey::name:
                primary = compare_folded(left.label, right.label);
                break;
            case SortKey::category:
                primary = compare_folded(left.category, right.category);
                break;
            case SortKey::status:
                primary = status_index(left.status) < status_index(right.status)
                    ? -1
                    : (status_index(left.status) > status_index(right.status) ? 1 : 0);
                break;
            case SortKey::kind:
                primary = static_cast<std::uint8_t>(left.kind)
                        < static_cast<std::uint8_t>(right.kind)
                    ? -1
                    : (static_cast<std::uint8_t>(left.kind)
                            > static_cast<std::uint8_t>(right.kind)
                        ? 1
                        : 0);
                break;
            }
            if (primary != 0)
            {
                return ordering.direction == SortDirection::ascending
                    ? primary < 0
                    : primary > 0;
            }
            if (left.adapter_order != right.adapter_order)
                return left.adapter_order < right.adapter_order;
            const int label_order = compare_folded(left.label, right.label);
            if (label_order != 0)
                return label_order < 0;
            return compare_folded(left.id, right.id) < 0;
        }

        void mark_selection() noexcept
        {
            counts.selection_visible = false;
            for (VisibleRow& item : visible)
            {
                item.selected = item.row != nullptr && item.row->id == selection;
                counts.selection_visible = counts.selection_visible || item.selected;
            }
        }

        void bump_view_revision() noexcept
        {
            if (view_revision != (std::numeric_limits<std::uint64_t>::max)())
                ++view_revision;
        }

        void rebuild()
        {
            const auto less = [&](std::size_t left, std::size_t right)
            {
                return row_less(left, right);
            };
            std::ranges::sort(roots, less);
            for (auto& row_children : children)
                std::ranges::sort(row_children, less);

            const bool filtering = filters_active();
            std::vector<std::uint8_t> direct(rows.size(), 0u);
            std::vector<std::uint8_t> included(rows.size(), 0u);
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                direct[index] = row_matches(rows[index]) ? 1u : 0u;
                if (direct[index] == 0u)
                    continue;
                included[index] = 1u;
                for (std::size_t parent = parents[index]; parent != invalid_index;
                    parent = parents[parent])
                {
                    included[parent] = 1u;
                }
            }

            visible.clear();
            visible.reserve(rows.size());
            const auto append = [&](auto&& self, std::size_t index, std::size_t depth) -> void
            {
                if (included[index] == 0u)
                    return;

                const bool children_present = has_children(index);
                const bool included_child = std::ranges::any_of(
                    children[index],
                    [&](std::size_t child) { return included[child] != 0u; });
                const bool forced_open = filtering && included_child;
                const bool user_expanded = expanded.contains(rows[index].id);
                const bool effective_expanded = children_present
                    && (user_expanded || forced_open);
                visible.push_back({
                    .row = &rows[index],
                    .depth = depth,
                    .has_children = children_present,
                    .expanded = effective_expanded,
                    .expansion_forced = forced_open && !user_expanded,
                    .selected = false,
                    .direct_match = direct[index] != 0u
                });
                if (!effective_expanded)
                    return;
                for (const std::size_t child : children[index])
                    self(self, child, depth + 1u);
            };
            for (const std::size_t root : roots)
                append(append, root, 0u);

            counts = {};
            counts.total_rows = rows.size();
            counts.visible_rows = visible.size();
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                const Row& row = rows[index];
                counts.matching_rows += direct[index] != 0u ? 1u : 0u;
                counts.system_rows += row.kind == RowKind::system ? 1u : 0u;
                counts.task_rows += row.kind == RowKind::task ? 1u : 0u;
                ++counts.status_totals[status_index(row.status)];
                if (expanded.contains(row.id) && has_children(index))
                    ++counts.expanded_rows;
            }
            for (const VisibleRow& item : visible)
            {
                if (item.row == nullptr)
                    continue;
                counts.visible_system_rows += item.row->kind == RowKind::system ? 1u : 0u;
                counts.visible_task_rows += item.row->kind == RowKind::task ? 1u : 0u;
                ++counts.visible_status_totals[status_index(item.row->status)];
            }

            std::map<std::string, CategorySummary> by_category{};
            for (const Row& row : rows)
            {
                CategorySummary& category = by_category[row.category];
                category.category = row.category;
                ++category.total_rows;
                category.filter_active = category_enabled(row.category);
            }
            for (const VisibleRow& item : visible)
            {
                if (item.row != nullptr)
                    ++by_category[item.row->category].visible_rows;
            }
            categories.clear();
            categories.reserve(by_category.size());
            for (auto& [name, category] : by_category)
            {
                (void)name;
                categories.push_back(std::move(category));
            }
            std::ranges::sort(
                categories,
                [](const CategorySummary& left, const CategorySummary& right)
                {
                    return compare_folded(left.category, right.category) < 0;
                });

            mark_selection();
            if (!current_error.present())
            {
                if (rows.empty())
                    state = ContentState::empty;
                else if (visible.empty())
                    state = ContentState::no_matches;
                else
                    state = ContentState::ready;
            }
            bump_view_revision();
        }

        [[nodiscard]] RefreshResult fail_refresh(
            Error error,
            std::size_t supplied_rows)
        {
            current_error = std::move(error);
            state = ContentState::error;
            stale_rows = !rows.empty();
            return {
                .error = current_error.code,
                .supplied_rows = supplied_rows,
                .committed_rows = rows.size(),
                .visible_rows = visible.size()
            };
        }

        [[nodiscard]] std::size_t visible_selection_index() const noexcept
        {
            for (std::size_t index = 0; index < visible.size(); ++index)
            {
                if (visible[index].row != nullptr
                    && visible[index].row->id == selection)
                {
                    return index;
                }
            }
            return invalid_index;
        }
    };

    Controller::Controller(Limits limits)
        : implementation_{std::make_unique<Implementation>(limits)}
    {
    }

    Controller::~Controller() = default;
    Controller::Controller(Controller&&) noexcept = default;
    Controller& Controller::operator=(Controller&&) noexcept = default;

    const Limits& Controller::limits() const noexcept
    {
        return implementation_->limits;
    }

    RefreshResult Controller::refresh(Adapter& adapter, RefreshOptions options)
    {
        Implementation& implementation = *implementation_;
        if (!implementation.limits.valid())
        {
            return implementation.fail_refresh(
                {
                    .code = ErrorCode::invalid_limits,
                    .message = "System workspace limits are invalid."
                },
                0u);
        }

        AdapterSnapshot snapshot{};
        try
        {
            snapshot = adapter.read({.maximum_rows = implementation.limits.maximum_rows});
        }
        catch (const std::exception& exception)
        {
            return implementation.fail_refresh(
                {
                    .code = ErrorCode::adapter_failure,
                    .message = bounded_message(
                        exception.what(),
                        implementation.limits.maximum_detail_bytes)
                },
                0u);
        }
        catch (...)
        {
            return implementation.fail_refresh(
                {
                    .code = ErrorCode::adapter_failure,
                    .message = "System workspace adapter raised an unknown exception."
                },
                0u);
        }

        if (!snapshot.succeeded)
        {
            const std::string_view message = snapshot.error_message.empty()
                ? std::string_view{"System workspace adapter failed."}
                : snapshot.error_message;
            return implementation.fail_refresh(
                {
                    .code = ErrorCode::adapter_failure,
                    .message = bounded_message(
                        message,
                        implementation.limits.maximum_detail_bytes)
                },
                snapshot.rows.size());
        }
        return replace_rows(snapshot.rows, snapshot.source_revision, options);
    }

    RefreshResult Controller::replace_rows(
        std::span<const Row> supplied,
        std::uint64_t source_revision,
        RefreshOptions options)
    {
        Implementation& implementation = *implementation_;
        CandidateResult candidate = build_candidate(supplied, implementation.limits);
        if (candidate.error.present())
            return implementation.fail_refresh(std::move(candidate.error), supplied.size());

        const std::string previous_selection = implementation.selection;
        const std::size_t previous_visible_index = implementation.visible_selection_index();
        const std::size_t previous_expansion_count = implementation.expanded.size();

        implementation.rows = std::move(candidate.candidate.rows);
        implementation.indices = std::move(candidate.candidate.indices);
        implementation.parents = std::move(candidate.candidate.parents);
        implementation.children = std::move(candidate.candidate.children);
        implementation.roots = std::move(candidate.candidate.roots);
        implementation.source_revision = source_revision;
        implementation.current_error = {};
        implementation.stale_rows = false;

        if (!options.preserve_expansion)
        {
            implementation.expanded.clear();
        }
        else
        {
            std::erase_if(
                implementation.expanded,
                [&](const std::string& id)
                {
                    const auto found = implementation.indices.find(id);
                    return found == implementation.indices.end()
                        || !implementation.has_children(found->second);
                });
        }

        bool stale_selection = false;
        if (!implementation.selection.empty()
            && !implementation.indices.contains(implementation.selection))
        {
            stale_selection = true;
            implementation.selection.clear();
        }
        implementation.rebuild();

        if (stale_selection
            && options.stale_selection == StaleSelectionPolicy::select_nearest_visible
            && !implementation.visible.empty())
        {
            const std::size_t requested = previous_visible_index == invalid_index
                ? 0u
                : previous_visible_index;
            const std::size_t replacement = (std::min)(
                requested,
                implementation.visible.size() - 1u);
            implementation.selection = implementation.visible[replacement].row->id;
            implementation.mark_selection();
            implementation.bump_view_revision();
        }

        return {
            .error = ErrorCode::none,
            .supplied_rows = supplied.size(),
            .committed_rows = implementation.rows.size(),
            .visible_rows = implementation.visible.size(),
            .stale_expansions_removed = previous_expansion_count
                - implementation.expanded.size(),
            .committed = true,
            .selection_changed = previous_selection != implementation.selection,
            .stale_selection_resolved = stale_selection
        };
    }

    MutationResult Controller::set_text_filter(std::string_view filter)
    {
        Implementation& implementation = *implementation_;
        if (filter.size() > implementation.limits.maximum_filter_bytes)
        {
            return {
                .error = ErrorCode::filter_too_long,
                .accepted = false
            };
        }
        if (implementation.filter == filter)
            return {};
        implementation.filter.assign(filter);
        implementation.rebuild();
        return {.changed = true};
    }

    MutationResult Controller::set_category_filter(
        std::string_view category,
        bool enabled)
    {
        Implementation& implementation = *implementation_;
        if (category.size() > implementation.limits.maximum_category_bytes)
        {
            return {
                .error = ErrorCode::filter_too_long,
                .accepted = false
            };
        }

        const auto found = std::ranges::find_if(
            implementation.category_filters,
            [&](const std::string& existing)
            {
                return equal_folded(existing, category);
            });
        if (enabled)
        {
            if (found != implementation.category_filters.end())
                return {};
            if (implementation.category_filters.size()
                >= implementation.limits.maximum_active_category_filters)
            {
                return {
                    .error = ErrorCode::category_filter_limit,
                    .accepted = false
                };
            }
            implementation.category_filters.emplace_back(category);
            std::ranges::sort(
                implementation.category_filters,
                [](const std::string& left, const std::string& right)
                {
                    return compare_folded(left, right) < 0;
                });
        }
        else
        {
            if (found == implementation.category_filters.end())
                return {};
            implementation.category_filters.erase(found);
        }
        implementation.rebuild();
        return {.changed = true};
    }

    MutationResult Controller::set_status_filter(RowStatus status, bool enabled)
    {
        if (!valid_status(status))
        {
            return {
                .error = ErrorCode::invalid_status,
                .accepted = false
            };
        }
        Implementation& implementation = *implementation_;
        bool& current = implementation.status_filters[status_index(status)];
        if (current == enabled)
            return {};
        current = enabled;
        implementation.rebuild();
        return {.changed = true};
    }

    MutationResult Controller::clear_filters()
    {
        Implementation& implementation = *implementation_;
        const bool changed = !implementation.filter.empty()
            || !implementation.category_filters.empty()
            || implementation.status_filter_active();
        if (!changed)
            return {};
        implementation.filter.clear();
        implementation.category_filters.clear();
        implementation.status_filters.fill(false);
        implementation.rebuild();
        return {.changed = true};
    }

    MutationResult Controller::set_sort(Sort sort)
    {
        if (static_cast<std::uint8_t>(sort.key)
                > static_cast<std::uint8_t>(SortKey::kind)
            || static_cast<std::uint8_t>(sort.direction)
                > static_cast<std::uint8_t>(SortDirection::descending))
        {
            return {
                .error = ErrorCode::invalid_kind,
                .accepted = false
            };
        }
        Implementation& implementation = *implementation_;
        if (implementation.ordering == sort)
            return {};
        implementation.ordering = sort;
        implementation.rebuild();
        return {.changed = true};
    }

    MutationResult Controller::select(std::string_view id)
    {
        Implementation& implementation = *implementation_;
        if (!implementation.indices.contains(std::string{id}))
        {
            return {
                .error = ErrorCode::unknown_row,
                .accepted = false
            };
        }
        if (implementation.selection == id)
            return {};
        implementation.selection.assign(id);
        implementation.mark_selection();
        implementation.bump_view_revision();
        return {.changed = true};
    }

    bool Controller::clear_selection()
    {
        Implementation& implementation = *implementation_;
        if (implementation.selection.empty())
            return false;
        implementation.selection.clear();
        implementation.mark_selection();
        implementation.bump_view_revision();
        return true;
    }

    MutationResult Controller::set_expanded(std::string_view id, bool expanded)
    {
        Implementation& implementation = *implementation_;
        const auto found = implementation.indices.find(std::string{id});
        if (found == implementation.indices.end())
        {
            return {
                .error = ErrorCode::unknown_row,
                .accepted = false
            };
        }
        if (!implementation.has_children(found->second))
            return {};

        bool changed = false;
        if (expanded)
            changed = implementation.expanded.emplace(id).second;
        else
            changed = implementation.expanded.erase(std::string{id}) != 0u;
        if (!changed)
            return {};
        implementation.rebuild();
        return {.changed = true};
    }

    MutationResult Controller::toggle_expanded(std::string_view id)
    {
        return set_expanded(id, !is_expanded(id));
    }

    NavigationResult Controller::navigate(NavigationCommand command)
    {
        Implementation& implementation = *implementation_;
        NavigationResult result{.selected_id = implementation.selection};
        if (implementation.visible.empty())
            return result;

        std::size_t current = implementation.visible_selection_index();
        const auto select_visible = [&](std::size_t index)
        {
            if (index >= implementation.visible.size()
                || implementation.visible[index].row == nullptr)
            {
                return;
            }
            const std::string& id = implementation.visible[index].row->id;
            if (implementation.selection == id)
                return;
            implementation.selection = id;
            implementation.mark_selection();
            implementation.bump_view_revision();
            result.selection_changed = true;
            result.changed = true;
        };

        switch (command)
        {
        case NavigationCommand::first:
            select_visible(0u);
            break;
        case NavigationCommand::previous:
            select_visible(current == invalid_index || current == 0u ? 0u : current - 1u);
            break;
        case NavigationCommand::next:
            select_visible(current == invalid_index
                ? 0u
                : (std::min)(current + 1u, implementation.visible.size() - 1u));
            break;
        case NavigationCommand::last:
            select_visible(implementation.visible.size() - 1u);
            break;
        case NavigationCommand::move_left:
            if (current == invalid_index)
            {
                select_visible(0u);
                break;
            }
            if (implementation.visible[current].has_children
                && implementation.expanded.contains(
                    implementation.visible[current].row->id))
            {
                const std::string id = implementation.visible[current].row->id;
                implementation.expanded.erase(id);
                implementation.rebuild();
                result.expansion_changed = true;
                result.changed = true;
                break;
            }
            if (implementation.visible[current].depth > 0u)
            {
                const std::size_t row_index = implementation.indices.at(
                    implementation.visible[current].row->id);
                const std::size_t parent_index = implementation.parents[row_index];
                if (parent_index != invalid_index)
                {
                    const std::string& parent_id = implementation.rows[parent_index].id;
                    const auto parent_visible = std::ranges::find_if(
                        implementation.visible,
                        [&](const VisibleRow& item)
                        {
                            return item.row != nullptr && item.row->id == parent_id;
                        });
                    if (parent_visible != implementation.visible.end())
                    {
                        select_visible(static_cast<std::size_t>(
                            parent_visible - implementation.visible.begin()));
                    }
                }
            }
            break;
        case NavigationCommand::move_right:
            if (current == invalid_index)
            {
                select_visible(0u);
                break;
            }
            if (!implementation.visible[current].has_children)
                break;
            if (!implementation.expanded.contains(implementation.visible[current].row->id))
            {
                const std::string id = implementation.visible[current].row->id;
                implementation.expanded.emplace(id);
                implementation.rebuild();
                result.expansion_changed = true;
                result.changed = true;
                break;
            }
            if (current + 1u < implementation.visible.size()
                && implementation.visible[current + 1u].depth
                    == implementation.visible[current].depth + 1u)
            {
                select_visible(current + 1u);
            }
            break;
        case NavigationCommand::toggle_expansion:
            if (current != invalid_index && implementation.visible[current].has_children)
            {
                const std::string id = implementation.visible[current].row->id;
                if (implementation.expanded.contains(id))
                    implementation.expanded.erase(id);
                else
                    implementation.expanded.emplace(id);
                implementation.rebuild();
                result.expansion_changed = true;
                result.changed = true;
            }
            break;
        }

        result.selected_id = implementation.selection;
        return result;
    }

    ContentState Controller::content_state() const noexcept
    {
        return implementation_->state;
    }

    const Error& Controller::error() const noexcept
    {
        return implementation_->current_error;
    }

    bool Controller::using_stale_rows() const noexcept
    {
        return implementation_->stale_rows;
    }

    std::uint64_t Controller::source_revision() const noexcept
    {
        return implementation_->source_revision;
    }

    std::uint64_t Controller::view_revision() const noexcept
    {
        return implementation_->view_revision;
    }

    std::string_view Controller::text_filter() const noexcept
    {
        return implementation_->filter;
    }

    std::span<const std::string> Controller::category_filters() const noexcept
    {
        return implementation_->category_filters;
    }

    bool Controller::status_filter_enabled(RowStatus status) const noexcept
    {
        return valid_status(status)
            && implementation_->status_filters[status_index(status)];
    }

    const Sort& Controller::sort() const noexcept
    {
        return implementation_->ordering;
    }

    std::string_view Controller::selected_id() const noexcept
    {
        return implementation_->selection;
    }

    const Row* Controller::selected_row() const noexcept
    {
        return find_row(implementation_->selection);
    }

    const Row* Controller::find_row(std::string_view id) const noexcept
    {
        const auto found = std::ranges::find_if(
            implementation_->rows,
            [&](const Row& row) { return row.id == id; });
        return found == implementation_->rows.end() ? nullptr : &*found;
    }

    bool Controller::is_expanded(std::string_view id) const noexcept
    {
        return std::ranges::any_of(
            implementation_->expanded,
            [&](const std::string& expanded_id) { return expanded_id == id; });
    }

    std::span<const Row> Controller::rows() const noexcept
    {
        return implementation_->rows;
    }

    std::span<const VisibleRow> Controller::visible_rows() const noexcept
    {
        return implementation_->visible;
    }

    const Summary& Controller::summary() const noexcept
    {
        return implementation_->counts;
    }

    std::span<const CategorySummary> Controller::category_summaries() const noexcept
    {
        return implementation_->categories;
    }
}
