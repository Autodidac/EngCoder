// SPDX-License-Identifier: LicenseRef-MIT-NoSell

#include "../../include/gui/node_graph_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace epochengine::gui_lib::node_graph_workspace
{
    namespace
    {
        constexpr double comparison_epsilon = 1.0e-9;

        [[nodiscard]] bool finite(double value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool finite(Vec2 value) noexcept
        {
            return finite(value.x) && finite(value.y);
        }

        [[nodiscard]] bool finite(Rect value) noexcept
        {
            return finite(value.x) && finite(value.y)
                && finite(value.width) && finite(value.height);
        }

        [[nodiscard]] double clamp(double value, double minimum, double maximum) noexcept
        {
            return (std::max)(minimum, (std::min)(value, maximum));
        }

        [[nodiscard]] Rect normalized(Rect value) noexcept
        {
            if (value.width < 0.0)
            {
                value.x += value.width;
                value.width = -value.width;
            }
            if (value.height < 0.0)
            {
                value.y += value.height;
                value.height = -value.height;
            }
            return value;
        }

        [[nodiscard]] bool contains(Rect bounds, Vec2 point) noexcept
        {
            return point.x >= bounds.x && point.y >= bounds.y
                && point.x <= bounds.x + bounds.width
                && point.y <= bounds.y + bounds.height;
        }

        [[nodiscard]] bool contains(Rect outer, Rect inner) noexcept
        {
            return inner.x >= outer.x && inner.y >= outer.y
                && inner.x + inner.width <= outer.x + outer.width
                && inner.y + inner.height <= outer.y + outer.height;
        }

        [[nodiscard]] bool intersects(Rect left, Rect right) noexcept
        {
            return left.x <= right.x + right.width
                && left.x + left.width >= right.x
                && left.y <= right.y + right.height
                && left.y + left.height >= right.y;
        }

        [[nodiscard]] Vec2 center(Rect bounds) noexcept
        {
            return {
                bounds.x + bounds.width * 0.5,
                bounds.y + bounds.height * 0.5
            };
        }

        [[nodiscard]] double squared_distance(Vec2 left, Vec2 right) noexcept
        {
            const double x = left.x - right.x;
            const double y = left.y - right.y;
            return x * x + y * y;
        }

        [[nodiscard]] Vec2 curve_point(const CubicCurve& curve, double t) noexcept
        {
            const double one_minus_t = 1.0 - t;
            const double a = one_minus_t * one_minus_t * one_minus_t;
            const double b = 3.0 * one_minus_t * one_minus_t * t;
            const double c = 3.0 * one_minus_t * t * t;
            const double d = t * t * t;
            return {
                a * curve.start.x + b * curve.control1.x
                    + c * curve.control2.x + d * curve.end.x,
                a * curve.start.y + b * curve.control1.y
                    + c * curve.control2.y + d * curve.end.y
            };
        }

        [[nodiscard]] double point_segment_distance_squared(
            Vec2 point,
            Vec2 first,
            Vec2 second) noexcept
        {
            const Vec2 segment{second.x - first.x, second.y - first.y};
            const double length_squared = segment.x * segment.x + segment.y * segment.y;
            if (length_squared <= comparison_epsilon)
                return squared_distance(point, first);
            const double t = clamp(
                ((point.x - first.x) * segment.x
                    + (point.y - first.y) * segment.y) / length_squared,
                0.0,
                1.0);
            return squared_distance(
                point,
                {first.x + segment.x * t, first.y + segment.y * t});
        }

        [[nodiscard]] bool almost_equal(double left, double right) noexcept
        {
            return std::abs(left - right) <= comparison_epsilon;
        }

        struct ConnectionKey final
        {
            PinId output{};
            PinId input{};

            friend bool operator==(const ConnectionKey&, const ConnectionKey&) noexcept = default;
        };

        struct ConnectionKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const ConnectionKey& key) const noexcept
            {
                const std::size_t left = std::hash<PinId>{}(key.output);
                const std::size_t right = std::hash<PinId>{}(key.input);
                return left ^ (right + static_cast<std::size_t>(0x9e37'79b9u)
                    + (left << 6u) + (left >> 2u));
            }
        };

        struct Candidate final
        {
            std::vector<NodeLayout> nodes{};
            std::vector<PinLayout> pins{};
            std::vector<EdgeLayout> edges{};
            std::unordered_map<NodeId, std::size_t> node_indices{};
            std::unordered_map<PinId, std::size_t> pin_indices{};
            std::unordered_map<EdgeId, std::size_t> edge_indices{};
            Error error{};
        };

        [[nodiscard]] Candidate build_candidate(
            const GraphLayoutInput& input,
            const Limits& limits)
        {
            Candidate candidate{};
            if (!limits.valid())
            {
                candidate.error = {
                    .code = ErrorCode::invalid_limits,
                    .message = "Node graph workspace limits are invalid."
                };
                return candidate;
            }
            if (input.nodes.size() > limits.maximum_nodes)
            {
                candidate.error = {
                    .code = ErrorCode::node_limit_exceeded,
                    .message = "Node layout input exceeds the configured node limit."
                };
                return candidate;
            }
            if (input.pins.size() > limits.maximum_pins)
            {
                candidate.error = {
                    .code = ErrorCode::pin_limit_exceeded,
                    .message = "Node layout input exceeds the configured pin limit."
                };
                return candidate;
            }
            if (input.edges.size() > limits.maximum_edges)
            {
                candidate.error = {
                    .code = ErrorCode::edge_limit_exceeded,
                    .message = "Node layout input exceeds the configured edge limit."
                };
                return candidate;
            }

            candidate.nodes.assign(input.nodes.begin(), input.nodes.end());
            std::unordered_set<NodeId> node_ids{};
            node_ids.reserve(candidate.nodes.size());
            for (const NodeLayout& node : candidate.nodes)
            {
                if (node.id == invalid_node_id)
                {
                    candidate.error = {
                        .code = ErrorCode::invalid_node_id,
                        .message = "Every node projection requires a nonzero stable ID."
                    };
                    return candidate;
                }
                if (!node_ids.emplace(node.id).second)
                {
                    candidate.error = {
                        .code = ErrorCode::duplicate_node_id,
                        .message = "Node projection IDs must be unique.",
                        .related_id = node.id
                    };
                    return candidate;
                }
                const double right = node.bounds.x + node.bounds.width;
                const double bottom = node.bounds.y + node.bounds.height;
                if (!finite(node.bounds)
                    || node.bounds.width <= 0.0 || node.bounds.height <= 0.0
                    || node.bounds.width > limits.maximum_node_extent
                    || node.bounds.height > limits.maximum_node_extent
                    || std::abs(node.bounds.x) > limits.maximum_world_coordinate
                    || std::abs(node.bounds.y) > limits.maximum_world_coordinate
                    || !finite(right) || !finite(bottom)
                    || std::abs(right) > limits.maximum_world_coordinate
                    || std::abs(bottom) > limits.maximum_world_coordinate)
                {
                    candidate.error = {
                        .code = ErrorCode::invalid_geometry,
                        .message = "Node bounds are non-finite, empty, or outside configured world bounds.",
                        .related_id = node.id
                    };
                    return candidate;
                }
            }
            std::ranges::sort(
                candidate.nodes,
                [](const NodeLayout& left, const NodeLayout& right)
                {
                    return std::tie(left.layout_order, left.id)
                        < std::tie(right.layout_order, right.id);
                });
            candidate.node_indices.reserve(candidate.nodes.size());
            for (std::size_t index = 0; index < candidate.nodes.size(); ++index)
                candidate.node_indices.emplace(candidate.nodes[index].id, index);

            candidate.pins.assign(input.pins.begin(), input.pins.end());
            std::unordered_set<PinId> pin_ids{};
            pin_ids.reserve(candidate.pins.size());
            for (const PinLayout& pin : candidate.pins)
            {
                if (pin.id == invalid_pin_id)
                {
                    candidate.error = {
                        .code = ErrorCode::invalid_pin_id,
                        .message = "Every pin projection requires a nonzero stable ID."
                    };
                    return candidate;
                }
                if (!pin_ids.emplace(pin.id).second)
                {
                    candidate.error = {
                        .code = ErrorCode::duplicate_pin_id,
                        .message = "Pin projection IDs must be unique.",
                        .related_id = pin.id
                    };
                    return candidate;
                }
                if (!candidate.node_indices.contains(pin.node_id))
                {
                    candidate.error = {
                        .code = ErrorCode::missing_node,
                        .message = "A pin references a missing node.",
                        .related_id = pin.id
                    };
                    return candidate;
                }
                if (pin.direction != PinDirection::input
                    && pin.direction != PinDirection::output)
                {
                    candidate.error = {
                        .code = ErrorCode::invalid_pin_direction,
                        .message = "A pin has an invalid direction.",
                        .related_id = pin.id
                    };
                    return candidate;
                }
                if (!finite(pin.position) || !finite(pin.hit_radius)
                    || pin.hit_radius <= 0.0
                    || pin.hit_radius > limits.maximum_pin_hit_radius
                    || std::abs(pin.position.x) > limits.maximum_world_coordinate
                    || std::abs(pin.position.y) > limits.maximum_world_coordinate)
                {
                    candidate.error = {
                        .code = ErrorCode::invalid_geometry,
                        .message = "Pin geometry is invalid or outside configured world bounds.",
                        .related_id = pin.id
                    };
                    return candidate;
                }
            }
            std::ranges::sort(
                candidate.pins,
                [&](const PinLayout& left, const PinLayout& right)
                {
                    const std::size_t left_node = candidate.node_indices.at(left.node_id);
                    const std::size_t right_node = candidate.node_indices.at(right.node_id);
                    return std::tuple{
                        left_node,
                        static_cast<std::uint8_t>(left.direction),
                        left.layout_order,
                        left.id}
                        < std::tuple{
                            right_node,
                            static_cast<std::uint8_t>(right.direction),
                            right.layout_order,
                            right.id};
                });
            candidate.pin_indices.reserve(candidate.pins.size());
            for (std::size_t index = 0; index < candidate.pins.size(); ++index)
                candidate.pin_indices.emplace(candidate.pins[index].id, index);

            candidate.edges.assign(input.edges.begin(), input.edges.end());
            std::unordered_set<EdgeId> edge_ids{};
            std::unordered_set<ConnectionKey, ConnectionKeyHash> connections{};
            edge_ids.reserve(candidate.edges.size());
            connections.reserve(candidate.edges.size());
            for (const EdgeLayout& edge : candidate.edges)
            {
                if (edge.id == invalid_edge_id)
                {
                    candidate.error = {
                        .code = ErrorCode::invalid_edge_id,
                        .message = "Every edge projection requires a nonzero stable ID."
                    };
                    return candidate;
                }
                if (!edge_ids.emplace(edge.id).second)
                {
                    candidate.error = {
                        .code = ErrorCode::duplicate_edge_id,
                        .message = "Edge projection IDs must be unique.",
                        .related_id = edge.id
                    };
                    return candidate;
                }
                const auto output = candidate.pin_indices.find(edge.output_pin_id);
                const auto input_pin = candidate.pin_indices.find(edge.input_pin_id);
                if (output == candidate.pin_indices.end()
                    || input_pin == candidate.pin_indices.end())
                {
                    candidate.error = {
                        .code = ErrorCode::missing_pin,
                        .message = "An edge references a missing pin.",
                        .related_id = edge.id
                    };
                    return candidate;
                }
                if (candidate.pins[output->second].direction != PinDirection::output
                    || candidate.pins[input_pin->second].direction != PinDirection::input)
                {
                    candidate.error = {
                        .code = ErrorCode::incompatible_pins,
                        .message = "Edges must be directed from an output pin to an input pin.",
                        .related_id = edge.id
                    };
                    return candidate;
                }
                if (!connections.emplace(ConnectionKey{
                        edge.output_pin_id,
                        edge.input_pin_id}).second)
                {
                    candidate.error = {
                        .code = ErrorCode::duplicate_connection,
                        .message = "A graph layout cannot contain duplicate pin connections.",
                        .related_id = edge.id
                    };
                    return candidate;
                }
            }
            std::ranges::sort(
                candidate.edges,
                [](const EdgeLayout& left, const EdgeLayout& right)
                {
                    return std::tie(left.layout_order, left.id)
                        < std::tie(right.layout_order, right.id);
                });
            candidate.edge_indices.reserve(candidate.edges.size());
            for (std::size_t index = 0; index < candidate.edges.size(); ++index)
                candidate.edge_indices.emplace(candidate.edges[index].id, index);
            return candidate;
        }
    }

    bool Limits::valid() const noexcept
    {
        return maximum_nodes > 0u && maximum_pins > 0u && maximum_edges > 0u
            && maximum_selected_items > 0u
            && edge_hit_test_segments >= 4u && edge_hit_test_segments <= 256u
            && finite(minimum_zoom) && finite(maximum_zoom)
            && minimum_zoom > 0.0 && maximum_zoom >= minimum_zoom
            && finite(maximum_world_coordinate) && maximum_world_coordinate > 0.0
            && finite(maximum_node_extent) && maximum_node_extent > 0.0
            && maximum_node_extent <= maximum_world_coordinate * 2.0
            && finite(maximum_viewport_extent) && maximum_viewport_extent > 0.0
            && finite(maximum_pan) && maximum_pan > 0.0
            && finite(maximum_pin_hit_radius) && maximum_pin_hit_radius > 0.0
            && finite(minimum_screen_pin_hit_radius)
            && minimum_screen_pin_hit_radius > 0.0
            && finite(maximum_screen_pin_hit_radius)
            && maximum_screen_pin_hit_radius >= minimum_screen_pin_hit_radius
            && finite(edge_hit_tolerance) && edge_hit_tolerance > 0.0
            && finite(minimum_edge_control_distance)
            && minimum_edge_control_distance > 0.0
            && finite(maximum_edge_control_distance)
            && maximum_edge_control_distance >= minimum_edge_control_distance
            && finite(maximum_pointer_world_delta)
            && maximum_pointer_world_delta > 0.0;
    }

    struct Controller::Implementation final
    {
        enum class DragMode : std::uint8_t
        {
            none,
            pan,
            move_nodes,
            box_selection,
            connect_pins
        };

        struct DragState final
        {
            DragMode mode{DragMode::none};
            Vec2 start_screen{};
            Vec2 last_screen{};
            Vec2 start_world{};
            SelectionMode selection_mode{SelectionMode::replace};
            PinId start_pin_id{invalid_pin_id};
            std::vector<NodeId> node_ids{};
        };

        explicit Implementation(Limits requested_limits)
            : limits{requested_limits}
        {
            if (!limits.valid())
            {
                current_error = {
                    .code = ErrorCode::invalid_limits,
                    .message = "Node graph workspace limits are invalid."
                };
            }
        }

        Limits limits{};
        std::vector<NodeLayout> nodes{};
        std::vector<PinLayout> pins{};
        std::vector<EdgeLayout> edges{};
        std::unordered_map<NodeId, std::size_t> node_indices{};
        std::unordered_map<PinId, std::size_t> pin_indices{};
        std::unordered_map<EdgeId, std::size_t> edge_indices{};
        std::vector<NodeProjection> node_views{};
        std::vector<PinProjection> pin_views{};
        std::vector<EdgeProjection> edge_views{};
        std::unordered_set<NodeId> selected_nodes{};
        std::unordered_set<EdgeId> selected_edges{};
        std::vector<NodeId> ordered_selected_nodes{};
        std::vector<EdgeId> ordered_selected_edges{};
        Rect viewport{0.0, 0.0, 1.0, 1.0};
        ViewState view{};
        std::optional<Rect> content{};
        Error current_error{};
        DragState drag{};
        NodeId focus{invalid_node_id};
        std::uint64_t source_revision{};
        std::uint64_t view_revision{};
        bool stale_graph{};

        void bump_revision() noexcept
        {
            if (view_revision != (std::numeric_limits<std::uint64_t>::max)())
                ++view_revision;
        }

        [[nodiscard]] Vec2 to_screen(Vec2 world) const noexcept
        {
            return {
                viewport.x + view.pan.x + world.x * view.zoom,
                viewport.y + view.pan.y + world.y * view.zoom
            };
        }

        [[nodiscard]] Vec2 to_world(Vec2 screen) const noexcept
        {
            return {
                (screen.x - viewport.x - view.pan.x) / view.zoom,
                (screen.y - viewport.y - view.pan.y) / view.zoom
            };
        }

        [[nodiscard]] Rect to_screen(Rect world) const noexcept
        {
            const Vec2 top_left = to_screen(Vec2{world.x, world.y});
            return {
                top_left.x,
                top_left.y,
                world.width * view.zoom,
                world.height * view.zoom
            };
        }

        [[nodiscard]] Rect to_world(Rect screen) const noexcept
        {
            screen = normalized(screen);
            const Vec2 top_left = to_world(Vec2{screen.x, screen.y});
            return {
                top_left.x,
                top_left.y,
                screen.width / view.zoom,
                screen.height / view.zoom
            };
        }

        [[nodiscard]] CubicCurve make_world_curve(
            Vec2 output,
            Vec2 input) const noexcept
        {
            const double horizontal = std::abs(input.x - output.x);
            const double control = clamp(
                horizontal * 0.5,
                limits.minimum_edge_control_distance,
                limits.maximum_edge_control_distance);
            return {
                .start = output,
                .control1 = {output.x + control, output.y},
                .control2 = {input.x - control, input.y},
                .end = input
            };
        }

        [[nodiscard]] CubicCurve to_screen(const CubicCurve& world) const noexcept
        {
            return {
                .start = to_screen(world.start),
                .control1 = to_screen(world.control1),
                .control2 = to_screen(world.control2),
                .end = to_screen(world.end)
            };
        }

        void rebuild_maps()
        {
            node_indices.clear();
            pin_indices.clear();
            edge_indices.clear();
            node_indices.reserve(nodes.size());
            pin_indices.reserve(pins.size());
            edge_indices.reserve(edges.size());
            for (std::size_t index = 0; index < nodes.size(); ++index)
                node_indices.emplace(nodes[index].id, index);
            for (std::size_t index = 0; index < pins.size(); ++index)
                pin_indices.emplace(pins[index].id, index);
            for (std::size_t index = 0; index < edges.size(); ++index)
                edge_indices.emplace(edges[index].id, index);
        }

        void rebuild_selection_order()
        {
            ordered_selected_nodes.clear();
            ordered_selected_edges.clear();
            ordered_selected_nodes.reserve(selected_nodes.size());
            ordered_selected_edges.reserve(selected_edges.size());
            for (const NodeLayout& node : nodes)
            {
                if (selected_nodes.contains(node.id))
                    ordered_selected_nodes.push_back(node.id);
            }
            for (const EdgeLayout& edge : edges)
            {
                if (selected_edges.contains(edge.id))
                    ordered_selected_edges.push_back(edge.id);
            }
        }

        void rebuild_projections()
        {
            node_views.clear();
            pin_views.clear();
            edge_views.clear();
            node_views.reserve(nodes.size());
            pin_views.reserve(pins.size());
            edge_views.reserve(edges.size());

            content.reset();
            for (const NodeLayout& node : nodes)
            {
                node_views.push_back({
                    .id = node.id,
                    .world_bounds = node.bounds,
                    .screen_bounds = to_screen(node.bounds),
                    .layout_order = node.layout_order,
                    .selectable = node.selectable,
                    .selected = selected_nodes.contains(node.id),
                    .focused = focus == node.id
                });
                if (!content.has_value())
                {
                    content = node.bounds;
                }
                else
                {
                    const double left = (std::min)(content->x, node.bounds.x);
                    const double top = (std::min)(content->y, node.bounds.y);
                    const double right = (std::max)(
                        content->x + content->width,
                        node.bounds.x + node.bounds.width);
                    const double bottom = (std::max)(
                        content->y + content->height,
                        node.bounds.y + node.bounds.height);
                    content = Rect{left, top, right - left, bottom - top};
                }
            }

            for (const PinLayout& pin : pins)
            {
                pin_views.push_back({
                    .id = pin.id,
                    .node_id = pin.node_id,
                    .direction = pin.direction,
                    .world_position = pin.position,
                    .screen_position = to_screen(pin.position),
                    .screen_hit_radius = clamp(
                        pin.hit_radius * view.zoom,
                        limits.minimum_screen_pin_hit_radius,
                        limits.maximum_screen_pin_hit_radius),
                    .layout_order = pin.layout_order,
                    .connectable = pin.connectable
                });
            }

            for (const EdgeLayout& edge : edges)
            {
                const PinLayout& output = pins[pin_indices.at(edge.output_pin_id)];
                const PinLayout& input = pins[pin_indices.at(edge.input_pin_id)];
                const CubicCurve world_curve = make_world_curve(
                    output.position,
                    input.position);
                edge_views.push_back({
                    .id = edge.id,
                    .output_pin_id = edge.output_pin_id,
                    .input_pin_id = edge.input_pin_id,
                    .output_node_id = output.node_id,
                    .input_node_id = input.node_id,
                    .world_curve = world_curve,
                    .screen_curve = to_screen(world_curve),
                    .layout_order = edge.layout_order,
                    .selectable = edge.selectable,
                    .selected = selected_edges.contains(edge.id)
                });
            }
        }

        void rebuild_view()
        {
            rebuild_selection_order();
            rebuild_projections();
            bump_revision();
        }

        [[nodiscard]] SelectionResult commit_selection(
            std::unordered_set<NodeId> nodes_to_select,
            std::unordered_set<EdgeId> edges_to_select,
            NodeId requested_focus)
        {
            if (nodes_to_select.size() + edges_to_select.size()
                > limits.maximum_selected_items)
            {
                return {
                    .error = ErrorCode::selection_limit_exceeded,
                    .selected_node_count = selected_nodes.size(),
                    .selected_edge_count = selected_edges.size(),
                    .accepted = false
                };
            }

            if (requested_focus != invalid_node_id
                && !nodes_to_select.contains(requested_focus))
            {
                requested_focus = invalid_node_id;
            }
            if (requested_focus == invalid_node_id && !nodes_to_select.empty())
            {
                for (const NodeLayout& node : nodes)
                {
                    if (nodes_to_select.contains(node.id))
                    {
                        requested_focus = node.id;
                        break;
                    }
                }
            }

            const bool selection_changed = selected_nodes != nodes_to_select
                || selected_edges != edges_to_select;
            const bool focus_changed = focus != requested_focus;
            if (selection_changed)
            {
                selected_nodes = std::move(nodes_to_select);
                selected_edges = std::move(edges_to_select);
            }
            focus = requested_focus;
            if (selection_changed || focus_changed)
                rebuild_view();
            return {
                .selected_node_count = selected_nodes.size(),
                .selected_edge_count = selected_edges.size(),
                .changed = selection_changed || focus_changed,
                .focus_changed = focus_changed
            };
        }

        [[nodiscard]] bool compatible_connection(
            PinId first_id,
            PinId second_id,
            PinId& output_id,
            PinId& input_id) const noexcept
        {
            const auto first = pin_indices.find(first_id);
            const auto second = pin_indices.find(second_id);
            if (first == pin_indices.end() || second == pin_indices.end()
                || first_id == second_id)
            {
                return false;
            }
            const PinLayout& first_pin = pins[first->second];
            const PinLayout& second_pin = pins[second->second];
            if (!first_pin.connectable || !second_pin.connectable
                || first_pin.direction == second_pin.direction)
            {
                return false;
            }
            if (first_pin.direction == PinDirection::output)
            {
                output_id = first_id;
                input_id = second_id;
            }
            else
            {
                output_id = second_id;
                input_id = first_id;
            }
            return true;
        }

        [[nodiscard]] bool connection_exists(PinId output, PinId input) const noexcept
        {
            return std::ranges::any_of(
                edges,
                [&](const EdgeLayout& edge)
                {
                    return edge.output_pin_id == output
                        && edge.input_pin_id == input;
                });
        }
    };

    Controller::Controller(Limits limits)
        : implementation_{std::make_unique<Implementation>(limits)}
    {
        implementation_->rebuild_projections();
    }

    Controller::~Controller() = default;
    Controller::Controller(Controller&&) noexcept = default;
    Controller& Controller::operator=(Controller&&) noexcept = default;

    const Limits& Controller::limits() const noexcept
    {
        return implementation_->limits;
    }

    ReplaceResult Controller::replace_graph(
        const GraphLayoutInput& input,
        ReplaceOptions options)
    {
        Implementation& implementation = *implementation_;
        Candidate candidate = build_candidate(input, implementation.limits);
        if (candidate.error.present())
        {
            implementation.current_error = std::move(candidate.error);
            implementation.stale_graph = !implementation.nodes.empty();
            return {
                .error = implementation.current_error.code,
                .node_count = implementation.nodes.size(),
                .pin_count = implementation.pins.size(),
                .edge_count = implementation.edges.size()
            };
        }

        const auto previous_nodes = implementation.selected_nodes;
        const auto previous_edges = implementation.selected_edges;
        const NodeId previous_focus = implementation.focus;
        const std::size_t previous_selection_count = previous_nodes.size()
            + previous_edges.size();

        implementation.nodes = std::move(candidate.nodes);
        implementation.pins = std::move(candidate.pins);
        implementation.edges = std::move(candidate.edges);
        implementation.rebuild_maps();
        implementation.source_revision = input.source_revision;
        implementation.current_error = {};
        implementation.stale_graph = false;
        implementation.drag = {};

        if (!options.preserve_selection)
        {
            implementation.selected_nodes.clear();
            implementation.selected_edges.clear();
        }
        else
        {
            std::erase_if(
                implementation.selected_nodes,
                [&](NodeId id)
                {
                    const auto found = implementation.node_indices.find(id);
                    return found == implementation.node_indices.end()
                        || !implementation.nodes[found->second].selectable;
                });
            std::erase_if(
                implementation.selected_edges,
                [&](EdgeId id)
                {
                    const auto found = implementation.edge_indices.find(id);
                    return found == implementation.edge_indices.end()
                        || !implementation.edges[found->second].selectable;
                });
        }

        if (!options.preserve_focus
            || !implementation.selected_nodes.contains(implementation.focus))
        {
            implementation.focus = invalid_node_id;
        }
        if (implementation.focus == invalid_node_id
            && !implementation.selected_nodes.empty())
        {
            for (const NodeLayout& node : implementation.nodes)
            {
                if (implementation.selected_nodes.contains(node.id))
                {
                    implementation.focus = node.id;
                    break;
                }
            }
        }

        implementation.rebuild_view();
        const std::size_t current_selection_count =
            implementation.selected_nodes.size() + implementation.selected_edges.size();
        return {
            .node_count = implementation.nodes.size(),
            .pin_count = implementation.pins.size(),
            .edge_count = implementation.edges.size(),
            .stale_selection_count = previous_selection_count > current_selection_count
                ? previous_selection_count - current_selection_count
                : 0u,
            .committed = true,
            .selection_changed = previous_nodes != implementation.selected_nodes
                || previous_edges != implementation.selected_edges,
            .focus_changed = previous_focus != implementation.focus
        };
    }

    MutationResult Controller::set_viewport(Rect viewport)
    {
        Implementation& implementation = *implementation_;
        if (!finite(viewport) || viewport.width <= 0.0 || viewport.height <= 0.0
            || viewport.width > implementation.limits.maximum_viewport_extent
            || viewport.height > implementation.limits.maximum_viewport_extent)
        {
            return {
                .error = ErrorCode::invalid_viewport,
                .accepted = false
            };
        }
        if (implementation.viewport == viewport)
            return {};
        implementation.viewport = viewport;
        implementation.rebuild_view();
        return {.changed = true};
    }

    ViewResult Controller::set_view(ViewState view)
    {
        Implementation& implementation = *implementation_;
        if (!finite(view.pan) || !finite(view.zoom) || view.zoom <= 0.0)
        {
            return {
                .error = ErrorCode::invalid_view,
                .view = implementation.view,
                .accepted = false
            };
        }
        view.zoom = clamp(
            view.zoom,
            implementation.limits.minimum_zoom,
            implementation.limits.maximum_zoom);
        view.pan.x = clamp(
            view.pan.x,
            -implementation.limits.maximum_pan,
            implementation.limits.maximum_pan);
        view.pan.y = clamp(
            view.pan.y,
            -implementation.limits.maximum_pan,
            implementation.limits.maximum_pan);
        if (implementation.view == view)
            return {.view = implementation.view};
        implementation.view = view;
        implementation.rebuild_view();
        return {
            .view = implementation.view,
            .changed = true
        };
    }

    ViewResult Controller::pan_by(Vec2 screen_delta)
    {
        if (!finite(screen_delta))
        {
            return {
                .error = ErrorCode::invalid_view,
                .view = implementation_->view,
                .accepted = false
            };
        }
        ViewState requested = implementation_->view;
        requested.pan.x += screen_delta.x;
        requested.pan.y += screen_delta.y;
        return set_view(requested);
    }

    ViewResult Controller::zoom_at(Vec2 screen_anchor, double wheel_steps)
    {
        Implementation& implementation = *implementation_;
        if (!finite(screen_anchor) || !finite(wheel_steps))
        {
            return {
                .error = ErrorCode::invalid_view,
                .view = implementation.view,
                .accepted = false
            };
        }
        const Vec2 anchored_world = implementation.to_world(screen_anchor);
        const double bounded_steps = clamp(wheel_steps, -64.0, 64.0);
        const double requested_zoom = clamp(
            implementation.view.zoom * std::pow(1.2, bounded_steps),
            implementation.limits.minimum_zoom,
            implementation.limits.maximum_zoom);
        ViewState requested = implementation.view;
        requested.zoom = requested_zoom;
        requested.pan = {
            screen_anchor.x - implementation.viewport.x
                - anchored_world.x * requested_zoom,
            screen_anchor.y - implementation.viewport.y
                - anchored_world.y * requested_zoom
        };
        return set_view(requested);
    }

    ViewResult Controller::fit_to_content(double screen_padding)
    {
        Implementation& implementation = *implementation_;
        if (!implementation.content.has_value())
        {
            return {
                .error = ErrorCode::empty_graph,
                .view = implementation.view,
                .accepted = false
            };
        }
        if (!finite(screen_padding) || screen_padding < 0.0
            || screen_padding * 2.0 >= implementation.viewport.width
            || screen_padding * 2.0 >= implementation.viewport.height)
        {
            return {
                .error = ErrorCode::invalid_view,
                .view = implementation.view,
                .accepted = false
            };
        }
        const Rect bounds = *implementation.content;
        const double available_width = implementation.viewport.width - screen_padding * 2.0;
        const double available_height = implementation.viewport.height - screen_padding * 2.0;
        const double fitted_zoom = clamp(
            (std::min)(available_width / bounds.width, available_height / bounds.height),
            implementation.limits.minimum_zoom,
            implementation.limits.maximum_zoom);
        const Vec2 bounds_center = center(bounds);
        return set_view({
            .pan = {
                implementation.viewport.width * 0.5 - bounds_center.x * fitted_zoom,
                implementation.viewport.height * 0.5 - bounds_center.y * fitted_zoom
            },
            .zoom = fitted_zoom
        });
    }

    Vec2 Controller::world_to_screen(Vec2 world) const noexcept
    {
        return implementation_->to_screen(world);
    }

    Vec2 Controller::screen_to_world(Vec2 screen) const noexcept
    {
        return implementation_->to_world(screen);
    }

    Rect Controller::world_to_screen(Rect world) const noexcept
    {
        return implementation_->to_screen(world);
    }

    Rect Controller::screen_to_world(Rect screen) const noexcept
    {
        return implementation_->to_world(screen);
    }

    std::optional<NodeId> Controller::hit_test_node(Vec2 screen_point) const noexcept
    {
        if (!finite(screen_point) || !contains(implementation_->viewport, screen_point))
            return std::nullopt;
        for (auto item = implementation_->node_views.rbegin();
            item != implementation_->node_views.rend(); ++item)
        {
            if (contains(item->screen_bounds, screen_point))
                return item->id;
        }
        return std::nullopt;
    }

    std::optional<PinId> Controller::hit_test_pin(Vec2 screen_point) const noexcept
    {
        if (!finite(screen_point) || !contains(implementation_->viewport, screen_point))
            return std::nullopt;
        std::optional<PinId> result{};
        double best_distance = (std::numeric_limits<double>::max)();
        for (auto item = implementation_->pin_views.rbegin();
            item != implementation_->pin_views.rend(); ++item)
        {
            const double distance = squared_distance(item->screen_position, screen_point);
            const double radius_squared = item->screen_hit_radius * item->screen_hit_radius;
            if (distance <= radius_squared && distance < best_distance)
            {
                result = item->id;
                best_distance = distance;
            }
        }
        return result;
    }

    std::optional<EdgeId> Controller::hit_test_edge(
        Vec2 screen_point,
        double tolerance) const noexcept
    {
        const Implementation& implementation = *implementation_;
        if (!finite(screen_point) || !finite(tolerance)
            || !contains(implementation.viewport, screen_point))
        {
            return std::nullopt;
        }
        if (tolerance <= 0.0)
            tolerance = implementation.limits.edge_hit_tolerance;
        if (tolerance > implementation.limits.maximum_screen_pin_hit_radius * 4.0)
            return std::nullopt;

        std::optional<EdgeId> result{};
        double best_distance = tolerance * tolerance;
        for (auto item = implementation.edge_views.rbegin();
            item != implementation.edge_views.rend(); ++item)
        {
            Vec2 previous = item->screen_curve.start;
            double edge_distance = (std::numeric_limits<double>::max)();
            for (std::size_t segment = 1u;
                segment <= implementation.limits.edge_hit_test_segments;
                ++segment)
            {
                const double t = static_cast<double>(segment)
                    / static_cast<double>(implementation.limits.edge_hit_test_segments);
                const Vec2 current = curve_point(item->screen_curve, t);
                edge_distance = (std::min)(
                    edge_distance,
                    point_segment_distance_squared(screen_point, previous, current));
                previous = current;
            }
            if (edge_distance <= best_distance)
            {
                result = item->id;
                best_distance = edge_distance;
            }
        }
        return result;
    }

    Hit Controller::hit_test(Vec2 screen_point, HitTestOptions options) const noexcept
    {
        if (options.pins)
        {
            if (const auto pin = hit_test_pin(screen_point); pin.has_value())
            {
                const PinLayout& layout = implementation_->pins[
                    implementation_->pin_indices.at(*pin)];
                return {
                    .kind = HitKind::pin,
                    .node_id = layout.node_id,
                    .pin_id = *pin
                };
            }
        }
        if (options.nodes)
        {
            if (const auto node = hit_test_node(screen_point); node.has_value())
            {
                return {
                    .kind = HitKind::node,
                    .node_id = *node
                };
            }
        }
        if (options.edges)
        {
            if (const auto edge = hit_test_edge(
                    screen_point,
                    options.edge_tolerance); edge.has_value())
            {
                return {
                    .kind = HitKind::edge,
                    .edge_id = *edge
                };
            }
        }
        return {};
    }

    SelectionResult Controller::select_node(NodeId id, SelectionMode mode)
    {
        Implementation& implementation = *implementation_;
        const auto found = implementation.node_indices.find(id);
        if (found == implementation.node_indices.end())
        {
            return {
                .error = ErrorCode::unknown_node,
                .selected_node_count = implementation.selected_nodes.size(),
                .selected_edge_count = implementation.selected_edges.size(),
                .accepted = false
            };
        }
        if (!implementation.nodes[found->second].selectable)
        {
            return {
                .error = ErrorCode::not_selectable,
                .selected_node_count = implementation.selected_nodes.size(),
                .selected_edge_count = implementation.selected_edges.size(),
                .accepted = false
            };
        }

        auto selected_nodes = implementation.selected_nodes;
        auto selected_edges = implementation.selected_edges;
        NodeId requested_focus = implementation.focus;
        switch (mode)
        {
        case SelectionMode::replace:
            selected_nodes.clear();
            selected_edges.clear();
            selected_nodes.emplace(id);
            requested_focus = id;
            break;
        case SelectionMode::add:
            selected_nodes.emplace(id);
            requested_focus = id;
            break;
        case SelectionMode::toggle:
            if (selected_nodes.erase(id) == 0u)
            {
                selected_nodes.emplace(id);
                requested_focus = id;
            }
            else if (requested_focus == id)
            {
                requested_focus = invalid_node_id;
            }
            break;
        case SelectionMode::subtract:
            selected_nodes.erase(id);
            if (requested_focus == id)
                requested_focus = invalid_node_id;
            break;
        }
        return implementation.commit_selection(
            std::move(selected_nodes),
            std::move(selected_edges),
            requested_focus);
    }

    SelectionResult Controller::select_edge(EdgeId id, SelectionMode mode)
    {
        Implementation& implementation = *implementation_;
        const auto found = implementation.edge_indices.find(id);
        if (found == implementation.edge_indices.end())
        {
            return {
                .error = ErrorCode::unknown_edge,
                .selected_node_count = implementation.selected_nodes.size(),
                .selected_edge_count = implementation.selected_edges.size(),
                .accepted = false
            };
        }
        if (!implementation.edges[found->second].selectable)
        {
            return {
                .error = ErrorCode::not_selectable,
                .selected_node_count = implementation.selected_nodes.size(),
                .selected_edge_count = implementation.selected_edges.size(),
                .accepted = false
            };
        }

        auto selected_nodes = implementation.selected_nodes;
        auto selected_edges = implementation.selected_edges;
        NodeId requested_focus = implementation.focus;
        switch (mode)
        {
        case SelectionMode::replace:
            selected_nodes.clear();
            selected_edges.clear();
            selected_edges.emplace(id);
            requested_focus = invalid_node_id;
            break;
        case SelectionMode::add:
            selected_edges.emplace(id);
            break;
        case SelectionMode::toggle:
            if (selected_edges.erase(id) == 0u)
                selected_edges.emplace(id);
            break;
        case SelectionMode::subtract:
            selected_edges.erase(id);
            break;
        }
        return implementation.commit_selection(
            std::move(selected_nodes),
            std::move(selected_edges),
            requested_focus);
    }

    SelectionResult Controller::select_at(Vec2 screen_point, SelectionMode mode)
    {
        const Hit hit = hit_test(screen_point);
        if (hit.kind == HitKind::node)
            return select_node(hit.node_id, mode);
        if (hit.kind == HitKind::edge)
            return select_edge(hit.edge_id, mode);
        if (mode == SelectionMode::replace)
        {
            const bool changed = clear_selection();
            return {
                .selected_node_count = implementation_->selected_nodes.size(),
                .selected_edge_count = implementation_->selected_edges.size(),
                .changed = changed,
                .focus_changed = changed
            };
        }
        return {
            .selected_node_count = implementation_->selected_nodes.size(),
            .selected_edge_count = implementation_->selected_edges.size()
        };
    }

    SelectionResult Controller::box_select(
        Rect screen_box,
        BoxSelectionOptions options)
    {
        Implementation& implementation = *implementation_;
        if (!finite(screen_box))
        {
            return {
                .error = ErrorCode::invalid_geometry,
                .selected_node_count = implementation.selected_nodes.size(),
                .selected_edge_count = implementation.selected_edges.size(),
                .accepted = false
            };
        }
        screen_box = normalized(screen_box);
        std::vector<NodeId> candidates{};
        candidates.reserve(implementation.node_views.size());
        for (const NodeProjection& node : implementation.node_views)
        {
            if (!node.selectable)
                continue;
            const bool included = options.require_full_containment
                ? contains(screen_box, node.screen_bounds)
                : intersects(screen_box, node.screen_bounds);
            if (included)
                candidates.push_back(node.id);
        }

        auto selected_nodes = implementation.selected_nodes;
        auto selected_edges = implementation.selected_edges;
        NodeId requested_focus = implementation.focus;
        if (options.mode == SelectionMode::replace)
        {
            selected_nodes.clear();
            selected_edges.clear();
            requested_focus = invalid_node_id;
        }
        for (NodeId id : candidates)
        {
            switch (options.mode)
            {
            case SelectionMode::replace:
            case SelectionMode::add:
                selected_nodes.emplace(id);
                break;
            case SelectionMode::toggle:
                if (selected_nodes.erase(id) == 0u)
                    selected_nodes.emplace(id);
                break;
            case SelectionMode::subtract:
                selected_nodes.erase(id);
                break;
            }
        }
        if (requested_focus == invalid_node_id || !selected_nodes.contains(requested_focus))
        {
            requested_focus = invalid_node_id;
            for (const NodeLayout& node : implementation.nodes)
            {
                if (selected_nodes.contains(node.id))
                {
                    requested_focus = node.id;
                    break;
                }
            }
        }
        return implementation.commit_selection(
            std::move(selected_nodes),
            std::move(selected_edges),
            requested_focus);
    }

    bool Controller::clear_selection()
    {
        Implementation& implementation = *implementation_;
        if (implementation.selected_nodes.empty()
            && implementation.selected_edges.empty()
            && implementation.focus == invalid_node_id)
        {
            return false;
        }
        implementation.selected_nodes.clear();
        implementation.selected_edges.clear();
        implementation.focus = invalid_node_id;
        implementation.rebuild_view();
        return true;
    }

    NavigationResult Controller::navigate(
        NavigationCommand command,
        bool extend_selection)
    {
        Implementation& implementation = *implementation_;
        std::vector<std::size_t> candidates{};
        candidates.reserve(implementation.nodes.size());
        for (std::size_t index = 0; index < implementation.nodes.size(); ++index)
        {
            if (implementation.nodes[index].selectable)
                candidates.push_back(index);
        }
        if (candidates.empty())
            return {.focused_node_id = implementation.focus};

        std::size_t current_position = candidates.size();
        for (std::size_t position = 0; position < candidates.size(); ++position)
        {
            if (implementation.nodes[candidates[position]].id == implementation.focus)
            {
                current_position = position;
                break;
            }
        }

        std::size_t target_position = current_position;
        switch (command)
        {
        case NavigationCommand::first:
            target_position = 0u;
            break;
        case NavigationCommand::previous:
            target_position = current_position == candidates.size() || current_position == 0u
                ? 0u
                : current_position - 1u;
            break;
        case NavigationCommand::next:
            target_position = current_position == candidates.size()
                ? 0u
                : (std::min)(current_position + 1u, candidates.size() - 1u);
            break;
        case NavigationCommand::last:
            target_position = candidates.size() - 1u;
            break;
        case NavigationCommand::left:
        case NavigationCommand::right:
        case NavigationCommand::up:
        case NavigationCommand::down:
            if (current_position == candidates.size())
            {
                target_position = 0u;
                break;
            }
            {
                const Vec2 origin = center(
                    implementation.nodes[candidates[current_position]].bounds);
                double best_score = (std::numeric_limits<double>::max)();
                double best_distance = (std::numeric_limits<double>::max)();
                target_position = current_position;
                for (std::size_t position = 0; position < candidates.size(); ++position)
                {
                    if (position == current_position)
                        continue;
                    const Vec2 candidate_center = center(
                        implementation.nodes[candidates[position]].bounds);
                    const double x = candidate_center.x - origin.x;
                    const double y = candidate_center.y - origin.y;
                    bool eligible = false;
                    double primary = 0.0;
                    double perpendicular = 0.0;
                    switch (command)
                    {
                    case NavigationCommand::left:
                        eligible = x < -comparison_epsilon;
                        primary = -x;
                        perpendicular = std::abs(y);
                        break;
                    case NavigationCommand::right:
                        eligible = x > comparison_epsilon;
                        primary = x;
                        perpendicular = std::abs(y);
                        break;
                    case NavigationCommand::up:
                        eligible = y < -comparison_epsilon;
                        primary = -y;
                        perpendicular = std::abs(x);
                        break;
                    case NavigationCommand::down:
                        eligible = y > comparison_epsilon;
                        primary = y;
                        perpendicular = std::abs(x);
                        break;
                    default:
                        break;
                    }
                    if (!eligible)
                        continue;
                    const double score = primary + perpendicular * 2.0;
                    const double distance = x * x + y * y;
                    if (score < best_score
                        || (almost_equal(score, best_score) && distance < best_distance))
                    {
                        target_position = position;
                        best_score = score;
                        best_distance = distance;
                    }
                }
            }
            break;
        }

        if (target_position >= candidates.size())
            target_position = 0u;
        const NodeId previous_focus = implementation.focus;
        const auto previous_nodes = implementation.selected_nodes;
        const NodeId target = implementation.nodes[candidates[target_position]].id;
        const SelectionResult selection = select_node(
            target,
            extend_selection ? SelectionMode::add : SelectionMode::replace);
        return {
            .focused_node_id = implementation.focus,
            .error = selection.error,
            .changed = selection.changed,
            .selection_changed = previous_nodes != implementation.selected_nodes,
            .focus_changed = previous_focus != implementation.focus
        };
    }

    PointerResult Controller::pointer_down(const PointerInput& input)
    {
        Implementation& implementation = *implementation_;
        if (implementation.drag.mode != Implementation::DragMode::none)
        {
            return {
                .error = ErrorCode::pointer_already_active,
                .accepted = false
            };
        }
        if (!finite(input.screen_position))
        {
            return {
                .error = ErrorCode::invalid_geometry,
                .accepted = false
            };
        }

        if (input.disconnect_gesture)
        {
            if (const auto edge = hit_test_edge(input.screen_position); edge.has_value())
            {
                IntentResult requested = disconnect_intent(*edge);
                return {
                    .hit = {.kind = HitKind::edge, .edge_id = *edge},
                    .intent = std::move(requested.intent),
                    .accepted = requested.accepted
                };
            }
            return {};
        }

        if (input.button == PointerButton::middle || input.pan_gesture)
        {
            implementation.drag = {
                .mode = Implementation::DragMode::pan,
                .start_screen = input.screen_position,
                .last_screen = input.screen_position,
                .start_world = implementation.to_world(input.screen_position),
                .selection_mode = input.selection_mode
            };
            return {
                .intent = {
                    .kind = IntentKind::pan_view,
                    .phase = IntentPhase::begin
                }
            };
        }
        if (input.button != PointerButton::primary)
            return {};

        const Hit hit = hit_test(input.screen_position);
        if (hit.kind == HitKind::pin)
        {
            const PinLayout& pin = implementation.pins[
                implementation.pin_indices.at(hit.pin_id)];
            if (!pin.connectable)
                return {.hit = hit};
            implementation.drag = {
                .mode = Implementation::DragMode::connect_pins,
                .start_screen = input.screen_position,
                .last_screen = input.screen_position,
                .start_world = implementation.to_world(input.screen_position),
                .selection_mode = input.selection_mode,
                .start_pin_id = hit.pin_id
            };
            WorkspaceIntent intent{
                .kind = IntentKind::connect_pins,
                .phase = IntentPhase::begin
            };
            if (pin.direction == PinDirection::output)
                intent.output_pin_id = pin.id;
            else
                intent.input_pin_id = pin.id;
            return {
                .hit = hit,
                .intent = std::move(intent)
            };
        }

        if (hit.kind == HitKind::node)
        {
            const auto previous_nodes = implementation.selected_nodes;
            const auto previous_edges = implementation.selected_edges;
            SelectionResult selection{};
            if (input.selection_mode == SelectionMode::replace
                && implementation.selected_nodes.contains(hit.node_id))
            {
                selection = {
                    .selected_node_count = implementation.selected_nodes.size(),
                    .selected_edge_count = implementation.selected_edges.size()
                };
            }
            else
            {
                selection = select_node(hit.node_id, input.selection_mode);
            }
            if (!selection.accepted)
            {
                return {
                    .error = selection.error,
                    .hit = hit,
                    .accepted = false
                };
            }
            if (!implementation.selected_nodes.contains(hit.node_id))
            {
                return {
                    .hit = hit,
                    .selection_changed = previous_nodes != implementation.selected_nodes
                        || previous_edges != implementation.selected_edges
                };
            }
            implementation.drag = {
                .mode = Implementation::DragMode::move_nodes,
                .start_screen = input.screen_position,
                .last_screen = input.screen_position,
                .start_world = implementation.to_world(input.screen_position),
                .selection_mode = input.selection_mode,
                .node_ids = implementation.ordered_selected_nodes
            };
            return {
                .hit = hit,
                .intent = {
                    .kind = IntentKind::move_nodes,
                    .phase = IntentPhase::begin,
                    .node_ids = implementation.drag.node_ids
                },
                .selection_changed = previous_nodes != implementation.selected_nodes
                    || previous_edges != implementation.selected_edges
            };
        }

        if (hit.kind == HitKind::edge)
        {
            const SelectionResult selection = select_edge(hit.edge_id, input.selection_mode);
            return {
                .error = selection.error,
                .hit = hit,
                .accepted = selection.accepted,
                .selection_changed = selection.changed
            };
        }

        implementation.drag = {
            .mode = Implementation::DragMode::box_selection,
            .start_screen = input.screen_position,
            .last_screen = input.screen_position,
            .start_world = implementation.to_world(input.screen_position),
            .selection_mode = input.selection_mode
        };
        return {
            .intent = {
                .kind = IntentKind::box_selection,
                .phase = IntentPhase::begin,
                .screen_box = {input.screen_position.x, input.screen_position.y, 0.0, 0.0},
                .world_box = {
                    implementation.drag.start_world.x,
                    implementation.drag.start_world.y,
                    0.0,
                    0.0}
            }
        };
    }

    PointerResult Controller::pointer_move(Vec2 screen_position)
    {
        Implementation& implementation = *implementation_;
        if (implementation.drag.mode == Implementation::DragMode::none)
        {
            return {
                .error = ErrorCode::pointer_not_active,
                .accepted = false
            };
        }
        if (!finite(screen_position))
        {
            return {
                .error = ErrorCode::invalid_geometry,
                .accepted = false
            };
        }
        const Vec2 step{
            screen_position.x - implementation.drag.last_screen.x,
            screen_position.y - implementation.drag.last_screen.y
        };
        const Vec2 total{
            screen_position.x - implementation.drag.start_screen.x,
            screen_position.y - implementation.drag.start_screen.y
        };
        implementation.drag.last_screen = screen_position;

        switch (implementation.drag.mode)
        {
        case Implementation::DragMode::pan:
            {
                const ViewResult view_result = pan_by(step);
                return {
                    .error = view_result.error,
                    .intent = {
                        .kind = IntentKind::pan_view,
                        .phase = IntentPhase::update,
                        .screen_delta = step,
                        .total_screen_delta = total
                    },
                    .accepted = view_result.accepted,
                    .view_changed = view_result.changed
                };
            }
        case Implementation::DragMode::move_nodes:
            return {
                .hit = hit_test(screen_position),
                .intent = {
                    .kind = IntentKind::move_nodes,
                    .phase = IntentPhase::update,
                    .node_ids = implementation.drag.node_ids,
                    .screen_delta = step,
                    .total_screen_delta = total,
                    .world_delta = {
                        clamp(
                            total.x / implementation.view.zoom,
                            -implementation.limits.maximum_pointer_world_delta,
                            implementation.limits.maximum_pointer_world_delta),
                        clamp(
                            total.y / implementation.view.zoom,
                            -implementation.limits.maximum_pointer_world_delta,
                            implementation.limits.maximum_pointer_world_delta)}
                }
            };
        case Implementation::DragMode::box_selection:
            {
                const Rect screen_box = normalized({
                    implementation.drag.start_screen.x,
                    implementation.drag.start_screen.y,
                    total.x,
                    total.y
                });
                return {
                    .intent = {
                        .kind = IntentKind::box_selection,
                        .phase = IntentPhase::update,
                        .total_screen_delta = total,
                        .screen_box = screen_box,
                        .world_box = implementation.to_world(screen_box)
                    }
                };
            }
        case Implementation::DragMode::connect_pins:
            {
                WorkspaceIntent intent{
                    .kind = IntentKind::connect_pins,
                    .phase = IntentPhase::update,
                    .total_screen_delta = total
                };
                const PinLayout& start = implementation.pins[
                    implementation.pin_indices.at(implementation.drag.start_pin_id)];
                if (start.direction == PinDirection::output)
                    intent.output_pin_id = start.id;
                else
                    intent.input_pin_id = start.id;
                Hit hit{};
                if (const auto target = hit_test_pin(screen_position); target.has_value())
                {
                    const PinLayout& target_pin = implementation.pins[
                        implementation.pin_indices.at(*target)];
                    hit = {
                        .kind = HitKind::pin,
                        .node_id = target_pin.node_id,
                        .pin_id = *target
                    };
                    PinId output = invalid_pin_id;
                    PinId input = invalid_pin_id;
                    if (implementation.compatible_connection(
                            implementation.drag.start_pin_id,
                            *target,
                            output,
                            input)
                        && !implementation.connection_exists(output, input))
                    {
                        intent.output_pin_id = output;
                        intent.input_pin_id = input;
                        intent.valid_target = true;
                    }
                }
                return {
                    .hit = hit,
                    .intent = std::move(intent)
                };
            }
        case Implementation::DragMode::none:
            break;
        }
        return {};
    }

    PointerResult Controller::pointer_up(Vec2 screen_position)
    {
        Implementation& implementation = *implementation_;
        if (implementation.drag.mode == Implementation::DragMode::none)
        {
            return {
                .error = ErrorCode::pointer_not_active,
                .accepted = false
            };
        }
        const Implementation::DragMode mode = implementation.drag.mode;
        const SelectionMode selection_mode = implementation.drag.selection_mode;
        PointerResult result = pointer_move(screen_position);
        if (!result.accepted)
            return result;

        switch (mode)
        {
        case Implementation::DragMode::pan:
        case Implementation::DragMode::move_nodes:
            result.intent.phase = IntentPhase::commit;
            break;
        case Implementation::DragMode::box_selection:
            {
                const SelectionResult selected = box_select(
                    result.intent.screen_box,
                    {.mode = selection_mode});
                result.error = selected.error;
                result.accepted = selected.accepted;
                result.selection_changed = selected.changed;
                result.intent.phase = selected.accepted
                    ? IntentPhase::commit
                    : IntentPhase::cancel;
            }
            break;
        case Implementation::DragMode::connect_pins:
            result.intent.phase = result.intent.valid_target
                ? IntentPhase::commit
                : IntentPhase::cancel;
            break;
        case Implementation::DragMode::none:
            break;
        }
        implementation.drag = {};
        return result;
    }

    PointerResult Controller::cancel_pointer()
    {
        Implementation& implementation = *implementation_;
        if (implementation.drag.mode == Implementation::DragMode::none)
        {
            return {
                .error = ErrorCode::pointer_not_active,
                .accepted = false
            };
        }
        WorkspaceIntent intent{};
        intent.phase = IntentPhase::cancel;
        switch (implementation.drag.mode)
        {
        case Implementation::DragMode::pan:
            intent.kind = IntentKind::pan_view;
            break;
        case Implementation::DragMode::move_nodes:
            intent.kind = IntentKind::move_nodes;
            intent.node_ids = implementation.drag.node_ids;
            break;
        case Implementation::DragMode::box_selection:
            intent.kind = IntentKind::box_selection;
            break;
        case Implementation::DragMode::connect_pins:
            intent.kind = IntentKind::connect_pins;
            break;
        case Implementation::DragMode::none:
            break;
        }
        implementation.drag = {};
        return {.intent = std::move(intent)};
    }

    IntentResult Controller::disconnect_intent(EdgeId edge_id) const
    {
        if (!implementation_->edge_indices.contains(edge_id))
        {
            return {
                .error = ErrorCode::unknown_edge,
                .accepted = false
            };
        }
        return {
            .intent = {
                .kind = IntentKind::disconnect_edges,
                .phase = IntentPhase::commit,
                .edge_ids = {edge_id}
            }
        };
    }

    IntentResult Controller::disconnect_selected_intent() const
    {
        if (implementation_->ordered_selected_edges.empty())
        {
            return {
                .error = ErrorCode::unknown_edge,
                .accepted = false
            };
        }
        return {
            .intent = {
                .kind = IntentKind::disconnect_edges,
                .phase = IntentPhase::commit,
                .edge_ids = implementation_->ordered_selected_edges
            }
        };
    }

    Rect Controller::viewport() const noexcept
    {
        return implementation_->viewport;
    }

    const ViewState& Controller::view() const noexcept
    {
        return implementation_->view;
    }

    std::optional<Rect> Controller::content_bounds() const noexcept
    {
        return implementation_->content;
    }

    std::uint64_t Controller::source_revision() const noexcept
    {
        return implementation_->source_revision;
    }

    std::uint64_t Controller::view_revision() const noexcept
    {
        return implementation_->view_revision;
    }

    bool Controller::using_stale_graph() const noexcept
    {
        return implementation_->stale_graph;
    }

    bool Controller::pointer_active() const noexcept
    {
        return implementation_->drag.mode != Implementation::DragMode::none;
    }

    NodeId Controller::focused_node_id() const noexcept
    {
        return implementation_->focus;
    }

    const Error& Controller::error() const noexcept
    {
        return implementation_->current_error;
    }

    std::span<const NodeLayout> Controller::nodes() const noexcept
    {
        return implementation_->nodes;
    }

    std::span<const PinLayout> Controller::pins() const noexcept
    {
        return implementation_->pins;
    }

    std::span<const EdgeLayout> Controller::edges() const noexcept
    {
        return implementation_->edges;
    }

    std::span<const NodeProjection> Controller::node_projections() const noexcept
    {
        return implementation_->node_views;
    }

    std::span<const PinProjection> Controller::pin_projections() const noexcept
    {
        return implementation_->pin_views;
    }

    std::span<const EdgeProjection> Controller::edge_projections() const noexcept
    {
        return implementation_->edge_views;
    }

    std::span<const NodeId> Controller::selected_node_ids() const noexcept
    {
        return implementation_->ordered_selected_nodes;
    }

    std::span<const EdgeId> Controller::selected_edge_ids() const noexcept
    {
        return implementation_->ordered_selected_edges;
    }

    ContractFailure run_contract() noexcept
    {
        try
        {
            const std::vector<NodeLayout> nodes{
                {.id = 20u, .bounds = {240.0, 40.0, 120.0, 80.0}, .layout_order = 2u},
                {.id = 10u, .bounds = {20.0, 20.0, 120.0, 80.0}, .layout_order = 1u}
            };
            const std::vector<PinLayout> pins{
                {.id = 203u, .node_id = 20u, .direction = PinDirection::input,
                    .position = {240.0, 90.0}, .layout_order = 2u},
                {.id = 101u, .node_id = 10u, .direction = PinDirection::output,
                    .position = {140.0, 60.0}, .layout_order = 1u},
                {.id = 202u, .node_id = 20u, .direction = PinDirection::input,
                    .position = {240.0, 70.0}, .layout_order = 1u}
            };
            const std::vector<EdgeLayout> edges{
                {.id = 1'001u, .output_pin_id = 101u, .input_pin_id = 202u,
                    .layout_order = 7u}
            };

            Controller controller{};
            if (!controller.set_viewport({0.0, 0.0, 800.0, 600.0}).accepted)
                return ContractFailure::transforms;
            const ReplaceResult replaced = controller.replace_graph({
                .nodes = nodes,
                .pins = pins,
                .edges = edges,
                .source_revision = 42u
            });
            if (!replaced.committed || controller.nodes().size() != 2u
                || controller.nodes()[0].id != 10u || controller.nodes()[1].id != 20u)
            {
                return ContractFailure::deterministic_layout;
            }
            if (controller.node_projections()[0].id != 10u
                || controller.edge_projections()[0].id != 1'001u
                || controller.source_revision() != 42u)
            {
                return ContractFailure::stable_projection_ids;
            }

            if (!controller.set_view({.pan = {30.0, 40.0}, .zoom = 2.0}).accepted)
                return ContractFailure::transforms;
            const Vec2 screen = controller.world_to_screen(Vec2{10.0, 20.0});
            const Vec2 world = controller.screen_to_world(screen);
            if (!almost_equal(screen.x, 50.0) || !almost_equal(screen.y, 80.0)
                || !almost_equal(world.x, 10.0) || !almost_equal(world.y, 20.0))
            {
                return ContractFailure::transforms;
            }

            const Vec2 anchor{300.0, 200.0};
            const Vec2 anchored_world = controller.screen_to_world(anchor);
            if (!controller.zoom_at(anchor, 1.0).accepted)
                return ContractFailure::anchored_zoom;
            const Vec2 anchored_screen = controller.world_to_screen(anchored_world);
            if (!almost_equal(anchored_screen.x, anchor.x)
                || !almost_equal(anchored_screen.y, anchor.y))
            {
                return ContractFailure::anchored_zoom;
            }

            const PinProjection pin = controller.pin_projections()[0];
            if (!controller.hit_test_pin(pin.screen_position).has_value())
                return ContractFailure::hit_testing;
            const NodeProjection node = controller.node_projections()[0];
            if (controller.hit_test(center(node.screen_bounds)).kind != HitKind::node)
                return ContractFailure::hit_testing;
            const EdgeProjection edge = controller.edge_projections()[0];
            if (!controller.hit_test_edge(curve_point(edge.screen_curve, 0.5)).has_value())
                return ContractFailure::hit_testing;

            if (!controller.select_node(10u).accepted
                || !controller.select_node(20u, SelectionMode::add).accepted
                || controller.selected_node_ids().size() != 2u)
            {
                return ContractFailure::selection;
            }
            if (!controller.box_select(
                    controller.node_projections()[0].screen_bounds,
                    {.mode = SelectionMode::replace, .require_full_containment = true}).accepted
                || controller.selected_node_ids().size() != 1u
                || controller.selected_node_ids()[0] != 10u)
            {
                return ContractFailure::selection;
            }

            const Rect original_bounds = controller.nodes()[0].bounds;
            const Vec2 node_center = center(controller.node_projections()[0].screen_bounds);
            PointerResult pointer = controller.pointer_down({.screen_position = node_center});
            if (pointer.intent.kind != IntentKind::move_nodes)
                return ContractFailure::pointer_move_intent;
            pointer = controller.pointer_up({node_center.x + 48.0, node_center.y + 24.0});
            if (pointer.intent.kind != IntentKind::move_nodes
                || pointer.intent.phase != IntentPhase::commit
                || pointer.intent.world_delta == Vec2{}
                || controller.nodes()[0].bounds != original_bounds)
            {
                return ContractFailure::pointer_move_intent;
            }

            const Vec2 output_position = controller.pin_projections()[0].id == 101u
                ? controller.pin_projections()[0].screen_position
                : controller.pin_projections()[2].screen_position;
            Vec2 free_input_position{};
            for (const PinProjection& item : controller.pin_projections())
            {
                if (item.id == 203u)
                    free_input_position = item.screen_position;
            }
            pointer = controller.pointer_down({.screen_position = output_position});
            if (pointer.intent.kind != IntentKind::connect_pins)
                return ContractFailure::connection_intents;
            pointer = controller.pointer_up(free_input_position);
            if (pointer.intent.phase != IntentPhase::commit
                || pointer.intent.output_pin_id != 101u
                || pointer.intent.input_pin_id != 203u)
            {
                return ContractFailure::connection_intents;
            }
            const IntentResult disconnect = controller.disconnect_intent(1'001u);
            if (!disconnect.accepted
                || disconnect.intent.kind != IntentKind::disconnect_edges
                || disconnect.intent.edge_ids.size() != 1u)
            {
                return ContractFailure::connection_intents;
            }

            NavigationResult navigation = controller.navigate(NavigationCommand::first);
            navigation = controller.navigate(NavigationCommand::right);
            if (navigation.focused_node_id != 20u)
                return ContractFailure::keyboard_navigation;

            if (!controller.fit_to_content(20.0).accepted)
                return ContractFailure::fit_to_content;
            const Rect first_fitted = controller.node_projections()[0].screen_bounds;
            const Rect second_fitted = controller.node_projections()[1].screen_bounds;
            if (!contains(controller.viewport(), Vec2{first_fitted.x, first_fitted.y})
                || !contains(
                    controller.viewport(),
                    Vec2{second_fitted.x + second_fitted.width,
                        second_fitted.y + second_fitted.height}))
            {
                return ContractFailure::fit_to_content;
            }

            Controller bounded{Limits{.maximum_nodes = 1u}};
            if (bounded.replace_graph({.nodes = nodes}).error
                != ErrorCode::node_limit_exceeded)
            {
                return ContractFailure::bounded_rejection;
            }
            return ContractFailure::none;
        }
        catch (...)
        {
            return ContractFailure::bounded_rejection;
        }
    }
}
