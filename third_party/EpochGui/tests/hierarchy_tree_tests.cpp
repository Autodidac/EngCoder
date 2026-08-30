// SPDX-License-Identifier: LicenseRef-MIT-NoSell

#include <gui/hierarchy_tree.hpp>

#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace epochengine::gui_lib::hierarchy_tree;

    [[nodiscard]] int check(bool condition, int line) noexcept
    {
        return condition ? 0 : line;
    }

#define EPOCHGUI_CHECK(condition) \
    do { const int failure = check((condition), __LINE__); if (failure != 0) return failure; } while (false)

    [[nodiscard]] Node node(
        std::uint64_t id,
        std::string label,
        std::uint64_t parent = 0u,
        std::uint64_t order = 0u,
        bool expandable = false,
        bool disabled = false,
        bool locked = false,
        std::string search = {})
    {
        Node value{};
        value.id = NodeId{id};
        value.parent = NodeId{parent};
        value.label = std::move(label);
        value.search_terms = std::move(search);
        value.order = order;
        value.kind = expandable ? NodeKind::group : NodeKind::item;
        value.expandable = expandable;
        value.disabled = disabled;
        value.locked = locked;
        return value;
    }

    [[nodiscard]] std::vector<Node> fixture()
    {
        return {
            node(6u, "Leaf", 5u, 0u, false, false, false, "needle evidence"),
            node(3u, "Disabled", 1u, 1u, false, true),
            node(7u, "Other", 0u, 1u, true),
            node(5u, "Folder", 1u, 3u, true),
            node(1u, "Root", 0u, 0u, true),
            node(4u, "Locked", 1u, 2u, false, false, true),
            node(2u, "Alpha", 1u, 0u)
        };
    }

    [[nodiscard]] bool visible_ids_equal(
        const Controller& controller,
        std::initializer_list<std::uint64_t> expected)
    {
        const auto rows = controller.visible_rows();
        if (rows.size() != expected.size())
            return false;
        std::size_t index{};
        for (const std::uint64_t id : expected)
        {
            if (!rows[index].node || rows[index].node->id != NodeId{id})
                return false;
            ++index;
        }
        return true;
    }

    [[nodiscard]] int admission_and_expansion()
    {
        Controller controller{};
        const std::vector<Node> values = fixture();
        ReplaceResult refresh = controller.replace_nodes(values);
        EPOCHGUI_CHECK(refresh.committed);
        EPOCHGUI_CHECK(refresh.committed_nodes == values.size());
        EPOCHGUI_CHECK(visible_ids_equal(controller, {1u, 7u}));
        EPOCHGUI_CHECK(controller.summary().total_nodes == 7u);
        EPOCHGUI_CHECK(controller.summary().disabled_nodes == 1u);
        EPOCHGUI_CHECK(controller.summary().locked_nodes == 1u);

        MutationResult mutation = controller.set_expanded(NodeId{1u}, true);
        EPOCHGUI_CHECK(mutation.accepted && mutation.changed);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {1u, 2u, 3u, 4u, 5u, 7u}));
        mutation = controller.set_expanded(NodeId{5u}, true);
        EPOCHGUI_CHECK(mutation.changed);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {1u, 2u, 3u, 4u, 5u, 6u, 7u}));
        EPOCHGUI_CHECK(controller.visible_rows()[5].depth == 2u);

        const std::size_t committedCount = controller.nodes().size();
        std::vector<Node> invalid = values;
        invalid.push_back(values.front());
        refresh = controller.replace_nodes(invalid);
        EPOCHGUI_CHECK(!refresh.committed);
        EPOCHGUI_CHECK(refresh.error == ErrorCode::duplicate_id);
        EPOCHGUI_CHECK(controller.nodes().size() == committedCount);

        invalid = {node(10u, "Orphan", 99u)};
        refresh = controller.replace_nodes(invalid);
        EPOCHGUI_CHECK(refresh.error == ErrorCode::missing_parent);
        invalid = {node(10u, "A", 11u), node(11u, "B", 10u)};
        refresh = controller.replace_nodes(invalid);
        EPOCHGUI_CHECK(refresh.error == ErrorCode::hierarchy_cycle);

        Controller tiny{Limits{.maximum_nodes = 2u}};
        refresh = tiny.replace_nodes(values);
        EPOCHGUI_CHECK(refresh.error == ErrorCode::node_limit_exceeded);
        return 0;
    }

    [[nodiscard]] int filter_and_state_retention()
    {
        Controller controller{};
        std::vector<Node> values = fixture();
        EPOCHGUI_CHECK(controller.replace_nodes(values).committed);
        EPOCHGUI_CHECK(controller.set_filter({"needle", false}).changed);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {1u, 5u, 6u}));
        EPOCHGUI_CHECK(controller.visible_rows()[0].expansion_forced);
        EPOCHGUI_CHECK(controller.visible_rows()[1].expansion_forced);
        EPOCHGUI_CHECK(controller.visible_rows()[2].direct_match);
        EPOCHGUI_CHECK(controller.summary().direct_matches == 1u);

        EPOCHGUI_CHECK(controller.set_filter({"folder", true}).changed);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {1u, 5u, 6u}));
        EPOCHGUI_CHECK(controller.visible_rows()[1].direct_match);
        EPOCHGUI_CHECK(controller.clear_filter().changed);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {1u, 7u}));

        EPOCHGUI_CHECK(controller.set_expanded(NodeId{1u}, true).changed);
        EPOCHGUI_CHECK(controller.select(NodeId{2u}).accepted);
        values.erase(values.begin() + 6);
        const ReplaceResult refresh = controller.replace_nodes(values, true);
        EPOCHGUI_CHECK(refresh.committed);
        EPOCHGUI_CHECK(refresh.stale_selections_removed == 1u);
        EPOCHGUI_CHECK(controller.is_expanded(NodeId{1u}));
        EPOCHGUI_CHECK(controller.selected_ids().empty());
        return 0;
    }

    [[nodiscard]] int selection_and_navigation()
    {
        Controller controller{};
        const std::vector<Node> values = fixture();
        EPOCHGUI_CHECK(controller.replace_nodes(values).committed);
        EPOCHGUI_CHECK(controller.set_expanded(NodeId{1u}, true).changed);
        EPOCHGUI_CHECK(controller.set_expanded(NodeId{5u}, true).changed);

        SelectionResult selection = controller.select(NodeId{2u});
        EPOCHGUI_CHECK(selection.accepted && selection.selected_count == 1u);
        selection = controller.select(NodeId{6u}, SelectionMode::range);
        EPOCHGUI_CHECK(selection.accepted && selection.selected_count == 4u);
        EPOCHGUI_CHECK(!controller.is_selected(NodeId{3u}));
        EPOCHGUI_CHECK(controller.is_selected(NodeId{4u}));
        selection = controller.select(NodeId{4u}, SelectionMode::toggle);
        EPOCHGUI_CHECK(selection.selected_count == 3u);
        EPOCHGUI_CHECK(controller.select(NodeId{3u}).error == ErrorCode::disabled_node);
        EPOCHGUI_CHECK(controller.clear_selection());

        NavigationResult navigation = controller.navigate(NavigationCommand::first);
        EPOCHGUI_CHECK(navigation.focused == NodeId{1u});
        navigation = controller.navigate(NavigationCommand::next);
        EPOCHGUI_CHECK(navigation.focused == NodeId{2u});
        navigation = controller.navigate(NavigationCommand::next);
        EPOCHGUI_CHECK(navigation.focused == NodeId{4u});
        navigation = controller.navigate(NavigationCommand::next, true);
        EPOCHGUI_CHECK(navigation.focused == NodeId{5u});
        EPOCHGUI_CHECK(controller.selected_ids().size() == 2u);
        navigation = controller.navigate(NavigationCommand::collapse_or_parent);
        EPOCHGUI_CHECK(navigation.expansion_changed);
        EPOCHGUI_CHECK(!controller.is_expanded(NodeId{5u}));
        navigation = controller.navigate(NavigationCommand::expand_or_child);
        EPOCHGUI_CHECK(navigation.expansion_changed);
        navigation = controller.navigate(NavigationCommand::expand_or_child);
        EPOCHGUI_CHECK(navigation.focused == NodeId{6u});
        navigation = controller.navigate(NavigationCommand::parent);
        EPOCHGUI_CHECK(navigation.focused == NodeId{5u});
        return 0;
    }

    [[nodiscard]] int virtualization_and_scrolling()
    {
        Controller controller{};
        const std::vector<Node> values = fixture();
        EPOCHGUI_CHECK(controller.replace_nodes(values).committed);
        EPOCHGUI_CHECK(controller.set_expanded(NodeId{1u}, true).changed);
        EPOCHGUI_CHECK(controller.set_expanded(NodeId{5u}, true).changed);

        LayoutPlan layout = controller.plan_rows({
            .row_height = 20.0f,
            .viewport_height = 40.0f,
            .scroll_y = 100.0f,
            .overscan_rows = 1u});
        EPOCHGUI_CHECK(static_cast<bool>(layout));
        EPOCHGUI_CHECK(layout.range.total_rows == 7u);
        EPOCHGUI_CHECK(layout.range.first == 4u);
        EPOCHGUI_CHECK(layout.range.past_last == 7u);
        EPOCHGUI_CHECK(layout.rows.size() == 3u);
        EPOCHGUI_CHECK(layout.rows.front().row->node->id == NodeId{5u});
        EPOCHGUI_CHECK(layout.rows.front().top == 80.0f);

        ScrollPlan scroll = controller.scroll_to_visible(
            NodeId{7u},
            {.row_height = 20.0f, .viewport_height = 40.0f});
        EPOCHGUI_CHECK(scroll.error == ErrorCode::none);
        EPOCHGUI_CHECK(scroll.changed);
        EPOCHGUI_CHECK(scroll.scroll_y == 100.0f);
        layout = controller.plan_rows({
            .row_height = std::nanf(""), .viewport_height = 40.0f});
        EPOCHGUI_CHECK(layout.error == ErrorCode::invalid_viewport);

        EPOCHGUI_CHECK(controller.set_expanded(NodeId{1u}, false).changed);
        scroll = controller.scroll_to_visible(
            NodeId{6u},
            {.row_height = 20.0f, .viewport_height = 40.0f});
        EPOCHGUI_CHECK(scroll.error == ErrorCode::node_not_visible);
        return 0;
    }

    [[nodiscard]] int context_action_routing()
    {
        Controller controller{};
        const std::vector<Node> values = fixture();
        EPOCHGUI_CHECK(controller.replace_nodes(values).committed);
        EPOCHGUI_CHECK(controller.set_expanded(NodeId{1u}, true).changed);
        EPOCHGUI_CHECK(controller.select(NodeId{2u}).accepted);
        EPOCHGUI_CHECK(controller.select(NodeId{4u}, SelectionMode::add_range).accepted);

        const std::array actions{
            ContextAction{ActionId{10u}, "Rename", "hierarchy.rename", 20u},
            ContextAction{ActionId{11u}, "Inspect", "hierarchy.inspect", 10u,
                true, true, true},
            ContextAction{ActionId{12u}, "Disabled", "hierarchy.disabled", 30u,
                false, true, true}
        };
        MutationResult mutation = controller.replace_context_actions(actions);
        EPOCHGUI_CHECK(mutation.accepted && mutation.changed);
        EPOCHGUI_CHECK(controller.context_actions()[0].id == ActionId{11u});

        ContextRoute route = controller.route_context_action(
            ActionId{10u}, NodeId{2u});
        EPOCHGUI_CHECK(!route.accepted);
        EPOCHGUI_CHECK(route.error == ErrorCode::locked_node);
        route = controller.route_context_action(ActionId{11u}, NodeId{2u});
        EPOCHGUI_CHECK(route.accepted);
        EPOCHGUI_CHECK(route.command == "hierarchy.inspect");
        EPOCHGUI_CHECK(route.targets.size() == 2u);
        EPOCHGUI_CHECK(route.targets[0] == NodeId{2u});
        EPOCHGUI_CHECK(route.targets[1] == NodeId{4u});
        route = controller.route_context_action(ActionId{11u}, NodeId{5u});
        EPOCHGUI_CHECK(route.accepted);
        EPOCHGUI_CHECK(route.targets.size() == 1u);
        EPOCHGUI_CHECK(route.targets[0] == NodeId{5u});
        route = controller.route_context_action(ActionId{12u}, NodeId{5u});
        EPOCHGUI_CHECK(route.error == ErrorCode::action_disabled);

        std::array duplicate{
            ContextAction{ActionId{20u}, "A", "a"},
            ContextAction{ActionId{20u}, "B", "b"}
        };
        mutation = controller.replace_context_actions(duplicate);
        EPOCHGUI_CHECK(!mutation.accepted);
        EPOCHGUI_CHECK(mutation.error == ErrorCode::duplicate_action);
        EPOCHGUI_CHECK(controller.context_actions().size() == actions.size());
        return 0;
    }
}

int main()
{
    static_assert(error_code_name(ErrorCode::none) == "none");
    if (const int result = admission_and_expansion(); result != 0)
        return result;
    if (const int result = filter_and_state_retention(); result != 0)
        return result;
    if (const int result = selection_and_navigation(); result != 0)
        return result;
    if (const int result = virtualization_and_scrolling(); result != 0)
        return result;
    return context_action_routing();
}
