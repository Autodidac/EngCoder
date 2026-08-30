// SPDX-License-Identifier: LicenseRef-MIT-NoSell
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace epochengine::gui_lib::node_graph_workspace
{
    using NodeId = std::uint64_t;
    using PinId = std::uint64_t;
    using EdgeId = std::uint64_t;

    inline constexpr NodeId invalid_node_id = 0u;
    inline constexpr PinId invalid_pin_id = 0u;
    inline constexpr EdgeId invalid_edge_id = 0u;

    struct Vec2 final
    {
        double x{};
        double y{};

        friend constexpr bool operator==(Vec2, Vec2) noexcept = default;
    };

    struct Rect final
    {
        double x{};
        double y{};
        double width{};
        double height{};

        friend constexpr bool operator==(Rect, Rect) noexcept = default;
    };

    struct CubicCurve final
    {
        Vec2 start{};
        Vec2 control1{};
        Vec2 control2{};
        Vec2 end{};
    };

    enum class PinDirection : std::uint8_t
    {
        input,
        output
    };

    struct NodeLayout final
    {
        NodeId id{invalid_node_id};
        Rect bounds{};
        std::uint64_t layout_order{};
        bool selectable{true};
    };

    struct PinLayout final
    {
        PinId id{invalid_pin_id};
        NodeId node_id{invalid_node_id};
        PinDirection direction{PinDirection::input};
        Vec2 position{};
        double hit_radius{6.0};
        std::uint64_t layout_order{};
        bool connectable{true};
    };

    struct EdgeLayout final
    {
        EdgeId id{invalid_edge_id};
        PinId output_pin_id{invalid_pin_id};
        PinId input_pin_id{invalid_pin_id};
        std::uint64_t layout_order{};
        bool selectable{true};
    };

    struct GraphLayoutInput final
    {
        std::span<const NodeLayout> nodes{};
        std::span<const PinLayout> pins{};
        std::span<const EdgeLayout> edges{};
        std::uint64_t source_revision{};
    };

    struct NodeProjection final
    {
        NodeId id{invalid_node_id};
        Rect world_bounds{};
        Rect screen_bounds{};
        std::uint64_t layout_order{};
        bool selectable{};
        bool selected{};
        bool focused{};
    };

    struct PinProjection final
    {
        PinId id{invalid_pin_id};
        NodeId node_id{invalid_node_id};
        PinDirection direction{PinDirection::input};
        Vec2 world_position{};
        Vec2 screen_position{};
        double screen_hit_radius{};
        std::uint64_t layout_order{};
        bool connectable{};
    };

    struct EdgeProjection final
    {
        EdgeId id{invalid_edge_id};
        PinId output_pin_id{invalid_pin_id};
        PinId input_pin_id{invalid_pin_id};
        NodeId output_node_id{invalid_node_id};
        NodeId input_node_id{invalid_node_id};
        CubicCurve world_curve{};
        CubicCurve screen_curve{};
        std::uint64_t layout_order{};
        bool selectable{};
        bool selected{};
    };

    struct ViewState final
    {
        Vec2 pan{};
        double zoom{1.0};

        friend constexpr bool operator==(const ViewState&, const ViewState&) noexcept = default;
    };

    struct Limits final
    {
        std::size_t maximum_nodes{4'096u};
        std::size_t maximum_pins{16'384u};
        std::size_t maximum_edges{16'384u};
        std::size_t maximum_selected_items{4'096u};
        std::size_t edge_hit_test_segments{24u};
        double minimum_zoom{0.1};
        double maximum_zoom{8.0};
        double maximum_world_coordinate{1'000'000.0};
        double maximum_node_extent{100'000.0};
        double maximum_viewport_extent{100'000.0};
        double maximum_pan{10'000'000.0};
        double maximum_pin_hit_radius{1'024.0};
        double minimum_screen_pin_hit_radius{5.0};
        double maximum_screen_pin_hit_radius{24.0};
        double edge_hit_tolerance{7.0};
        double minimum_edge_control_distance{24.0};
        double maximum_edge_control_distance{240.0};
        double maximum_pointer_world_delta{2'000'000.0};

        [[nodiscard]] bool valid() const noexcept;
    };

    enum class ErrorCode : std::uint8_t
    {
        none,
        invalid_limits,
        node_limit_exceeded,
        pin_limit_exceeded,
        edge_limit_exceeded,
        selection_limit_exceeded,
        invalid_node_id,
        invalid_pin_id,
        invalid_edge_id,
        duplicate_node_id,
        duplicate_pin_id,
        duplicate_edge_id,
        duplicate_connection,
        invalid_geometry,
        invalid_pin_direction,
        missing_node,
        missing_pin,
        incompatible_pins,
        unknown_node,
        unknown_pin,
        unknown_edge,
        not_selectable,
        invalid_viewport,
        invalid_view,
        empty_graph,
        pointer_already_active,
        pointer_not_active
    };

    [[nodiscard]] constexpr std::string_view error_code_name(ErrorCode code) noexcept
    {
        switch (code)
        {
        case ErrorCode::none: return "none";
        case ErrorCode::invalid_limits: return "invalid_limits";
        case ErrorCode::node_limit_exceeded: return "node_limit_exceeded";
        case ErrorCode::pin_limit_exceeded: return "pin_limit_exceeded";
        case ErrorCode::edge_limit_exceeded: return "edge_limit_exceeded";
        case ErrorCode::selection_limit_exceeded: return "selection_limit_exceeded";
        case ErrorCode::invalid_node_id: return "invalid_node_id";
        case ErrorCode::invalid_pin_id: return "invalid_pin_id";
        case ErrorCode::invalid_edge_id: return "invalid_edge_id";
        case ErrorCode::duplicate_node_id: return "duplicate_node_id";
        case ErrorCode::duplicate_pin_id: return "duplicate_pin_id";
        case ErrorCode::duplicate_edge_id: return "duplicate_edge_id";
        case ErrorCode::duplicate_connection: return "duplicate_connection";
        case ErrorCode::invalid_geometry: return "invalid_geometry";
        case ErrorCode::invalid_pin_direction: return "invalid_pin_direction";
        case ErrorCode::missing_node: return "missing_node";
        case ErrorCode::missing_pin: return "missing_pin";
        case ErrorCode::incompatible_pins: return "incompatible_pins";
        case ErrorCode::unknown_node: return "unknown_node";
        case ErrorCode::unknown_pin: return "unknown_pin";
        case ErrorCode::unknown_edge: return "unknown_edge";
        case ErrorCode::not_selectable: return "not_selectable";
        case ErrorCode::invalid_viewport: return "invalid_viewport";
        case ErrorCode::invalid_view: return "invalid_view";
        case ErrorCode::empty_graph: return "empty_graph";
        case ErrorCode::pointer_already_active: return "pointer_already_active";
        case ErrorCode::pointer_not_active: return "pointer_not_active";
        }
        return "invalid";
    }

    struct Error final
    {
        ErrorCode code{ErrorCode::none};
        std::string message{};
        std::uint64_t related_id{};

        [[nodiscard]] bool present() const noexcept
        {
            return code != ErrorCode::none;
        }
    };

    struct ReplaceOptions final
    {
        bool preserve_selection{true};
        bool preserve_focus{true};
    };

    struct ReplaceResult final
    {
        ErrorCode error{ErrorCode::none};
        std::size_t node_count{};
        std::size_t pin_count{};
        std::size_t edge_count{};
        std::size_t stale_selection_count{};
        bool committed{};
        bool selection_changed{};
        bool focus_changed{};
    };

    struct MutationResult final
    {
        ErrorCode error{ErrorCode::none};
        bool accepted{true};
        bool changed{};
    };

    struct ViewResult final
    {
        ErrorCode error{ErrorCode::none};
        ViewState view{};
        bool accepted{true};
        bool changed{};
    };

    enum class HitKind : std::uint8_t
    {
        none,
        node,
        pin,
        edge
    };

    struct Hit final
    {
        HitKind kind{HitKind::none};
        NodeId node_id{invalid_node_id};
        PinId pin_id{invalid_pin_id};
        EdgeId edge_id{invalid_edge_id};
    };

    struct HitTestOptions final
    {
        bool pins{true};
        bool nodes{true};
        bool edges{true};
        double edge_tolerance{};
    };

    enum class SelectionMode : std::uint8_t
    {
        replace,
        add,
        toggle,
        subtract
    };

    struct SelectionResult final
    {
        ErrorCode error{ErrorCode::none};
        std::size_t selected_node_count{};
        std::size_t selected_edge_count{};
        bool accepted{true};
        bool changed{};
        bool focus_changed{};
    };

    struct BoxSelectionOptions final
    {
        SelectionMode mode{SelectionMode::replace};
        bool require_full_containment{};
    };

    enum class NavigationCommand : std::uint8_t
    {
        first,
        previous,
        next,
        last,
        left,
        right,
        up,
        down
    };

    struct NavigationResult final
    {
        NodeId focused_node_id{invalid_node_id};
        ErrorCode error{ErrorCode::none};
        bool changed{};
        bool selection_changed{};
        bool focus_changed{};
    };

    enum class PointerButton : std::uint8_t
    {
        primary,
        middle,
        secondary
    };

    struct PointerInput final
    {
        Vec2 screen_position{};
        PointerButton button{PointerButton::primary};
        SelectionMode selection_mode{SelectionMode::replace};
        bool pan_gesture{};
        bool disconnect_gesture{};
    };

    enum class IntentKind : std::uint8_t
    {
        none,
        pan_view,
        move_nodes,
        box_selection,
        connect_pins,
        disconnect_edges
    };

    enum class IntentPhase : std::uint8_t
    {
        begin,
        update,
        commit,
        cancel
    };

    struct WorkspaceIntent final
    {
        IntentKind kind{IntentKind::none};
        IntentPhase phase{IntentPhase::begin};
        std::vector<NodeId> node_ids{};
        std::vector<EdgeId> edge_ids{};
        PinId output_pin_id{invalid_pin_id};
        PinId input_pin_id{invalid_pin_id};
        Vec2 screen_delta{};
        Vec2 total_screen_delta{};
        Vec2 world_delta{};
        Rect screen_box{};
        Rect world_box{};
        bool valid_target{};
    };

    struct PointerResult final
    {
        ErrorCode error{ErrorCode::none};
        Hit hit{};
        WorkspaceIntent intent{};
        bool accepted{true};
        bool selection_changed{};
        bool view_changed{};
    };

    struct IntentResult final
    {
        ErrorCode error{ErrorCode::none};
        WorkspaceIntent intent{};
        bool accepted{true};
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
        [[nodiscard]] ReplaceResult replace_graph(
            const GraphLayoutInput& input,
            ReplaceOptions options = {});

        [[nodiscard]] MutationResult set_viewport(Rect viewport);
        [[nodiscard]] ViewResult set_view(ViewState view);
        [[nodiscard]] ViewResult pan_by(Vec2 screen_delta);
        [[nodiscard]] ViewResult zoom_at(Vec2 screen_anchor, double wheel_steps);
        [[nodiscard]] ViewResult fit_to_content(double screen_padding = 32.0);
        [[nodiscard]] Vec2 world_to_screen(Vec2 world) const noexcept;
        [[nodiscard]] Vec2 screen_to_world(Vec2 screen) const noexcept;
        [[nodiscard]] Rect world_to_screen(Rect world) const noexcept;
        [[nodiscard]] Rect screen_to_world(Rect screen) const noexcept;

        [[nodiscard]] std::optional<NodeId> hit_test_node(Vec2 screen_point) const noexcept;
        [[nodiscard]] std::optional<PinId> hit_test_pin(Vec2 screen_point) const noexcept;
        [[nodiscard]] std::optional<EdgeId> hit_test_edge(
            Vec2 screen_point,
            double tolerance = 0.0) const noexcept;
        [[nodiscard]] Hit hit_test(
            Vec2 screen_point,
            HitTestOptions options = {}) const noexcept;

        [[nodiscard]] SelectionResult select_node(
            NodeId id,
            SelectionMode mode = SelectionMode::replace);
        [[nodiscard]] SelectionResult select_edge(
            EdgeId id,
            SelectionMode mode = SelectionMode::replace);
        [[nodiscard]] SelectionResult select_at(
            Vec2 screen_point,
            SelectionMode mode = SelectionMode::replace);
        [[nodiscard]] SelectionResult box_select(
            Rect screen_box,
            BoxSelectionOptions options = {});
        [[nodiscard]] bool clear_selection();
        [[nodiscard]] NavigationResult navigate(
            NavigationCommand command,
            bool extend_selection = false);

        [[nodiscard]] PointerResult pointer_down(const PointerInput& input);
        [[nodiscard]] PointerResult pointer_move(Vec2 screen_position);
        [[nodiscard]] PointerResult pointer_up(Vec2 screen_position);
        [[nodiscard]] PointerResult cancel_pointer();
        [[nodiscard]] IntentResult disconnect_intent(EdgeId edge_id) const;
        [[nodiscard]] IntentResult disconnect_selected_intent() const;

        [[nodiscard]] Rect viewport() const noexcept;
        [[nodiscard]] const ViewState& view() const noexcept;
        [[nodiscard]] std::optional<Rect> content_bounds() const noexcept;
        [[nodiscard]] std::uint64_t source_revision() const noexcept;
        [[nodiscard]] std::uint64_t view_revision() const noexcept;
        [[nodiscard]] bool using_stale_graph() const noexcept;
        [[nodiscard]] bool pointer_active() const noexcept;
        [[nodiscard]] NodeId focused_node_id() const noexcept;
        [[nodiscard]] const Error& error() const noexcept;
        [[nodiscard]] std::span<const NodeLayout> nodes() const noexcept;
        [[nodiscard]] std::span<const PinLayout> pins() const noexcept;
        [[nodiscard]] std::span<const EdgeLayout> edges() const noexcept;
        [[nodiscard]] std::span<const NodeProjection> node_projections() const noexcept;
        [[nodiscard]] std::span<const PinProjection> pin_projections() const noexcept;
        [[nodiscard]] std::span<const EdgeProjection> edge_projections() const noexcept;
        [[nodiscard]] std::span<const NodeId> selected_node_ids() const noexcept;
        [[nodiscard]] std::span<const EdgeId> selected_edge_ids() const noexcept;

    private:
        struct Implementation;
        std::unique_ptr<Implementation> implementation_;
    };

    enum class ContractFailure : std::uint8_t
    {
        none,
        deterministic_layout,
        stable_projection_ids,
        transforms,
        anchored_zoom,
        hit_testing,
        selection,
        pointer_move_intent,
        connection_intents,
        keyboard_navigation,
        fit_to_content,
        bounded_rejection
    };

    [[nodiscard]] constexpr std::string_view contract_failure_name(
        ContractFailure failure) noexcept
    {
        switch (failure)
        {
        case ContractFailure::none: return "pass";
        case ContractFailure::deterministic_layout: return "deterministic_layout";
        case ContractFailure::stable_projection_ids: return "stable_projection_ids";
        case ContractFailure::transforms: return "transforms";
        case ContractFailure::anchored_zoom: return "anchored_zoom";
        case ContractFailure::hit_testing: return "hit_testing";
        case ContractFailure::selection: return "selection";
        case ContractFailure::pointer_move_intent: return "pointer_move_intent";
        case ContractFailure::connection_intents: return "connection_intents";
        case ContractFailure::keyboard_navigation: return "keyboard_navigation";
        case ContractFailure::fit_to_content: return "fit_to_content";
        case ContractFailure::bounded_rejection: return "bounded_rejection";
        }
        return "unknown";
    }

    [[nodiscard]] ContractFailure run_contract() noexcept;
}
