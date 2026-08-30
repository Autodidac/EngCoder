module;

#include <cstdint>
#include <string_view>

export module epoch.gui.tile_workspace;

export import epoch.gui;

export namespace epochengine::gui_lib::tile_workspace
{
    inline constexpr std::uint32_t invalid_index = 0xffff'ffffu;

    enum class Tool : std::uint8_t
    {
        select,
        pencil,
        eraser,
        fill,
        object,
        collision
    };

    [[nodiscard]] constexpr std::string_view tool_name(Tool tool) noexcept
    {
        switch (tool)
        {
        case Tool::select: return "Select";
        case Tool::pencil: return "Pencil";
        case Tool::eraser: return "Eraser";
        case Tool::fill: return "Fill";
        case Tool::object: return "Object";
        case Tool::collision: return "Collision";
        }
        return "Unknown";
    }

    struct WorkspaceState final
    {
        Tool tool{Tool::pencil};
        std::uint32_t selected_palette{invalid_index};
        std::uint32_t selected_layer{invalid_index};
        float palette_scroll{};
        float layer_scroll{};
        float zoom{1.0f};
        Vec2 pan{};
        bool show_grid{true};
        bool snap_to_grid{true};
    };

    struct WorkspaceLimits final
    {
        float minimum_zoom{0.25f};
        float maximum_zoom{8.0f};
        float minimum_canvas_width{160.0f};
        float minimum_canvas_height{120.0f};
        float minimum_sidebar_width{128.0f};
        float maximum_sidebar_width{360.0f};
        float minimum_inspector_width{144.0f};
        float maximum_inspector_width{360.0f};

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return minimum_zoom > 0.0f && maximum_zoom >= minimum_zoom
                && minimum_canvas_width > 0.0f && minimum_canvas_height > 0.0f
                && minimum_sidebar_width > 0.0f
                && maximum_sidebar_width >= minimum_sidebar_width
                && minimum_inspector_width > 0.0f
                && maximum_inspector_width >= minimum_inspector_width;
        }
    };

    struct WorkspaceLayoutOptions final
    {
        Rect area{};
        float toolbar_height{32.0f};
        float status_height{24.0f};
        float sidebar_width{232.0f};
        float inspector_width{220.0f};
        float gap{6.0f};
        float padding{6.0f};
        float palette_fraction{0.58f};
        bool show_inspector{true};
        WorkspaceLimits limits{};
    };

    struct WorkspaceLayout final
    {
        Rect area{};
        Rect toolbar{};
        Rect palette{};
        Rect layers{};
        Rect canvas{};
        Rect inspector{};
        Rect status{};
        bool inspector_visible{};
        bool compact{};
        bool valid{};
    };

    struct PaletteGridOptions final
    {
        Rect viewport{};
        std::uint32_t item_count{};
        float preferred_cell_size{52.0f};
        float minimum_cell_size{32.0f};
        float maximum_cell_size{96.0f};
        float gap{6.0f};
        float padding{6.0f};
        float scroll_y{};
        std::uint32_t maximum_columns{16u};
    };

    struct PaletteGridLayout final
    {
        Rect viewport{};
        std::uint32_t item_count{};
        std::uint32_t columns{};
        std::uint32_t rows{};
        float cell_size{};
        float gap{};
        float padding{};
        float content_height{};
        float scroll_y{};
        float maximum_scroll_y{};
        std::uint32_t first_visible_row{};
        std::uint32_t past_last_visible_row{};
        bool valid{};
    };

    struct PaletteCellLayout final
    {
        std::uint32_t index{invalid_index};
        Rect bounds{};
        Rect content{};
        bool visible{};
        bool hovered{};
        bool selected{};
    };

    struct CellCoordinate final
    {
        std::uint32_t x{};
        std::uint32_t y{};

        friend constexpr bool operator==(CellCoordinate, CellCoordinate) noexcept = default;
    };

    struct CellRange final
    {
        CellCoordinate first{};
        CellCoordinate past_last{};
        bool empty{true};
    };

    struct TileCanvasOptions final
    {
        Rect viewport{};
        std::uint32_t map_width{};
        std::uint32_t map_height{};
        float base_cell_pixels{24.0f};
        float zoom{1.0f};
        Vec2 pan{};
        float overscroll_cells{1.0f};
        WorkspaceLimits limits{};
    };

    struct TileCanvasLayout final
    {
        Rect viewport{};
        Rect map_bounds{};
        Vec2 origin{};
        Vec2 pan{};
        float zoom{1.0f};
        float cell_pixels{};
        std::uint32_t map_width{};
        std::uint32_t map_height{};
        CellRange visible_cells{};
        bool valid{};
    };

    struct CanvasView final
    {
        float zoom{1.0f};
        Vec2 pan{};
    };

    class TileWorkspaceController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void normalize_state(
            WorkspaceState& state,
            std::uint32_t palette_count,
            std::uint32_t layer_count,
            const WorkspaceLimits& limits = {}) const noexcept;
        [[nodiscard]] WorkspaceLayout make_layout(
            const WorkspaceLayoutOptions& options) const noexcept;
        [[nodiscard]] PaletteGridLayout make_palette_grid(
            const PaletteGridOptions& options) const noexcept;
        [[nodiscard]] PaletteCellLayout make_palette_cell(
            const PaletteGridLayout& layout,
            std::uint32_t index,
            Vec2 pointer,
            bool selected = false) const noexcept;
        [[nodiscard]] std::uint32_t palette_index_at(
            const PaletteGridLayout& layout,
            Vec2 point) const noexcept;
        [[nodiscard]] TileCanvasLayout make_canvas(
            const TileCanvasOptions& options) const noexcept;
        [[nodiscard]] CellCoordinate cell_at(
            const TileCanvasLayout& layout,
            Vec2 point) const noexcept;
        [[nodiscard]] Rect cell_rect(
            const TileCanvasLayout& layout,
            CellCoordinate cell,
            float inset = 0.0f) const noexcept;
        [[nodiscard]] CanvasView zoom_at(
            const TileCanvasLayout& layout,
            Vec2 anchor,
            float wheel_steps,
            const WorkspaceLimits& limits = {}) const noexcept;
        [[nodiscard]] Vec2 normalize_pan(
            const TileCanvasOptions& options,
            Vec2 pan) const noexcept;
    };

    [[nodiscard]] const TileWorkspaceController& controller() noexcept;

    enum class ContractFailure : std::uint8_t
    {
        none,
        state_normalization,
        wide_layout,
        compact_layout,
        palette_layout,
        palette_hit_test,
        palette_scroll,
        canvas_layout,
        canvas_hit_test,
        canvas_visible_range,
        anchored_zoom,
        pan_normalization,
        invalid_input
    };

    [[nodiscard]] constexpr std::string_view contract_failure_name(
        ContractFailure failure) noexcept
    {
        switch (failure)
        {
        case ContractFailure::none: return "pass";
        case ContractFailure::state_normalization: return "state_normalization";
        case ContractFailure::wide_layout: return "wide_layout";
        case ContractFailure::compact_layout: return "compact_layout";
        case ContractFailure::palette_layout: return "palette_layout";
        case ContractFailure::palette_hit_test: return "palette_hit_test";
        case ContractFailure::palette_scroll: return "palette_scroll";
        case ContractFailure::canvas_layout: return "canvas_layout";
        case ContractFailure::canvas_hit_test: return "canvas_hit_test";
        case ContractFailure::canvas_visible_range: return "canvas_visible_range";
        case ContractFailure::anchored_zoom: return "anchored_zoom";
        case ContractFailure::pan_normalization: return "pan_normalization";
        case ContractFailure::invalid_input: return "invalid_input";
        }
        return "unknown";
    }

    [[nodiscard]] ContractFailure run_contract() noexcept;
}
