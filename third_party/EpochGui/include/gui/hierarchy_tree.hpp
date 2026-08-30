// SPDX-License-Identifier: LicenseRef-MIT-NoSell
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace epochengine::gui_lib::hierarchy_tree
{
    struct NodeId final
    {
        std::uint64_t value{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0u;
        }

        friend constexpr auto operator<=>(NodeId, NodeId) noexcept = default;
    };

    struct ActionId final
    {
        std::uint64_t value{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0u;
        }

        friend constexpr auto operator<=>(ActionId, ActionId) noexcept = default;
    };

    enum class NodeKind : std::uint8_t
    {
        item,
        group,
        document,
        component,
        evidence
    };

    struct Node final
    {
        NodeId id{};
        NodeId parent{};
        std::string label{};
        std::string detail{};
        std::string search_terms{};
        std::string context_tag{};
        std::uint64_t order{};
        NodeKind kind{NodeKind::item};
        bool expandable{};
        bool disabled{};
        bool locked{};
    };

    struct Limits final
    {
        std::size_t maximum_nodes{65'536u};
        std::size_t maximum_depth{128u};
        std::size_t maximum_idless_text_bytes{2'048u};
        std::size_t maximum_filter_bytes{512u};
        std::size_t maximum_selection{16'384u};
        std::size_t maximum_context_actions{256u};

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return maximum_nodes > 0u && maximum_depth > 0u
                && maximum_idless_text_bytes > 0u
                && maximum_filter_bytes > 0u && maximum_selection > 0u
                && maximum_context_actions > 0u;
        }
    };

    enum class ErrorCode : std::uint8_t
    {
        none,
        invalid_limits,
        node_limit_exceeded,
        invalid_id,
        invalid_kind,
        duplicate_id,
        empty_label,
        text_limit_exceeded,
        missing_parent,
        self_parent,
        hierarchy_cycle,
        hierarchy_depth_exceeded,
        filter_too_long,
        unknown_node,
        node_not_visible,
        disabled_node,
        locked_node,
        selection_limit_exceeded,
        invalid_viewport,
        invalid_action,
        duplicate_action,
        action_limit_exceeded,
        action_disabled,
        no_action_targets
    };

    [[nodiscard]] constexpr std::string_view error_code_name(
        ErrorCode code) noexcept
    {
        switch (code)
        {
        case ErrorCode::none: return "none";
        case ErrorCode::invalid_limits: return "invalid_limits";
        case ErrorCode::node_limit_exceeded: return "node_limit_exceeded";
        case ErrorCode::invalid_id: return "invalid_id";
        case ErrorCode::invalid_kind: return "invalid_kind";
        case ErrorCode::duplicate_id: return "duplicate_id";
        case ErrorCode::empty_label: return "empty_label";
        case ErrorCode::text_limit_exceeded: return "text_limit_exceeded";
        case ErrorCode::missing_parent: return "missing_parent";
        case ErrorCode::self_parent: return "self_parent";
        case ErrorCode::hierarchy_cycle: return "hierarchy_cycle";
        case ErrorCode::hierarchy_depth_exceeded:
            return "hierarchy_depth_exceeded";
        case ErrorCode::filter_too_long: return "filter_too_long";
        case ErrorCode::unknown_node: return "unknown_node";
        case ErrorCode::node_not_visible: return "node_not_visible";
        case ErrorCode::disabled_node: return "disabled_node";
        case ErrorCode::locked_node: return "locked_node";
        case ErrorCode::selection_limit_exceeded:
            return "selection_limit_exceeded";
        case ErrorCode::invalid_viewport: return "invalid_viewport";
        case ErrorCode::invalid_action: return "invalid_action";
        case ErrorCode::duplicate_action: return "duplicate_action";
        case ErrorCode::action_limit_exceeded:
            return "action_limit_exceeded";
        case ErrorCode::action_disabled: return "action_disabled";
        case ErrorCode::no_action_targets: return "no_action_targets";
        }
        return "unknown";
    }

    struct Error final
    {
        ErrorCode code{ErrorCode::none};
        NodeId node{};
        ActionId action{};
        std::string message{};

        [[nodiscard]] bool present() const noexcept
        {
            return code != ErrorCode::none;
        }
    };

    struct MutationResult final
    {
        ErrorCode error{ErrorCode::none};
        bool accepted{true};
        bool changed{};
    };

    struct ReplaceResult final
    {
        ErrorCode error{ErrorCode::none};
        std::size_t supplied_nodes{};
        std::size_t committed_nodes{};
        std::size_t visible_rows{};
        std::size_t stale_expansions_removed{};
        std::size_t stale_selections_removed{};
        bool committed{};
        bool focus_changed{};
    };

    struct FilterOptions final
    {
        std::string_view query{};
        bool include_descendants_of_matches{true};
    };

    struct VisibleRow final
    {
        const Node* node{};
        std::size_t source_index{};
        std::size_t visible_index{};
        std::size_t depth{};
        bool has_children{};
        bool expanded{};
        bool expansion_forced{};
        bool direct_match{};
        bool selected{};
        bool focused{};
        bool selection_anchor{};
    };

    enum class SelectionMode : std::uint8_t
    {
        replace,
        toggle,
        range,
        add_range
    };

    struct SelectionResult final
    {
        ErrorCode error{ErrorCode::none};
        NodeId focused{};
        NodeId anchor{};
        std::size_t selected_count{};
        bool accepted{true};
        bool changed{};
        bool focus_changed{};
        bool anchor_changed{};
    };

    enum class NavigationCommand : std::uint8_t
    {
        first,
        previous,
        next,
        last,
        parent,
        first_child,
        collapse_or_parent,
        expand_or_child,
        toggle_expansion
    };

    struct NavigationResult final
    {
        ErrorCode error{ErrorCode::none};
        NodeId focused{};
        bool accepted{true};
        bool focus_changed{};
        bool expansion_changed{};
    };

    struct Viewport final
    {
        float row_height{22.0f};
        float viewport_height{};
        float scroll_y{};
        std::size_t overscan_rows{2u};

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return row_height > 0.0f && viewport_height >= 0.0f
                && scroll_y >= 0.0f;
        }
    };

    struct VisibleRange final
    {
        std::size_t first{};
        std::size_t past_last{};
        std::size_t total_rows{};
        float content_height{};
        float scroll_y{};
        float maximum_scroll_y{};
        bool valid{};

        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return first >= past_last;
        }
    };

    struct RowLayout final
    {
        const VisibleRow* row{};
        float top{};
        float bottom{};
    };

    struct LayoutPlan final
    {
        ErrorCode error{ErrorCode::none};
        VisibleRange range{};
        std::vector<RowLayout> rows{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == ErrorCode::none && range.valid;
        }
    };

    struct ScrollPlan final
    {
        ErrorCode error{ErrorCode::none};
        NodeId target{};
        float previous_scroll_y{};
        float scroll_y{};
        bool changed{};
    };

    struct ContextAction final
    {
        ActionId id{};
        std::string label{};
        std::string command{};
        std::uint64_t order{};
        bool enabled{true};
        bool allow_locked_targets{};
        bool use_selected_set{true};

        friend bool operator==(const ContextAction&, const ContextAction&)
            = default;
    };

    struct ContextRoute final
    {
        ErrorCode error{ErrorCode::none};
        ActionId action{};
        NodeId invoked_on{};
        std::string command{};
        std::vector<NodeId> targets{};
        bool accepted{};
    };

    struct Summary final
    {
        std::size_t total_nodes{};
        std::size_t visible_rows{};
        std::size_t selected_nodes{};
        std::size_t expanded_nodes{};
        std::size_t direct_matches{};
        std::size_t disabled_nodes{};
        std::size_t locked_nodes{};
        bool filter_active{};
        bool focus_visible{};
    };

    class Controller final
    {
    public:
        explicit Controller(Limits limits = {});
        ~Controller();

        Controller(const Controller&) = delete;
        Controller& operator=(const Controller&) = delete;
        Controller(Controller&&) noexcept;
        Controller& operator=(Controller&&) noexcept;

        [[nodiscard]] const Limits& limits() const noexcept;
        [[nodiscard]] ReplaceResult replace_nodes(
            std::span<const Node> nodes,
            bool preserve_state = true);
        [[nodiscard]] MutationResult set_filter(FilterOptions options);
        [[nodiscard]] MutationResult clear_filter();
        [[nodiscard]] MutationResult set_expanded(NodeId id, bool expanded);
        [[nodiscard]] MutationResult toggle_expanded(NodeId id);
        [[nodiscard]] SelectionResult select(
            NodeId id,
            SelectionMode mode = SelectionMode::replace);
        [[nodiscard]] bool clear_selection();
        [[nodiscard]] NavigationResult navigate(
            NavigationCommand command,
            bool extend_selection = false);

        [[nodiscard]] MutationResult replace_context_actions(
            std::span<const ContextAction> actions);
        [[nodiscard]] ContextRoute route_context_action(
            ActionId action,
            NodeId invoked_on) const;

        [[nodiscard]] LayoutPlan plan_rows(Viewport viewport) const;
        [[nodiscard]] ScrollPlan scroll_to_visible(
            NodeId id,
            Viewport viewport) const;

        [[nodiscard]] const Error& error() const noexcept;
        [[nodiscard]] std::string_view filter() const noexcept;
        [[nodiscard]] bool includes_descendants_of_matches() const noexcept;
        [[nodiscard]] NodeId focused_id() const noexcept;
        [[nodiscard]] NodeId selection_anchor() const noexcept;
        [[nodiscard]] const Node* find_node(NodeId id) const noexcept;
        [[nodiscard]] bool is_expanded(NodeId id) const noexcept;
        [[nodiscard]] bool is_selected(NodeId id) const noexcept;
        [[nodiscard]] std::span<const Node> nodes() const noexcept;
        [[nodiscard]] std::span<const VisibleRow> visible_rows() const noexcept;
        [[nodiscard]] std::span<const NodeId> selected_ids() const noexcept;
        [[nodiscard]] std::span<const ContextAction> context_actions() const noexcept;
        [[nodiscard]] const Summary& summary() const noexcept;
        [[nodiscard]] std::uint64_t model_revision() const noexcept;
        [[nodiscard]] std::uint64_t view_revision() const noexcept;

    private:
        struct Implementation;
        std::unique_ptr<Implementation> implementation_;
    };
}
