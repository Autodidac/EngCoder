module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <string_view>
module epoch.gui.tile_workspace;

namespace epochengine::gui_lib::tile_workspace
{
    namespace
    {
        [[nodiscard]] bool finite(float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool valid_rect(Rect value) noexcept
        {
            return finite(value.position.x) && finite(value.position.y)
                && finite(value.size.x) && finite(value.size.y)
                && value.size.x > 0.0f && value.size.y > 0.0f;
        }

        [[nodiscard]] float nonnegative(float value) noexcept
        {
            return finite(value) && value > 0.0f ? value : 0.0f;
        }

        [[nodiscard]] float positive_or(float value, float fallback) noexcept
        {
            return finite(value) && value > 0.0f ? value : fallback;
        }

        [[nodiscard]] std::uint32_t ceil_div(
            std::uint32_t numerator,
            std::uint32_t denominator) noexcept
        {
            if (denominator == 0u || numerator == 0u)
                return 0u;
            return 1u + (numerator - 1u) / denominator;
        }

        [[nodiscard]] float clamp_zoom(
            float zoom,
            const WorkspaceLimits& limits) noexcept
        {
            if (!limits.valid())
                return 1.0f;
            if (!finite(zoom))
                zoom = 1.0f;
            return std::clamp(zoom, limits.minimum_zoom, limits.maximum_zoom);
        }

        [[nodiscard]] Vec2 map_center_origin(
            const TileCanvasOptions& options,
            float cellPixels) noexcept
        {
            const float mapWidth = static_cast<float>(options.map_width) * cellPixels;
            const float mapHeight = static_cast<float>(options.map_height) * cellPixels;
            return {
                options.viewport.position.x + (options.viewport.size.x - mapWidth) * 0.5f,
                options.viewport.position.y + (options.viewport.size.y - mapHeight) * 0.5f
            };
        }

        [[nodiscard]] Vec2 normalized_canvas_pan(
            const TileCanvasOptions& options,
            Vec2 requested) noexcept
        {
            if (!valid_rect(options.viewport) || options.map_width == 0u
                || options.map_height == 0u || !options.limits.valid())
            {
                return {};
            }

            if (!finite(requested.x))
                requested.x = 0.0f;
            if (!finite(requested.y))
                requested.y = 0.0f;

            const float zoom = clamp_zoom(options.zoom, options.limits);
            const float baseCell = positive_or(options.base_cell_pixels, 1.0f);
            const float cellPixels = baseCell * zoom;
            if (!finite(cellPixels) || cellPixels <= 0.0f)
                return {};

            const Vec2 centered = map_center_origin(options, cellPixels);
            const float mapWidth = static_cast<float>(options.map_width) * cellPixels;
            const float mapHeight = static_cast<float>(options.map_height) * cellPixels;
            const float visibleMargin = (std::max)(
                1.0f,
                positive_or(options.overscroll_cells, 1.0f) * cellPixels);

            const float minimumOriginX = options.viewport.position.x - mapWidth + visibleMargin;
            const float maximumOriginX = options.viewport.position.x
                + options.viewport.size.x - visibleMargin;
            const float minimumOriginY = options.viewport.position.y - mapHeight + visibleMargin;
            const float maximumOriginY = options.viewport.position.y
                + options.viewport.size.y - visibleMargin;

            const float requestedOriginX = centered.x + requested.x;
            const float requestedOriginY = centered.y + requested.y;
            const float originX = minimumOriginX <= maximumOriginX
                ? std::clamp(requestedOriginX, minimumOriginX, maximumOriginX)
                : centered.x;
            const float originY = minimumOriginY <= maximumOriginY
                ? std::clamp(requestedOriginY, minimumOriginY, maximumOriginY)
                : centered.y;
            return {originX - centered.x, originY - centered.y};
        }

        [[nodiscard]] CellRange visible_cells(
            const TileCanvasLayout& layout) noexcept
        {
            CellRange result{};
            if (!layout.valid || layout.cell_pixels <= 0.0f)
                return result;

            const float viewportRight = layout.viewport.position.x
                + layout.viewport.size.x;
            const float viewportBottom = layout.viewport.position.y
                + layout.viewport.size.y;
            const float mapRight = layout.map_bounds.position.x
                + layout.map_bounds.size.x;
            const float mapBottom = layout.map_bounds.position.y
                + layout.map_bounds.size.y;
            if (viewportRight <= layout.map_bounds.position.x
                || viewportBottom <= layout.map_bounds.position.y
                || layout.viewport.position.x >= mapRight
                || layout.viewport.position.y >= mapBottom)
            {
                return result;
            }

            const float left = (std::max)(
                layout.viewport.position.x,
                layout.map_bounds.position.x);
            const float top = (std::max)(
                layout.viewport.position.y,
                layout.map_bounds.position.y);
            const float right = (std::min)(viewportRight, mapRight);
            const float bottom = (std::min)(viewportBottom, mapBottom);

            const auto clamp_axis = [](float value, std::uint32_t limit) noexcept
            {
                if (!finite(value) || value <= 0.0f)
                    return 0u;
                if (value >= static_cast<float>(limit))
                    return limit;
                return static_cast<std::uint32_t>(value);
            };

            const float firstX = std::floor(
                (left - layout.origin.x) / layout.cell_pixels);
            const float firstY = std::floor(
                (top - layout.origin.y) / layout.cell_pixels);
            const float lastX = std::ceil(
                (right - layout.origin.x) / layout.cell_pixels);
            const float lastY = std::ceil(
                (bottom - layout.origin.y) / layout.cell_pixels);
            result.first = {
                clamp_axis(firstX, layout.map_width),
                clamp_axis(firstY, layout.map_height)
            };
            result.past_last = {
                clamp_axis(lastX, layout.map_width),
                clamp_axis(lastY, layout.map_height)
            };
            result.empty = result.first.x >= result.past_last.x
                || result.first.y >= result.past_last.y;
            return result;
        }

        [[nodiscard]] bool nearly_equal(
            float first,
            float second,
            float tolerance = 0.001f) noexcept
        {
            return std::abs(first - second) <= tolerance;
        }
    }

    std::string_view TileWorkspaceController::name() const noexcept
    {
        return "Tile authoring workspace";
    }

    void TileWorkspaceController::normalize_state(
        WorkspaceState& state,
        std::uint32_t paletteCount,
        std::uint32_t layerCount,
        const WorkspaceLimits& limits) const noexcept
    {
        state.zoom = clamp_zoom(state.zoom, limits);
        state.palette_scroll = nonnegative(state.palette_scroll);
        state.layer_scroll = nonnegative(state.layer_scroll);
        if (state.selected_palette >= paletteCount)
            state.selected_palette = invalid_index;
        if (state.selected_layer >= layerCount)
            state.selected_layer = invalid_index;
        if (!finite(state.pan.x))
            state.pan.x = 0.0f;
        if (!finite(state.pan.y))
            state.pan.y = 0.0f;
    }

    WorkspaceLayout TileWorkspaceController::make_layout(
        const WorkspaceLayoutOptions& options) const noexcept
    {
        WorkspaceLayout result{};
        result.area = options.area;
        if (!valid_rect(options.area) || !options.limits.valid())
            return result;

        const float gap = nonnegative(options.gap);
        const float padding = nonnegative(options.padding);
        const float toolbarHeight = positive_or(options.toolbar_height, 1.0f);
        const float statusHeight = positive_or(options.status_height, 1.0f);
        const float innerWidth = options.area.size.x - padding * 2.0f;
        const float innerHeight = options.area.size.y - padding * 2.0f;
        const float contentHeight = innerHeight - toolbarHeight - statusHeight - gap * 2.0f;
        if (innerWidth <= 0.0f || contentHeight < options.limits.minimum_canvas_height)
            return result;

        const float originX = options.area.position.x + padding;
        const float originY = options.area.position.y + padding;
        result.toolbar = {{originX, originY}, {innerWidth, toolbarHeight}};
        result.status = {
            {originX, originY + toolbarHeight + gap + contentHeight + gap},
            {innerWidth, statusHeight}
        };

        float sidebarWidth = std::clamp(
            positive_or(options.sidebar_width, options.limits.minimum_sidebar_width),
            options.limits.minimum_sidebar_width,
            options.limits.maximum_sidebar_width);
        float inspectorWidth = std::clamp(
            positive_or(options.inspector_width, options.limits.minimum_inspector_width),
            options.limits.minimum_inspector_width,
            options.limits.maximum_inspector_width);
        const bool inspectorFits = options.show_inspector
            && innerWidth >= sidebarWidth + inspectorWidth
                + options.limits.minimum_canvas_width + gap * 2.0f;
        result.inspector_visible = inspectorFits;
        result.compact = !inspectorFits;

        const float reservedInspector = inspectorFits ? inspectorWidth + gap : 0.0f;
        const float maximumSidebar = innerWidth - reservedInspector
            - options.limits.minimum_canvas_width - gap;
        sidebarWidth = (std::min)(sidebarWidth, maximumSidebar);
        if (sidebarWidth < options.limits.minimum_sidebar_width)
            return result;

        const float canvasWidth = innerWidth - sidebarWidth - gap - reservedInspector;
        if (canvasWidth < options.limits.minimum_canvas_width)
            return result;

        const float contentY = originY + toolbarHeight + gap;
        const float paletteFraction = finite(options.palette_fraction)
            ? std::clamp(options.palette_fraction, 0.25f, 0.75f)
            : 0.58f;
        const float paletteHeight = (contentHeight - gap) * paletteFraction;
        const float layerHeight = contentHeight - gap - paletteHeight;
        result.palette = {{originX, contentY}, {sidebarWidth, paletteHeight}};
        result.layers = {
            {originX, contentY + paletteHeight + gap},
            {sidebarWidth, layerHeight}
        };
        result.canvas = {
            {originX + sidebarWidth + gap, contentY},
            {canvasWidth, contentHeight}
        };
        if (inspectorFits)
        {
            result.inspector = {
                {result.canvas.position.x + canvasWidth + gap, contentY},
                {inspectorWidth, contentHeight}
            };
        }
        result.valid = valid_rect(result.toolbar) && valid_rect(result.palette)
            && valid_rect(result.layers) && valid_rect(result.canvas)
            && valid_rect(result.status)
            && (!result.inspector_visible || valid_rect(result.inspector));
        return result;
    }

    PaletteGridLayout TileWorkspaceController::make_palette_grid(
        const PaletteGridOptions& options) const noexcept
    {
        PaletteGridLayout result{};
        result.viewport = options.viewport;
        result.item_count = options.item_count;
        if (!valid_rect(options.viewport) || options.maximum_columns == 0u)
            return result;

        result.gap = nonnegative(options.gap);
        result.padding = nonnegative(options.padding);
        const float minimumCell = positive_or(options.minimum_cell_size, 1.0f);
        const float maximumCell = (std::max)(
            minimumCell,
            positive_or(options.maximum_cell_size, minimumCell));
        result.cell_size = std::clamp(
            positive_or(options.preferred_cell_size, minimumCell),
            minimumCell,
            maximumCell);
        const float availableWidth = options.viewport.size.x - result.padding * 2.0f;
        if (availableWidth < minimumCell)
            return result;

        const float stride = result.cell_size + result.gap;
        const float rawColumns = std::floor((availableWidth + result.gap) / stride);
        const auto fittedColumns = rawColumns >= 1.0f
            ? static_cast<std::uint32_t>((std::min)(
                rawColumns,
                static_cast<float>(std::numeric_limits<std::uint32_t>::max())))
            : 1u;
        result.columns = (std::max)(
            1u,
            (std::min)(fittedColumns, options.maximum_columns));
        result.rows = ceil_div(result.item_count, result.columns);
        result.content_height = result.padding * 2.0f;
        if (result.rows != 0u)
        {
            result.content_height += static_cast<float>(result.rows) * result.cell_size
                + static_cast<float>(result.rows - 1u) * result.gap;
        }
        result.maximum_scroll_y = (std::max)(
            0.0f,
            result.content_height - options.viewport.size.y);
        result.scroll_y = std::clamp(
            nonnegative(options.scroll_y),
            0.0f,
            result.maximum_scroll_y);

        const float firstRow = std::floor(
            (result.scroll_y - result.padding) / stride);
        const float lastRow = std::ceil(
            (result.scroll_y + options.viewport.size.y - result.padding) / stride);
        result.first_visible_row = firstRow > 0.0f
            ? (std::min)(static_cast<std::uint32_t>(firstRow), result.rows)
            : 0u;
        result.past_last_visible_row = lastRow > 0.0f
            ? (std::min)(static_cast<std::uint32_t>(lastRow), result.rows)
            : 0u;
        result.valid = true;
        return result;
    }

    PaletteCellLayout TileWorkspaceController::make_palette_cell(
        const PaletteGridLayout& layout,
        std::uint32_t index,
        Vec2 pointer,
        bool selected) const noexcept
    {
        PaletteCellLayout result{};
        result.index = index;
        if (!layout.valid || layout.columns == 0u || index >= layout.item_count)
        {
            result.index = invalid_index;
            return result;
        }

        const std::uint32_t row = index / layout.columns;
        const std::uint32_t column = index % layout.columns;
        const float stride = layout.cell_size + layout.gap;
        result.bounds = {
            {
                layout.viewport.position.x + layout.padding
                    + static_cast<float>(column) * stride,
                layout.viewport.position.y + layout.padding
                    + static_cast<float>(row) * stride - layout.scroll_y
            },
            {layout.cell_size, layout.cell_size}
        };
        const float contentInset = (std::min)(4.0f, layout.cell_size * 0.1f);
        result.content = {
            {result.bounds.position.x + contentInset, result.bounds.position.y + contentInset},
            {
                (std::max)(0.0f, result.bounds.size.x - contentInset * 2.0f),
                (std::max)(0.0f, result.bounds.size.y - contentInset * 2.0f)
            }
        };
        const float bottom = result.bounds.position.y + result.bounds.size.y;
        const float right = result.bounds.position.x + result.bounds.size.x;
        result.visible = right > layout.viewport.position.x
            && bottom > layout.viewport.position.y
            && result.bounds.position.x
                < layout.viewport.position.x + layout.viewport.size.x
            && result.bounds.position.y
                < layout.viewport.position.y + layout.viewport.size.y;
        result.hovered = result.visible && contains(layout.viewport, pointer)
            && contains(result.bounds, pointer);
        result.selected = selected;
        return result;
    }

    std::uint32_t TileWorkspaceController::palette_index_at(
        const PaletteGridLayout& layout,
        Vec2 point) const noexcept
    {
        if (!layout.valid || layout.columns == 0u || !contains(layout.viewport, point))
            return invalid_index;
        const float localX = point.x - layout.viewport.position.x - layout.padding;
        const float localY = point.y - layout.viewport.position.y
            - layout.padding + layout.scroll_y;
        if (localX < 0.0f || localY < 0.0f)
            return invalid_index;

        const float stride = layout.cell_size + layout.gap;
        const auto column = static_cast<std::uint32_t>(std::floor(localX / stride));
        const auto row = static_cast<std::uint32_t>(std::floor(localY / stride));
        if (column >= layout.columns || row >= layout.rows)
            return invalid_index;
        const float cellX = localX - static_cast<float>(column) * stride;
        const float cellY = localY - static_cast<float>(row) * stride;
        if (cellX > layout.cell_size || cellY > layout.cell_size)
            return invalid_index;
        const std::uint64_t index = static_cast<std::uint64_t>(row)
            * layout.columns + column;
        return index < layout.item_count
            ? static_cast<std::uint32_t>(index)
            : invalid_index;
    }

    TileCanvasLayout TileWorkspaceController::make_canvas(
        const TileCanvasOptions& options) const noexcept
    {
        TileCanvasLayout result{};
        result.viewport = options.viewport;
        result.map_width = options.map_width;
        result.map_height = options.map_height;
        if (!valid_rect(options.viewport) || options.map_width == 0u
            || options.map_height == 0u || !options.limits.valid())
        {
            return result;
        }

        result.zoom = clamp_zoom(options.zoom, options.limits);
        const float baseCell = positive_or(options.base_cell_pixels, 1.0f);
        result.cell_pixels = baseCell * result.zoom;
        if (!finite(result.cell_pixels) || result.cell_pixels <= 0.0f)
            return result;
        result.pan = normalized_canvas_pan(options, options.pan);
        const Vec2 centered = map_center_origin(options, result.cell_pixels);
        result.origin = {centered.x + result.pan.x, centered.y + result.pan.y};
        result.map_bounds = {
            result.origin,
            {
                static_cast<float>(options.map_width) * result.cell_pixels,
                static_cast<float>(options.map_height) * result.cell_pixels
            }
        };
        result.valid = valid_rect(result.map_bounds);
        result.visible_cells = visible_cells(result);
        return result;
    }

    CellCoordinate TileWorkspaceController::cell_at(
        const TileCanvasLayout& layout,
        Vec2 point) const noexcept
    {
        if (!layout.valid || !contains(layout.viewport, point)
            || !contains(layout.map_bounds, point))
        {
            return {invalid_index, invalid_index};
        }
        const float localX = (point.x - layout.origin.x) / layout.cell_pixels;
        const float localY = (point.y - layout.origin.y) / layout.cell_pixels;
        if (!finite(localX) || !finite(localY) || localX < 0.0f || localY < 0.0f)
            return {invalid_index, invalid_index};
        const auto x = static_cast<std::uint32_t>(std::floor(localX));
        const auto y = static_cast<std::uint32_t>(std::floor(localY));
        return x < layout.map_width && y < layout.map_height
            ? CellCoordinate{x, y}
            : CellCoordinate{invalid_index, invalid_index};
    }

    Rect TileWorkspaceController::cell_rect(
        const TileCanvasLayout& layout,
        CellCoordinate cell,
        float inset) const noexcept
    {
        if (!layout.valid || cell.x >= layout.map_width || cell.y >= layout.map_height)
            return {};
        const float safeInset = std::clamp(
            nonnegative(inset),
            0.0f,
            layout.cell_pixels * 0.5f);
        return {
            {
                layout.origin.x + static_cast<float>(cell.x) * layout.cell_pixels
                    + safeInset,
                layout.origin.y + static_cast<float>(cell.y) * layout.cell_pixels
                    + safeInset
            },
            {
                (std::max)(0.0f, layout.cell_pixels - safeInset * 2.0f),
                (std::max)(0.0f, layout.cell_pixels - safeInset * 2.0f)
            }
        };
    }

    CanvasView TileWorkspaceController::zoom_at(
        const TileCanvasLayout& layout,
        Vec2 anchor,
        float wheelSteps,
        const WorkspaceLimits& limits) const noexcept
    {
        CanvasView result{layout.zoom, layout.pan};
        if (!layout.valid || !limits.valid() || !finite(wheelSteps)
            || wheelSteps == 0.0f || !contains(layout.viewport, anchor))
        {
            return result;
        }

        const float localX = (anchor.x - layout.origin.x) / layout.cell_pixels;
        const float localY = (anchor.y - layout.origin.y) / layout.cell_pixels;
        const float requestedZoom = layout.zoom * std::pow(1.2f, wheelSteps);
        result.zoom = clamp_zoom(requestedZoom, limits);
        const float baseCell = layout.cell_pixels / layout.zoom;
        const float newCell = baseCell * result.zoom;
        TileCanvasOptions options{
            .viewport = layout.viewport,
            .map_width = layout.map_width,
            .map_height = layout.map_height,
            .base_cell_pixels = baseCell,
            .zoom = result.zoom,
            .pan = {},
            .overscroll_cells = 1.0f,
            .limits = limits
        };
        const Vec2 centered = map_center_origin(options, newCell);
        result.pan = {
            anchor.x - localX * newCell - centered.x,
            anchor.y - localY * newCell - centered.y
        };
        result.pan = normalized_canvas_pan(options, result.pan);
        return result;
    }

    Vec2 TileWorkspaceController::normalize_pan(
        const TileCanvasOptions& options,
        Vec2 pan) const noexcept
    {
        return normalized_canvas_pan(options, pan);
    }

    const TileWorkspaceController& controller() noexcept
    {
        static const TileWorkspaceController value{};
        return value;
    }

    ContractFailure run_contract() noexcept
    {
        const auto& workspace = controller();
        WorkspaceState state{
            .selected_palette = 99u,
            .selected_layer = 42u,
            .palette_scroll = -5.0f,
            .layer_scroll = std::numeric_limits<float>::quiet_NaN(),
            .zoom = std::numeric_limits<float>::infinity(),
            .pan = {
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity()
            }
        };
        workspace.normalize_state(state, 4u, 2u);
        if (state.selected_palette != invalid_index
            || state.selected_layer != invalid_index
            || state.palette_scroll != 0.0f || state.layer_scroll != 0.0f
            || state.zoom != 1.0f || state.pan.x != 0.0f || state.pan.y != 0.0f)
        {
            return ContractFailure::state_normalization;
        }

        const WorkspaceLayout wide = workspace.make_layout({
            .area = {{0.0f, 0.0f}, {1'200.0f, 720.0f}}
        });
        if (!wide.valid || !wide.inspector_visible || wide.compact
            || wide.canvas.size.x < 160.0f || wide.canvas.size.y < 120.0f)
        {
            return ContractFailure::wide_layout;
        }
        const WorkspaceLayout compact = workspace.make_layout({
            .area = {{0.0f, 0.0f}, {460.0f, 360.0f}}
        });
        if (!compact.valid || compact.inspector_visible || !compact.compact)
            return ContractFailure::compact_layout;

        const PaletteGridLayout palette = workspace.make_palette_grid({
            .viewport = {{10.0f, 20.0f}, {180.0f, 160.0f}},
            .item_count = 10u,
            .preferred_cell_size = 48.0f,
            .minimum_cell_size = 32.0f,
            .maximum_cell_size = 64.0f,
            .gap = 6.0f,
            .padding = 6.0f
        });
        if (!palette.valid || palette.columns != 3u || palette.rows != 4u
            || palette.content_height <= palette.viewport.size.y)
        {
            return ContractFailure::palette_layout;
        }
        const PaletteCellLayout paletteCell = workspace.make_palette_cell(
            palette,
            4u,
            {palette.viewport.position.x + 6.0f + 48.0f + 6.0f + 24.0f,
             palette.viewport.position.y + 6.0f + 48.0f + 6.0f + 24.0f},
            true);
        if (!paletteCell.visible || !paletteCell.hovered || !paletteCell.selected
            || workspace.palette_index_at(
                palette,
                {paletteCell.bounds.position.x + 2.0f,
                 paletteCell.bounds.position.y + 2.0f}) != 4u)
        {
            return ContractFailure::palette_hit_test;
        }
        const PaletteGridLayout scrolled = workspace.make_palette_grid({
            .viewport = {{0.0f, 0.0f}, {180.0f, 100.0f}},
            .item_count = 100u,
            .preferred_cell_size = 48.0f,
            .scroll_y = 10'000.0f
        });
        if (!scrolled.valid || scrolled.scroll_y != scrolled.maximum_scroll_y
            || scrolled.first_visible_row == 0u)
        {
            return ContractFailure::palette_scroll;
        }

        const TileCanvasLayout canvas = workspace.make_canvas({
            .viewport = {{100.0f, 50.0f}, {640.0f, 480.0f}},
            .map_width = 20u,
            .map_height = 10u,
            .base_cell_pixels = 20.0f
        });
        if (!canvas.valid || !nearly_equal(canvas.cell_pixels, 20.0f)
            || canvas.map_bounds.size.x != 400.0f
            || canvas.map_bounds.size.y != 200.0f)
        {
            return ContractFailure::canvas_layout;
        }
        const Vec2 targetPoint{
            canvas.origin.x + 3.5f * canvas.cell_pixels,
            canvas.origin.y + 4.5f * canvas.cell_pixels
        };
        if (workspace.cell_at(canvas, targetPoint) != CellCoordinate{3u, 4u})
            return ContractFailure::canvas_hit_test;
        if (canvas.visible_cells.empty || canvas.visible_cells.first != CellCoordinate{0u, 0u}
            || canvas.visible_cells.past_last != CellCoordinate{20u, 10u})
        {
            return ContractFailure::canvas_visible_range;
        }

        const float beforeX = (targetPoint.x - canvas.origin.x) / canvas.cell_pixels;
        const float beforeY = (targetPoint.y - canvas.origin.y) / canvas.cell_pixels;
        const CanvasView zoomed = workspace.zoom_at(canvas, targetPoint, 2.0f);
        const TileCanvasLayout zoomedCanvas = workspace.make_canvas({
            .viewport = canvas.viewport,
            .map_width = canvas.map_width,
            .map_height = canvas.map_height,
            .base_cell_pixels = 20.0f,
            .zoom = zoomed.zoom,
            .pan = zoomed.pan
        });
        const float afterX = (targetPoint.x - zoomedCanvas.origin.x)
            / zoomedCanvas.cell_pixels;
        const float afterY = (targetPoint.y - zoomedCanvas.origin.y)
            / zoomedCanvas.cell_pixels;
        if (!zoomedCanvas.valid || zoomed.zoom <= canvas.zoom
            || !nearly_equal(beforeX, afterX) || !nearly_equal(beforeY, afterY))
        {
            return ContractFailure::anchored_zoom;
        }

        const TileCanvasOptions panOptions{
            .viewport = {{0.0f, 0.0f}, {320.0f, 240.0f}},
            .map_width = 100u,
            .map_height = 100u,
            .base_cell_pixels = 16.0f
        };
        const Vec2 normalized = workspace.normalize_pan(
            panOptions,
            {1'000'000.0f, -1'000'000.0f});
        if (!finite(normalized.x) || !finite(normalized.y)
            || normalized.x == 1'000'000.0f || normalized.y == -1'000'000.0f)
        {
            return ContractFailure::pan_normalization;
        }

        if (workspace.make_layout({}).valid
            || workspace.make_palette_grid({}).valid
            || workspace.make_canvas({}).valid
            || workspace.cell_at({}, {}) != CellCoordinate{invalid_index, invalid_index})
        {
            return ContractFailure::invalid_input;
        }
        return ContractFailure::none;
    }
}
