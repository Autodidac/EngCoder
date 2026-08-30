// SPDX-License-Identifier: LicenseRef-MIT-NoSell

#include <gui/hierarchy_tree.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace epochengine::gui_lib::hierarchy_tree
{
    namespace
    {
        [[nodiscard]] bool valid_kind(NodeKind kind) noexcept
        {
            switch (kind)
            {
            case NodeKind::item:
            case NodeKind::group:
            case NodeKind::document:
            case NodeKind::component:
            case NodeKind::evidence:
                return true;
            }
            return false;
        }

        [[nodiscard]] char folded(char value) noexcept
        {
            return static_cast<char>(std::tolower(
                static_cast<unsigned char>(value)));
        }

        [[nodiscard]] bool contains_folded(
            std::string_view text,
            std::string_view query) noexcept
        {
            if (query.empty())
                return true;
            if (query.size() > text.size())
                return false;
            return std::search(
                       text.begin(), text.end(), query.begin(), query.end(),
                       [](char left, char right) noexcept
                       {
                           return folded(left) == folded(right);
                       })
                != text.end();
        }

        [[nodiscard]] std::vector<std::string_view> query_tokens(
            std::string_view query)
        {
            std::vector<std::string_view> result{};
            std::size_t cursor{};
            while (cursor < query.size())
            {
                while (cursor < query.size()
                    && std::isspace(static_cast<unsigned char>(query[cursor])))
                {
                    ++cursor;
                }
                const std::size_t first = cursor;
                while (cursor < query.size()
                    && !std::isspace(static_cast<unsigned char>(query[cursor])))
                {
                    ++cursor;
                }
                if (cursor > first)
                    result.emplace_back(query.substr(first, cursor - first));
            }
            return result;
        }

        [[nodiscard]] bool node_matches(
            const Node& node,
            std::span<const std::string_view> tokens) noexcept
        {
            return std::ranges::all_of(tokens, [&](std::string_view token)
            {
                return contains_folded(node.label, token)
                    || contains_folded(node.detail, token)
                    || contains_folded(node.search_terms, token)
                    || contains_folded(node.context_tag, token);
            });
        }

    }

    struct Controller::Implementation final
    {
        explicit Implementation(Limits requested) : limits(requested)
        {
            if (!limits.valid())
            {
                limits = {};
                error = {
                    .code = ErrorCode::invalid_limits,
                    .message = "Hierarchy limits were invalid; defaults are active."};
            }
        }

        Limits limits{};
        std::vector<Node> nodes{};
        std::unordered_map<std::uint64_t, std::size_t> index_by_id{};
        std::vector<std::vector<std::size_t>> children{};
        std::vector<std::size_t> roots{};
        std::vector<std::size_t> depths{};
        std::unordered_set<std::uint64_t> expanded{};
        std::unordered_set<std::uint64_t> selected{};
        std::vector<NodeId> selected_ids{};
        std::vector<VisibleRow> visible{};
        std::vector<ContextAction> actions{};
        std::string filter{};
        bool include_descendants{true};
        NodeId focused{};
        NodeId anchor{};
        Error error{};
        Summary summary{};
        std::uint64_t model_revision{};
        std::uint64_t view_revision{};

        [[nodiscard]] const Node* find(NodeId id) const noexcept
        {
            const auto found = index_by_id.find(id.value);
            return found == index_by_id.end() ? nullptr : &nodes[found->second];
        }

        [[nodiscard]] std::size_t index_of(NodeId id) const noexcept
        {
            const auto found = index_by_id.find(id.value);
            return found == index_by_id.end()
                ? std::numeric_limits<std::size_t>::max()
                : found->second;
        }

        [[nodiscard]] std::size_t visible_index(NodeId id) const noexcept
        {
            const auto found = std::ranges::find_if(
                visible,
                [&](const VisibleRow& row)
                {
                    return row.node && row.node->id == id;
                });
            return found == visible.end()
                ? std::numeric_limits<std::size_t>::max()
                : static_cast<std::size_t>(found - visible.begin());
        }

        [[nodiscard]] bool selectable(std::size_t index) const noexcept
        {
            return index < nodes.size() && !nodes[index].disabled;
        }

        void rebuild_selected_ids()
        {
            selected_ids.clear();
            selected_ids.reserve(selected.size());
            const auto append = [&](auto&& self, std::size_t index) -> void
            {
                if (index >= nodes.size())
                    return;
                if (selected.contains(nodes[index].id.value))
                    selected_ids.emplace_back(nodes[index].id);
                for (const std::size_t child : children[index])
                    self(self, child);
            };
            for (const std::size_t root : roots)
                append(append, root);
        }

        void refresh_row_state() noexcept
        {
            bool focusVisible = false;
            for (VisibleRow& row : visible)
            {
                row.selected = row.node
                    && selected.contains(row.node->id.value);
                row.focused = row.node && row.node->id == focused;
                row.selection_anchor = row.node && row.node->id == anchor;
                focusVisible = focusVisible || row.focused;
            }
            summary.selected_nodes = selected.size();
            summary.focus_visible = focusVisible;
        }

        void rebuild_view()
        {
            visible.clear();
            summary = {};
            summary.total_nodes = nodes.size();
            summary.selected_nodes = selected.size();
            summary.expanded_nodes = expanded.size();
            summary.filter_active = !filter.empty();
            for (const Node& node : nodes)
            {
                summary.disabled_nodes += node.disabled ? 1u : 0u;
                summary.locked_nodes += node.locked ? 1u : 0u;
            }

            const auto tokens = query_tokens(filter);
            std::vector<bool> direct(nodes.size(), tokens.empty());
            std::vector<bool> included(nodes.size(), tokens.empty());
            if (!tokens.empty())
            {
                for (std::size_t index = 0; index < nodes.size(); ++index)
                {
                    direct[index] = node_matches(nodes[index], tokens);
                    included[index] = direct[index];
                    summary.direct_matches += direct[index] ? 1u : 0u;
                }

                for (std::size_t index = 0; index < nodes.size(); ++index)
                {
                    if (!direct[index])
                        continue;
                    NodeId parent = nodes[index].parent;
                    while (parent)
                    {
                        const std::size_t parentIndex = index_of(parent);
                        if (parentIndex >= nodes.size() || included[parentIndex])
                            break;
                        included[parentIndex] = true;
                        parent = nodes[parentIndex].parent;
                    }
                }

                if (include_descendants)
                {
                    const auto include_subtree = [&](auto&& self, std::size_t index) -> void
                    {
                        included[index] = true;
                        for (const std::size_t child : children[index])
                            self(self, child);
                    };
                    for (std::size_t index = 0; index < nodes.size(); ++index)
                    {
                        if (direct[index])
                            include_subtree(include_subtree, index);
                    }
                }
            }
            else
            {
                summary.direct_matches = nodes.size();
            }

            const auto append = [&](auto&& self, std::size_t index) -> void
            {
                if (index >= nodes.size() || !included[index])
                    return;
                const bool hasChildren = !children[index].empty();
                const bool expandedByState = expanded.contains(nodes[index].id.value);
                bool forcedOpen{};
                if (!filter.empty())
                {
                    forcedOpen = std::ranges::any_of(
                        children[index],
                        [&](std::size_t child)
                        {
                            return child < included.size() && included[child];
                        });
                }
                visible.emplace_back(VisibleRow{
                    .node = &nodes[index],
                    .source_index = index,
                    .visible_index = visible.size(),
                    .depth = depths[index],
                    .has_children = hasChildren,
                    .expanded = expandedByState,
                    .expansion_forced = forcedOpen,
                    .direct_match = direct[index]});
                if (hasChildren && (expandedByState || forcedOpen))
                {
                    for (const std::size_t child : children[index])
                        self(self, child);
                }
            };
            for (const std::size_t root : roots)
                append(append, root);

            summary.visible_rows = visible.size();
            if (focused && visible_index(focused) >= visible.size())
            {
                const auto replacement = std::ranges::find_if(
                    visible,
                    [](const VisibleRow& row)
                    {
                        return row.node && !row.node->disabled;
                    });
                focused = replacement == visible.end()
                    ? NodeId{}
                    : replacement->node->id;
            }
            if (anchor && !selected.contains(anchor.value))
                anchor = {};
            refresh_row_state();
            ++view_revision;
        }

        [[nodiscard]] MutationResult expansion(NodeId id, bool value)
        {
            const std::size_t index = index_of(id);
            if (index >= nodes.size())
                return {ErrorCode::unknown_node, false, false};
            if (nodes[index].disabled)
                return {ErrorCode::disabled_node, false, false};
            if (children[index].empty() && !nodes[index].expandable)
                return {ErrorCode::none, true, false};
            const bool before = expanded.contains(id.value);
            if (before == value)
                return {};
            if (value)
                expanded.insert(id.value);
            else
                expanded.erase(id.value);
            rebuild_view();
            return {ErrorCode::none, true, true};
        }
    };

    Controller::Controller(Limits limits)
        : implementation_(std::make_unique<Implementation>(limits))
    {
    }

    Controller::~Controller() = default;
    Controller::Controller(Controller&&) noexcept = default;
    Controller& Controller::operator=(Controller&&) noexcept = default;

    const Limits& Controller::limits() const noexcept
    {
        return implementation_->limits;
    }

    ReplaceResult Controller::replace_nodes(
        std::span<const Node> supplied,
        bool preserve_state)
    {
        auto& state = *implementation_;
        ReplaceResult result{.supplied_nodes = supplied.size()};
        state.error = {};
        if (supplied.size() > state.limits.maximum_nodes)
        {
            state.error = {
                .code = ErrorCode::node_limit_exceeded,
                .message = "Hierarchy node limit exceeded."};
            result.error = state.error.code;
            return result;
        }

        std::vector<Node> candidate{supplied.begin(), supplied.end()};
        std::unordered_map<std::uint64_t, std::size_t> candidateIndex{};
        candidateIndex.reserve(candidate.size());
        for (std::size_t index = 0; index < candidate.size(); ++index)
        {
            const Node& node = candidate[index];
            ErrorCode code = ErrorCode::none;
            if (!node.id)
                code = ErrorCode::invalid_id;
            else if (!valid_kind(node.kind))
                code = ErrorCode::invalid_kind;
            else if (node.label.empty())
                code = ErrorCode::empty_label;
            else if (node.label.size() + node.detail.size()
                    + node.search_terms.size() + node.context_tag.size()
                > state.limits.maximum_idless_text_bytes)
                code = ErrorCode::text_limit_exceeded;
            else if (node.parent == node.id)
                code = ErrorCode::self_parent;
            else if (!candidateIndex.emplace(node.id.value, index).second)
                code = ErrorCode::duplicate_id;
            if (code != ErrorCode::none)
            {
                state.error = {
                    .code = code,
                    .node = node.id,
                    .message = "Hierarchy node validation failed."};
                result.error = code;
                return result;
            }
        }

        std::vector<std::vector<std::size_t>> candidateChildren(candidate.size());
        std::vector<std::size_t> candidateRoots{};
        candidateRoots.reserve(candidate.size());
        for (std::size_t index = 0; index < candidate.size(); ++index)
        {
            const NodeId parent = candidate[index].parent;
            if (!parent)
            {
                candidateRoots.emplace_back(index);
                continue;
            }
            const auto found = candidateIndex.find(parent.value);
            if (found == candidateIndex.end())
            {
                state.error = {
                    .code = ErrorCode::missing_parent,
                    .node = candidate[index].id,
                    .message = "Hierarchy node parent is missing."};
                result.error = state.error.code;
                return result;
            }
            candidateChildren[found->second].emplace_back(index);
        }

        std::vector<std::size_t> candidateDepths(candidate.size());
        std::vector<std::uint8_t> marks(candidate.size());
        constexpr std::size_t noIndex = std::numeric_limits<std::size_t>::max();
        for (std::size_t start = 0; start < candidate.size(); ++start)
        {
            if (marks[start] == 2u)
                continue;
            std::vector<std::size_t> path{};
            std::size_t cursor = start;
            while (marks[cursor] == 0u)
            {
                marks[cursor] = 1u;
                path.emplace_back(cursor);
                const NodeId parentId = candidate[cursor].parent;
                if (!parentId)
                {
                    cursor = noIndex;
                    break;
                }
                cursor = candidateIndex.at(parentId.value);
            }
            if (cursor != noIndex && marks[cursor] == 1u)
            {
                state.error = {
                    .code = ErrorCode::hierarchy_cycle,
                    .node = candidate[cursor].id,
                    .message = "Hierarchy contains a parent cycle."};
                result.error = state.error.code;
                return result;
            }
            for (auto iterator = path.rbegin(); iterator != path.rend(); ++iterator)
            {
                const std::size_t index = *iterator;
                std::size_t depth{};
                if (candidate[index].parent)
                {
                    const std::size_t parentIndex = candidateIndex.at(
                        candidate[index].parent.value);
                    depth = candidateDepths[parentIndex] + 1u;
                }
                if (depth >= state.limits.maximum_depth)
                {
                    state.error = {
                        .code = ErrorCode::hierarchy_depth_exceeded,
                        .node = candidate[index].id,
                        .message = "Hierarchy depth limit exceeded."};
                    result.error = state.error.code;
                    return result;
                }
                candidateDepths[index] = depth;
                marks[index] = 2u;
            }
        }

        const auto less = [&](std::size_t left, std::size_t right)
        {
            if (candidate[left].order != candidate[right].order)
                return candidate[left].order < candidate[right].order;
            if (candidate[left].label != candidate[right].label)
                return candidate[left].label < candidate[right].label;
            return candidate[left].id < candidate[right].id;
        };
        std::ranges::sort(candidateRoots, less);
        for (auto& siblings : candidateChildren)
            std::ranges::sort(siblings, less);

        const auto oldExpanded = state.expanded;
        const auto oldSelected = state.selected;
        const NodeId oldFocus = state.focused;
        state.nodes = std::move(candidate);
        state.index_by_id = std::move(candidateIndex);
        state.children = std::move(candidateChildren);
        state.roots = std::move(candidateRoots);
        state.depths = std::move(candidateDepths);
        state.expanded.clear();
        state.selected.clear();
        state.focused = {};
        state.anchor = {};

        if (preserve_state)
        {
            for (const std::uint64_t id : oldExpanded)
            {
                if (state.index_by_id.contains(id))
                    state.expanded.insert(id);
                else
                    ++result.stale_expansions_removed;
            }
            for (const std::uint64_t id : oldSelected)
            {
                const auto found = state.index_by_id.find(id);
                if (found != state.index_by_id.end()
                    && state.selectable(found->second))
                    state.selected.insert(id);
                else
                    ++result.stale_selections_removed;
            }
            if (state.index_by_id.contains(oldFocus.value))
                state.focused = oldFocus;
            else
                result.focus_changed = static_cast<bool>(oldFocus);
        }
        state.rebuild_selected_ids();
        ++state.model_revision;
        state.rebuild_view();
        result.committed_nodes = state.nodes.size();
        result.visible_rows = state.visible.size();
        result.committed = true;
        return result;
    }

    MutationResult Controller::set_filter(FilterOptions options)
    {
        auto& state = *implementation_;
        if (options.query.size() > state.limits.maximum_filter_bytes)
            return {ErrorCode::filter_too_long, false, false};
        if (state.filter == options.query
            && state.include_descendants
                == options.include_descendants_of_matches)
            return {};
        state.filter.assign(options.query);
        state.include_descendants = options.include_descendants_of_matches;
        state.rebuild_view();
        return {ErrorCode::none, true, true};
    }

    MutationResult Controller::clear_filter()
    {
        return set_filter({});
    }

    MutationResult Controller::set_expanded(NodeId id, bool expanded)
    {
        return implementation_->expansion(id, expanded);
    }

    MutationResult Controller::toggle_expanded(NodeId id)
    {
        return implementation_->expansion(
            id,
            !implementation_->expanded.contains(id.value));
    }

    SelectionResult Controller::select(NodeId id, SelectionMode mode)
    {
        auto& state = *implementation_;
        SelectionResult result{
            .focused = state.focused,
            .anchor = state.anchor,
            .selected_count = state.selected.size()};
        const std::size_t sourceIndex = state.index_of(id);
        if (sourceIndex >= state.nodes.size())
        {
            result.error = ErrorCode::unknown_node;
            result.accepted = false;
            return result;
        }
        if (!state.selectable(sourceIndex))
        {
            result.error = ErrorCode::disabled_node;
            result.accepted = false;
            return result;
        }
        const std::size_t targetVisibleIndex = state.visible_index(id);
        if (targetVisibleIndex >= state.visible.size())
        {
            result.error = ErrorCode::node_not_visible;
            result.accepted = false;
            return result;
        }

        std::unordered_set<std::uint64_t> candidate{};
        NodeId candidateAnchor = state.anchor;
        switch (mode)
        {
        case SelectionMode::replace:
            candidate.insert(id.value);
            candidateAnchor = id;
            break;
        case SelectionMode::toggle:
            candidate = state.selected;
            if (candidate.erase(id.value) == 0u)
            {
                candidate.insert(id.value);
                candidateAnchor = id;
            }
            else if (candidateAnchor == id)
            {
                candidateAnchor = {};
            }
            break;
        case SelectionMode::range:
        case SelectionMode::add_range:
        {
            if (mode == SelectionMode::add_range)
                candidate = state.selected;
            std::size_t anchorVisibleIndex = state.visible_index(candidateAnchor);
            if (anchorVisibleIndex >= state.visible.size())
            {
                candidateAnchor = id;
                anchorVisibleIndex = targetVisibleIndex;
            }
            const std::size_t first = std::min(
                anchorVisibleIndex, targetVisibleIndex);
            const std::size_t pastLast = std::max(
                anchorVisibleIndex, targetVisibleIndex) + 1u;
            for (std::size_t index = first; index < pastLast; ++index)
            {
                const VisibleRow& row = state.visible[index];
                if (row.node && !row.node->disabled)
                    candidate.insert(row.node->id.value);
            }
            break;
        }
        }

        if (candidate.size() > state.limits.maximum_selection)
        {
            result.error = ErrorCode::selection_limit_exceeded;
            result.accepted = false;
            return result;
        }

        const NodeId previousFocus = state.focused;
        const NodeId previousAnchor = state.anchor;
        result.changed = candidate != state.selected;
        state.selected = std::move(candidate);
        state.focused = id;
        if (!candidateAnchor && !state.selected.empty())
        {
            const auto firstSelected = std::ranges::find_if(
                state.nodes,
                [&](const Node& node)
                {
                    return state.selected.contains(node.id.value);
                });
            candidateAnchor = firstSelected == state.nodes.end()
                ? NodeId{}
                : firstSelected->id;
        }
        state.anchor = candidateAnchor;
        state.rebuild_selected_ids();
        state.refresh_row_state();
        result.focus_changed = previousFocus != state.focused;
        result.anchor_changed = previousAnchor != state.anchor;
        if (result.changed || result.focus_changed || result.anchor_changed)
            ++state.view_revision;
        result.focused = state.focused;
        result.anchor = state.anchor;
        result.selected_count = state.selected.size();
        return result;
    }

    bool Controller::clear_selection()
    {
        auto& state = *implementation_;
        if (state.selected.empty() && !state.anchor)
            return false;
        state.selected.clear();
        state.selected_ids.clear();
        state.anchor = {};
        state.refresh_row_state();
        ++state.view_revision;
        return true;
    }

    NavigationResult Controller::navigate(
        NavigationCommand command,
        bool extend_selection)
    {
        auto& state = *implementation_;
        NavigationResult result{.focused = state.focused};
        if (state.visible.empty())
            return result;

        const auto first_selectable = [&](bool reverse) -> NodeId
        {
            if (reverse)
            {
                for (auto iterator = state.visible.rbegin();
                     iterator != state.visible.rend(); ++iterator)
                {
                    if (iterator->node && !iterator->node->disabled)
                        return iterator->node->id;
                }
                return {};
            }
            for (const VisibleRow& row : state.visible)
            {
                if (row.node && !row.node->disabled)
                    return row.node->id;
            }
            return {};
        };
        const auto adjacent_selectable = [&](std::size_t from, int step) -> NodeId
        {
            std::ptrdiff_t cursor = static_cast<std::ptrdiff_t>(from) + step;
            while (cursor >= 0
                && cursor < static_cast<std::ptrdiff_t>(state.visible.size()))
            {
                const VisibleRow& row = state.visible[static_cast<std::size_t>(cursor)];
                if (row.node && !row.node->disabled)
                    return row.node->id;
                cursor += step;
            }
            return {};
        };

        NodeId target = state.focused;
        std::size_t currentVisibleIndex = state.visible_index(state.focused);
        if (currentVisibleIndex >= state.visible.size())
        {
            target = first_selectable(command == NavigationCommand::last
                || command == NavigationCommand::previous);
        }
        else
        {
            const NodeId currentId = state.visible[currentVisibleIndex].node->id;
            const std::size_t sourceIndex = state.index_of(currentId);
            switch (command)
            {
            case NavigationCommand::first:
                target = first_selectable(false);
                break;
            case NavigationCommand::last:
                target = first_selectable(true);
                break;
            case NavigationCommand::previous:
                if (const NodeId adjacent = adjacent_selectable(
                        currentVisibleIndex, -1))
                    target = adjacent;
                break;
            case NavigationCommand::next:
                if (const NodeId adjacent = adjacent_selectable(
                        currentVisibleIndex, 1))
                    target = adjacent;
                break;
            case NavigationCommand::parent:
                if (sourceIndex < state.nodes.size())
                {
                    const NodeId parent = state.nodes[sourceIndex].parent;
                    const std::size_t parentIndex = state.index_of(parent);
                    if (parentIndex < state.nodes.size()
                        && state.selectable(parentIndex)
                        && state.visible_index(parent) < state.visible.size())
                        target = parent;
                }
                break;
            case NavigationCommand::first_child:
                if (sourceIndex < state.children.size())
                {
                    for (const std::size_t child : state.children[sourceIndex])
                    {
                        const NodeId childId = state.nodes[child].id;
                        if (state.selectable(child)
                            && state.visible_index(childId) < state.visible.size())
                        {
                            target = childId;
                            break;
                        }
                    }
                }
                break;
            case NavigationCommand::collapse_or_parent:
                if (sourceIndex < state.children.size()
                    && !state.children[sourceIndex].empty()
                    && state.expanded.contains(currentId.value))
                {
                    const MutationResult mutation = state.expansion(currentId, false);
                    result.expansion_changed = mutation.changed;
                }
                else if (sourceIndex < state.nodes.size())
                {
                    const NodeId parent = state.nodes[sourceIndex].parent;
                    const std::size_t parentIndex = state.index_of(parent);
                    if (parentIndex < state.nodes.size()
                        && state.selectable(parentIndex)
                        && state.visible_index(parent) < state.visible.size())
                        target = parent;
                }
                break;
            case NavigationCommand::expand_or_child:
                if (sourceIndex < state.children.size()
                    && !state.children[sourceIndex].empty())
                {
                    if (!state.expanded.contains(currentId.value))
                    {
                        const MutationResult mutation = state.expansion(currentId, true);
                        result.expansion_changed = mutation.changed;
                    }
                    else
                    {
                        for (const std::size_t child : state.children[sourceIndex])
                        {
                            if (state.selectable(child))
                            {
                                target = state.nodes[child].id;
                                break;
                            }
                        }
                    }
                }
                break;
            case NavigationCommand::toggle_expansion:
            {
                const MutationResult mutation = toggle_expanded(currentId);
                result.error = mutation.error;
                result.accepted = mutation.accepted;
                result.expansion_changed = mutation.changed;
                break;
            }
            }
        }

        if (target && target != state.focused)
        {
            const SelectionResult selection = select(
                target,
                extend_selection ? SelectionMode::range
                                 : SelectionMode::replace);
            result.error = selection.error;
            result.accepted = selection.accepted;
            result.focus_changed = selection.focus_changed;
        }
        else if (target && !state.focused)
        {
            const SelectionResult selection = select(target);
            result.error = selection.error;
            result.accepted = selection.accepted;
            result.focus_changed = selection.focus_changed;
        }
        result.focused = state.focused;
        return result;
    }

    MutationResult Controller::replace_context_actions(
        std::span<const ContextAction> supplied)
    {
        auto& state = *implementation_;
        if (supplied.size() > state.limits.maximum_context_actions)
            return {ErrorCode::action_limit_exceeded, false, false};

        std::vector<ContextAction> candidate{supplied.begin(), supplied.end()};
        std::unordered_set<std::uint64_t> ids{};
        ids.reserve(candidate.size());
        for (const ContextAction& action : candidate)
        {
            if (!action.id || action.label.empty() || action.command.empty())
                return {ErrorCode::invalid_action, false, false};
            if (action.label.size() + action.command.size()
                > state.limits.maximum_idless_text_bytes)
                return {ErrorCode::text_limit_exceeded, false, false};
            if (!ids.insert(action.id.value).second)
                return {ErrorCode::duplicate_action, false, false};
        }
        std::ranges::sort(candidate, [](const ContextAction& left,
                                        const ContextAction& right)
        {
            if (left.order != right.order)
                return left.order < right.order;
            if (left.label != right.label)
                return left.label < right.label;
            return left.id < right.id;
        });
        if (candidate == state.actions)
            return {};
        state.actions = std::move(candidate);
        ++state.model_revision;
        return {ErrorCode::none, true, true};
    }

    ContextRoute Controller::route_context_action(
        ActionId actionId,
        NodeId invokedOn) const
    {
        const auto& state = *implementation_;
        ContextRoute result{.action = actionId, .invoked_on = invokedOn};
        const auto action = std::ranges::find_if(
            state.actions,
            [&](const ContextAction& candidate)
            {
                return candidate.id == actionId;
            });
        if (action == state.actions.end())
        {
            result.error = ErrorCode::invalid_action;
            return result;
        }
        if (!action->enabled)
        {
            result.error = ErrorCode::action_disabled;
            return result;
        }
        const std::size_t invokedIndex = state.index_of(invokedOn);
        if (invokedIndex >= state.nodes.size())
        {
            result.error = ErrorCode::unknown_node;
            return result;
        }
        if (state.nodes[invokedIndex].disabled)
        {
            result.error = ErrorCode::disabled_node;
            return result;
        }

        if (action->use_selected_set
            && state.selected.contains(invokedOn.value)
            && !state.selected_ids.empty())
            result.targets = state.selected_ids;
        else
            result.targets.emplace_back(invokedOn);

        if (result.targets.empty())
        {
            result.error = ErrorCode::no_action_targets;
            return result;
        }
        for (const NodeId target : result.targets)
        {
            const std::size_t index = state.index_of(target);
            if (index >= state.nodes.size())
            {
                result.error = ErrorCode::unknown_node;
                result.targets.clear();
                return result;
            }
            if (state.nodes[index].disabled)
            {
                result.error = ErrorCode::disabled_node;
                result.targets.clear();
                return result;
            }
            if (state.nodes[index].locked && !action->allow_locked_targets)
            {
                result.error = ErrorCode::locked_node;
                result.targets.clear();
                return result;
            }
        }
        result.command = action->command;
        result.accepted = true;
        return result;
    }

    LayoutPlan Controller::plan_rows(Viewport viewport) const
    {
        const auto& state = *implementation_;
        LayoutPlan result{};
        if (!viewport.valid() || !std::isfinite(viewport.row_height)
            || !std::isfinite(viewport.viewport_height)
            || !std::isfinite(viewport.scroll_y))
        {
            result.error = ErrorCode::invalid_viewport;
            return result;
        }

        const std::size_t total = state.visible.size();
        if (total > 0u
            && viewport.row_height
                > std::numeric_limits<float>::max()
                    / static_cast<float>(total))
        {
            result.error = ErrorCode::invalid_viewport;
            return result;
        }
        const float contentHeight = viewport.row_height
            * static_cast<float>(total);
        const float maximumScroll = std::max(
            0.0f, contentHeight - viewport.viewport_height);
        const float scroll = std::clamp(
            viewport.scroll_y, 0.0f, maximumScroll);
        std::size_t first{};
        std::size_t pastLast{};
        if (total > 0u)
        {
            const float firstVisible = std::floor(scroll / viewport.row_height);
            first = std::min(
                static_cast<std::size_t>(firstVisible), total - 1u);
            const std::size_t visibleCount = std::max<std::size_t>(
                1u,
                static_cast<std::size_t>(std::ceil(
                    viewport.viewport_height / viewport.row_height)) + 1u);
            const std::size_t visiblePast = std::min(
                total, first + visibleCount);
            first = first > viewport.overscan_rows
                ? first - viewport.overscan_rows
                : 0u;
            pastLast = std::min(
                total, visiblePast + viewport.overscan_rows);
        }
        result.range = {
            .first = first,
            .past_last = pastLast,
            .total_rows = total,
            .content_height = contentHeight,
            .scroll_y = scroll,
            .maximum_scroll_y = maximumScroll,
            .valid = true};
        result.rows.reserve(pastLast - first);
        for (std::size_t index = first; index < pastLast; ++index)
        {
            const float top = viewport.row_height * static_cast<float>(index);
            result.rows.emplace_back(RowLayout{
                .row = &state.visible[index],
                .top = top,
                .bottom = top + viewport.row_height});
        }
        return result;
    }

    ScrollPlan Controller::scroll_to_visible(
        NodeId id,
        Viewport viewport) const
    {
        const auto& state = *implementation_;
        ScrollPlan result{
            .target = id,
            .previous_scroll_y = viewport.scroll_y,
            .scroll_y = viewport.scroll_y};
        const LayoutPlan layout = plan_rows(viewport);
        if (!layout)
        {
            result.error = layout.error;
            return result;
        }
        const std::size_t index = state.visible_index(id);
        if (index >= state.visible.size())
        {
            result.error = state.find(id)
                ? ErrorCode::node_not_visible
                : ErrorCode::unknown_node;
            return result;
        }
        const float rowTop = viewport.row_height * static_cast<float>(index);
        const float rowBottom = rowTop + viewport.row_height;
        float scroll = layout.range.scroll_y;
        if (rowTop < scroll)
            scroll = rowTop;
        else if (rowBottom > scroll + viewport.viewport_height)
            scroll = rowBottom - viewport.viewport_height;
        scroll = std::clamp(
            scroll, 0.0f, layout.range.maximum_scroll_y);
        result.scroll_y = scroll;
        result.changed = scroll != result.previous_scroll_y;
        return result;
    }

    const Error& Controller::error() const noexcept
    {
        return implementation_->error;
    }

    std::string_view Controller::filter() const noexcept
    {
        return implementation_->filter;
    }

    bool Controller::includes_descendants_of_matches() const noexcept
    {
        return implementation_->include_descendants;
    }

    NodeId Controller::focused_id() const noexcept
    {
        return implementation_->focused;
    }

    NodeId Controller::selection_anchor() const noexcept
    {
        return implementation_->anchor;
    }

    const Node* Controller::find_node(NodeId id) const noexcept
    {
        return implementation_->find(id);
    }

    bool Controller::is_expanded(NodeId id) const noexcept
    {
        return implementation_->expanded.contains(id.value);
    }

    bool Controller::is_selected(NodeId id) const noexcept
    {
        return implementation_->selected.contains(id.value);
    }

    std::span<const Node> Controller::nodes() const noexcept
    {
        return implementation_->nodes;
    }

    std::span<const VisibleRow> Controller::visible_rows() const noexcept
    {
        return implementation_->visible;
    }

    std::span<const NodeId> Controller::selected_ids() const noexcept
    {
        return implementation_->selected_ids;
    }

    std::span<const ContextAction> Controller::context_actions() const noexcept
    {
        return implementation_->actions;
    }

    const Summary& Controller::summary() const noexcept
    {
        return implementation_->summary;
    }

    std::uint64_t Controller::model_revision() const noexcept
    {
        return implementation_->model_revision;
    }

    std::uint64_t Controller::view_revision() const noexcept
    {
        return implementation_->view_revision;
    }
}
