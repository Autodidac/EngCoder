// SPDX-License-Identifier: LicenseRef-MIT-NoSell

#include "../include/gui/node_graph_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
    using namespace epochengine::gui_lib::node_graph_workspace;

    [[nodiscard]] int check(bool condition, int line) noexcept
    {
        return condition ? 0 : line;
    }

#define EPOCHGUI_CHECK(condition) \
    do { const int failure = check((condition), __LINE__); if (failure != 0) return failure; } while (false)

    struct GraphFixture final
    {
        std::vector<NodeLayout> nodes{
            {.id = 3u, .bounds = {280.0, 200.0, 120.0, 80.0}, .layout_order = 30u},
            {.id = 1u, .bounds = {40.0, 40.0, 120.0, 80.0}, .layout_order = 10u},
            {.id = 2u, .bounds = {280.0, 40.0, 120.0, 80.0}, .layout_order = 20u}
        };
        std::vector<PinLayout> pins{
            {.id = 31u, .node_id = 3u, .direction = PinDirection::input,
                .position = {280.0, 240.0}, .layout_order = 1u},
            {.id = 22u, .node_id = 2u, .direction = PinDirection::output,
                .position = {400.0, 80.0}, .layout_order = 2u},
            {.id = 21u, .node_id = 2u, .direction = PinDirection::input,
                .position = {280.0, 80.0}, .layout_order = 1u},
            {.id = 11u, .node_id = 1u, .direction = PinDirection::output,
                .position = {160.0, 80.0}, .layout_order = 1u}
        };
        std::vector<EdgeLayout> edges{
            {.id = 101u, .output_pin_id = 11u, .input_pin_id = 21u,
                .layout_order = 4u}
        };

        [[nodiscard]] GraphLayoutInput input(std::uint64_t revision = 1u) const noexcept
        {
            return {
                .nodes = nodes,
                .pins = pins,
                .edges = edges,
                .source_revision = revision
            };
        }
    };

    [[nodiscard]] bool near(double left, double right) noexcept
    {
        return std::abs(left - right) < 1.0e-8;
    }

    [[nodiscard]] Vec2 center(Rect bounds) noexcept
    {
        return {
            bounds.x + bounds.width * 0.5,
            bounds.y + bounds.height * 0.5
        };
    }

    [[nodiscard]] Vec2 curve_point(const CubicCurve& curve, double t) noexcept
    {
        const double inverse = 1.0 - t;
        const double a = inverse * inverse * inverse;
        const double b = 3.0 * inverse * inverse * t;
        const double c = 3.0 * inverse * t * t;
        const double d = t * t * t;
        return {
            a * curve.start.x + b * curve.control1.x
                + c * curve.control2.x + d * curve.end.x,
            a * curve.start.y + b * curve.control1.y
                + c * curve.control2.y + d * curve.end.y
        };
    }

    [[nodiscard]] const NodeProjection* find_node(
        const Controller& controller,
        NodeId id) noexcept
    {
        const auto nodes = controller.node_projections();
        const auto found = std::ranges::find(nodes, id, &NodeProjection::id);
        return found == nodes.end() ? nullptr : &*found;
    }

    [[nodiscard]] const PinProjection* find_pin(
        const Controller& controller,
        PinId id) noexcept
    {
        const auto pins = controller.pin_projections();
        const auto found = std::ranges::find(pins, id, &PinProjection::id);
        return found == pins.end() ? nullptr : &*found;
    }

    int deterministic_projection_and_bounds()
    {
        GraphFixture graph{};
        Controller controller{};
        EPOCHGUI_CHECK(controller.set_viewport({10.0, 20.0, 700.0, 500.0}).changed);
        const ReplaceResult replaced = controller.replace_graph(graph.input(77u));
        EPOCHGUI_CHECK(replaced.committed);
        EPOCHGUI_CHECK(replaced.node_count == 3u);
        EPOCHGUI_CHECK(replaced.pin_count == 4u);
        EPOCHGUI_CHECK(replaced.edge_count == 1u);
        EPOCHGUI_CHECK(controller.source_revision() == 77u);
        EPOCHGUI_CHECK(controller.nodes()[0].id == 1u);
        EPOCHGUI_CHECK(controller.nodes()[1].id == 2u);
        EPOCHGUI_CHECK(controller.nodes()[2].id == 3u);
        EPOCHGUI_CHECK(controller.node_projections()[0].id == 1u);
        EPOCHGUI_CHECK(controller.edge_projections()[0].id == 101u);

        std::ranges::reverse(graph.nodes);
        std::ranges::reverse(graph.pins);
        const ReplaceResult reordered = controller.replace_graph(graph.input(78u));
        EPOCHGUI_CHECK(reordered.committed);
        EPOCHGUI_CHECK(controller.nodes()[0].id == 1u);
        EPOCHGUI_CHECK(controller.pins()[0].node_id == 1u);

        const Rect committed_bounds = controller.nodes()[0].bounds;
        graph.nodes.push_back(graph.nodes.front());
        const ReplaceResult duplicate = controller.replace_graph(graph.input(79u));
        EPOCHGUI_CHECK(!duplicate.committed);
        EPOCHGUI_CHECK(duplicate.error == ErrorCode::duplicate_node_id);
        EPOCHGUI_CHECK(controller.using_stale_graph());
        EPOCHGUI_CHECK(controller.source_revision() == 78u);
        EPOCHGUI_CHECK(controller.nodes()[0].bounds == committed_bounds);

        Controller one_node{Limits{.maximum_nodes = 1u}};
        GraphFixture too_large{};
        EPOCHGUI_CHECK(one_node.replace_graph(too_large.input()).error
            == ErrorCode::node_limit_exceeded);

        Controller one_selection{Limits{.maximum_selected_items = 1u}};
        EPOCHGUI_CHECK(one_selection.replace_graph(too_large.input()).committed);
        EPOCHGUI_CHECK(one_selection.select_node(1u).accepted);
        const SelectionResult selection = one_selection.select_node(2u, SelectionMode::add);
        EPOCHGUI_CHECK(!selection.accepted);
        EPOCHGUI_CHECK(selection.error == ErrorCode::selection_limit_exceeded);
        EPOCHGUI_CHECK(one_selection.selected_node_ids().size() == 1u);
        return 0;
    }

    int viewport_transforms_and_fit()
    {
        GraphFixture graph{};
        Controller controller{};
        EPOCHGUI_CHECK(controller.replace_graph(graph.input()).committed);
        EPOCHGUI_CHECK(controller.set_viewport({50.0, 30.0, 800.0, 600.0}).accepted);
        EPOCHGUI_CHECK(controller.set_view({.pan = {20.0, 40.0}, .zoom = 2.0}).changed);

        const Vec2 screen = controller.world_to_screen(Vec2{15.0, 25.0});
        EPOCHGUI_CHECK(near(screen.x, 100.0));
        EPOCHGUI_CHECK(near(screen.y, 120.0));
        const Vec2 world = controller.screen_to_world(screen);
        EPOCHGUI_CHECK(near(world.x, 15.0));
        EPOCHGUI_CHECK(near(world.y, 25.0));

        const Rect screen_rect = controller.world_to_screen(
            Rect{10.0, 20.0, 30.0, 40.0});
        EPOCHGUI_CHECK((screen_rect == Rect{90.0, 110.0, 60.0, 80.0}));
        EPOCHGUI_CHECK((controller.screen_to_world(screen_rect)
            == Rect{10.0, 20.0, 30.0, 40.0}));

        const Vec2 anchor{300.0, 200.0};
        const Vec2 anchored_world = controller.screen_to_world(anchor);
        EPOCHGUI_CHECK(controller.zoom_at(anchor, 2.0).changed);
        const Vec2 anchored_screen = controller.world_to_screen(anchored_world);
        EPOCHGUI_CHECK(near(anchored_screen.x, anchor.x));
        EPOCHGUI_CHECK(near(anchored_screen.y, anchor.y));

        EPOCHGUI_CHECK(controller.pan_by({25.0, -15.0}).changed);
        EPOCHGUI_CHECK(!controller.set_view({.pan = {}, .zoom = 0.0}).accepted);
        EPOCHGUI_CHECK(!controller.set_viewport({0.0, 0.0, 0.0, 100.0}).accepted);

        const ViewResult fitted = controller.fit_to_content(24.0);
        EPOCHGUI_CHECK(fitted.accepted);
        for (const NodeProjection& node : controller.node_projections())
        {
            EPOCHGUI_CHECK(node.screen_bounds.x >= controller.viewport().x + 23.9);
            EPOCHGUI_CHECK(node.screen_bounds.y >= controller.viewport().y + 23.9);
            EPOCHGUI_CHECK(node.screen_bounds.x + node.screen_bounds.width
                <= controller.viewport().x + controller.viewport().width - 23.9);
            EPOCHGUI_CHECK(node.screen_bounds.y + node.screen_bounds.height
                <= controller.viewport().y + controller.viewport().height - 23.9);
        }

        Controller empty{};
        EPOCHGUI_CHECK(empty.fit_to_content().error == ErrorCode::empty_graph);
        return 0;
    }

    int hit_testing_and_selection()
    {
        GraphFixture graph{};
        Controller controller{};
        EPOCHGUI_CHECK(controller.set_viewport({0.0, 0.0, 640.0, 480.0}).accepted);
        EPOCHGUI_CHECK(controller.replace_graph(graph.input()).committed);

        const PinProjection* output = find_pin(controller, 11u);
        const NodeProjection* first = find_node(controller, 1u);
        EPOCHGUI_CHECK(output != nullptr && first != nullptr);
        const Hit pin_hit = controller.hit_test(output->screen_position);
        EPOCHGUI_CHECK(pin_hit.kind == HitKind::pin);
        EPOCHGUI_CHECK(pin_hit.pin_id == 11u);
        EPOCHGUI_CHECK(pin_hit.node_id == 1u);

        const Hit node_hit = controller.hit_test(center(first->screen_bounds));
        EPOCHGUI_CHECK(node_hit.kind == HitKind::node);
        EPOCHGUI_CHECK(node_hit.node_id == 1u);

        const EdgeProjection& edge = controller.edge_projections()[0];
        const Vec2 edge_midpoint = curve_point(edge.screen_curve, 0.5);
        EPOCHGUI_CHECK(controller.hit_test_edge(edge_midpoint) == 101u);
        EPOCHGUI_CHECK(controller.hit_test(
            edge_midpoint,
            {.pins = false, .nodes = false, .edges = true}).kind == HitKind::edge);

        EPOCHGUI_CHECK(controller.select_node(1u).changed);
        EPOCHGUI_CHECK(controller.select_node(2u, SelectionMode::add).changed);
        EPOCHGUI_CHECK(controller.selected_node_ids().size() == 2u);
        EPOCHGUI_CHECK(controller.selected_node_ids()[0] == 1u);
        EPOCHGUI_CHECK(controller.selected_node_ids()[1] == 2u);
        EPOCHGUI_CHECK(controller.select_edge(101u, SelectionMode::add).changed);
        EPOCHGUI_CHECK(controller.selected_edge_ids().size() == 1u);
        EPOCHGUI_CHECK(controller.select_node(1u, SelectionMode::toggle).changed);
        EPOCHGUI_CHECK(controller.selected_node_ids().size() == 1u);

        const Rect first_two{30.0, 30.0, 380.0, 100.0};
        EPOCHGUI_CHECK(controller.box_select(
            first_two,
            {.mode = SelectionMode::replace, .require_full_containment = true}).changed);
        EPOCHGUI_CHECK(controller.selected_node_ids().size() == 2u);
        EPOCHGUI_CHECK(controller.selected_edge_ids().empty());
        EPOCHGUI_CHECK(controller.box_select(
            first->screen_bounds,
            {.mode = SelectionMode::subtract}).changed);
        EPOCHGUI_CHECK(controller.selected_node_ids().size() == 1u);
        EPOCHGUI_CHECK(controller.selected_node_ids()[0] == 2u);
        EPOCHGUI_CHECK(controller.clear_selection());
        EPOCHGUI_CHECK(!controller.clear_selection());
        EPOCHGUI_CHECK(!controller.select_node(999u).accepted);
        return 0;
    }

    int pointer_intents_without_document_mutation()
    {
        GraphFixture graph{};
        Controller controller{};
        EPOCHGUI_CHECK(controller.set_viewport({0.0, 0.0, 800.0, 600.0}).accepted);
        EPOCHGUI_CHECK(controller.replace_graph(graph.input()).committed);
        EPOCHGUI_CHECK(controller.select_node(1u).accepted);
        EPOCHGUI_CHECK(controller.select_node(2u, SelectionMode::add).accepted);

        const Rect first_before = controller.nodes()[0].bounds;
        const Rect second_before = controller.nodes()[1].bounds;
        const Vec2 first_center = center(find_node(controller, 1u)->screen_bounds);
        PointerResult pointer = controller.pointer_down({.screen_position = first_center});
        EPOCHGUI_CHECK(pointer.intent.kind == IntentKind::move_nodes);
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::begin);
        EPOCHGUI_CHECK(pointer.intent.node_ids.size() == 2u);
        pointer = controller.pointer_move({first_center.x + 60.0, first_center.y + 30.0});
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::update);
        EPOCHGUI_CHECK((pointer.intent.world_delta == Vec2{60.0, 30.0}));
        pointer = controller.pointer_up({first_center.x + 80.0, first_center.y + 40.0});
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::commit);
        EPOCHGUI_CHECK((pointer.intent.world_delta == Vec2{80.0, 40.0}));
        EPOCHGUI_CHECK(controller.nodes()[0].bounds == first_before);
        EPOCHGUI_CHECK(controller.nodes()[1].bounds == second_before);
        EPOCHGUI_CHECK(!controller.pointer_active());

        pointer = controller.pointer_down({
            .screen_position = {10.0, 10.0},
            .button = PointerButton::middle
        });
        EPOCHGUI_CHECK(pointer.intent.kind == IntentKind::pan_view);
        const ViewState before_pan = controller.view();
        pointer = controller.pointer_up({30.0, 45.0});
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::commit);
        EPOCHGUI_CHECK((controller.view().pan
            == Vec2{before_pan.pan.x + 20.0, before_pan.pan.y + 35.0}));

        EPOCHGUI_CHECK(controller.set_view({}).accepted);
        pointer = controller.pointer_down({.screen_position = {20.0, 20.0}});
        EPOCHGUI_CHECK(pointer.intent.kind == IntentKind::box_selection);
        pointer = controller.pointer_up({420.0, 140.0});
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::commit);
        EPOCHGUI_CHECK(controller.selected_node_ids().size() == 2u);

        pointer = controller.pointer_down({.screen_position = {700.0, 500.0}});
        EPOCHGUI_CHECK(pointer.intent.kind == IntentKind::box_selection);
        EPOCHGUI_CHECK(controller.cancel_pointer().intent.phase == IntentPhase::cancel);
        EPOCHGUI_CHECK(!controller.pointer_active());
        EPOCHGUI_CHECK(!controller.pointer_move({0.0, 0.0}).accepted);
        return 0;
    }

    int connection_and_disconnect_intents()
    {
        GraphFixture graph{};
        Controller controller{};
        EPOCHGUI_CHECK(controller.set_viewport({0.0, 0.0, 800.0, 600.0}).accepted);
        EPOCHGUI_CHECK(controller.replace_graph(graph.input()).committed);
        const PinProjection* output = find_pin(controller, 22u);
        const PinProjection* input = find_pin(controller, 31u);
        EPOCHGUI_CHECK(output != nullptr && input != nullptr);

        PointerResult pointer = controller.pointer_down({
            .screen_position = input->screen_position
        });
        EPOCHGUI_CHECK(pointer.intent.kind == IntentKind::connect_pins);
        EPOCHGUI_CHECK(pointer.intent.input_pin_id == 31u);
        pointer = controller.pointer_move(output->screen_position);
        EPOCHGUI_CHECK(pointer.intent.valid_target);
        EPOCHGUI_CHECK(pointer.intent.output_pin_id == 22u);
        EPOCHGUI_CHECK(pointer.intent.input_pin_id == 31u);
        pointer = controller.pointer_up(output->screen_position);
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::commit);
        EPOCHGUI_CHECK(controller.edges().size() == 1u);

        const PinProjection* existing_output = find_pin(controller, 11u);
        const PinProjection* existing_input = find_pin(controller, 21u);
        EPOCHGUI_CHECK(existing_output != nullptr && existing_input != nullptr);
        EPOCHGUI_CHECK(controller.pointer_down({
            .screen_position = existing_output->screen_position}).accepted);
        pointer = controller.pointer_up(existing_input->screen_position);
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::cancel);
        EPOCHGUI_CHECK(!pointer.intent.valid_target);

        const IntentResult disconnect = controller.disconnect_intent(101u);
        EPOCHGUI_CHECK(disconnect.accepted);
        EPOCHGUI_CHECK(disconnect.intent.kind == IntentKind::disconnect_edges);
        EPOCHGUI_CHECK(disconnect.intent.edge_ids.size() == 1u);
        EPOCHGUI_CHECK(controller.edges().size() == 1u);
        EPOCHGUI_CHECK(!controller.disconnect_intent(999u).accepted);

        EPOCHGUI_CHECK(controller.select_edge(101u).accepted);
        const IntentResult selected_disconnect = controller.disconnect_selected_intent();
        EPOCHGUI_CHECK(selected_disconnect.accepted);
        EPOCHGUI_CHECK(selected_disconnect.intent.edge_ids[0] == 101u);

        const Vec2 edge_midpoint = curve_point(
            controller.edge_projections()[0].screen_curve,
            0.5);
        pointer = controller.pointer_down({
            .screen_position = edge_midpoint,
            .disconnect_gesture = true
        });
        EPOCHGUI_CHECK(pointer.intent.kind == IntentKind::disconnect_edges);
        EPOCHGUI_CHECK(pointer.intent.phase == IntentPhase::commit);
        return 0;
    }

    int keyboard_navigation_and_contract()
    {
        GraphFixture graph{};
        Controller controller{};
        EPOCHGUI_CHECK(controller.replace_graph(graph.input()).committed);

        NavigationResult navigation = controller.navigate(NavigationCommand::first);
        EPOCHGUI_CHECK(navigation.focused_node_id == 1u);
        navigation = controller.navigate(NavigationCommand::right);
        EPOCHGUI_CHECK(navigation.focused_node_id == 2u);
        navigation = controller.navigate(NavigationCommand::down, true);
        EPOCHGUI_CHECK(navigation.focused_node_id == 3u);
        EPOCHGUI_CHECK(controller.selected_node_ids().size() == 2u);
        navigation = controller.navigate(NavigationCommand::up);
        EPOCHGUI_CHECK(navigation.focused_node_id == 2u);
        EPOCHGUI_CHECK(controller.selected_node_ids().size() == 1u);
        navigation = controller.navigate(NavigationCommand::last);
        EPOCHGUI_CHECK(navigation.focused_node_id == 3u);
        navigation = controller.navigate(NavigationCommand::previous);
        EPOCHGUI_CHECK(navigation.focused_node_id == 2u);

        EPOCHGUI_CHECK(run_contract() == ContractFailure::none);
        return 0;
    }
}

int main()
{
    static_assert(error_code_name(ErrorCode::duplicate_connection)
        == "duplicate_connection");
    static_assert(contract_failure_name(ContractFailure::none) == "pass");

    if (const int result = deterministic_projection_and_bounds(); result != 0)
        return result;
    if (const int result = viewport_transforms_and_fit(); result != 0)
        return result;
    if (const int result = hit_testing_and_selection(); result != 0)
        return result;
    if (const int result = pointer_intents_without_document_mutation(); result != 0)
        return result;
    if (const int result = connection_and_disconnect_intents(); result != 0)
        return result;
    return keyboard_navigation_and_contract();
}
