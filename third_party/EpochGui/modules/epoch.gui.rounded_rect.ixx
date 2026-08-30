module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module epoch.gui.rounded_rect;

export import epoch.gui;

export namespace epochengine::gui_lib::rounded_rect
{
    struct RoundedRectStyle
    {
        bool enabled{};
        float control_radius{ 6.0f };
        std::uint32_t segments_per_corner{ 4 };
    };

    struct CornerRadii
    {
        float top_left{};
        float top_right{};
        float bottom_right{};
        float bottom_left{};
    };

    struct RoundedRectOptions
    {
        Rect bounds{};
        CornerRadii radii{ 12.0f, 12.0f, 12.0f, 12.0f };
        float border_width{};
        std::uint32_t segments_per_corner{ 8 };
    };

    struct RoundedRectMesh
    {
        std::vector<Vec2> vertices{};
        std::vector<std::uint32_t> fill_indices{};
        std::vector<std::uint32_t> border_indices{};
        Rect bounds{};
        CornerRadii radii{};
        float border_width{};
        std::uint32_t outer_first{};
        std::uint32_t outer_count{};
        std::uint32_t inner_first{};
        std::uint32_t inner_count{};
        bool valid{};

        [[nodiscard]] std::span<const Vec2> outer_contour() const noexcept
        {
            const std::size_t first = outer_first;
            const std::size_t count = outer_count;
            if (count == 0 || first > vertices.size() || count > vertices.size() - first)
                return {};
            return { vertices.data() + first, count };
        }

        [[nodiscard]] std::span<const Vec2> inner_contour() const noexcept
        {
            const std::size_t first = inner_first;
            const std::size_t count = inner_count;
            if (count == 0 || first > vertices.size() || count > vertices.size() - first)
                return {};
            return { vertices.data() + first, count };
        }
    };

    [[nodiscard]] RoundedRectStyle normalize_rounded_rect_style(RoundedRectStyle style) noexcept;
    [[nodiscard]] RoundedRectOptions make_styled_rounded_rect_options(
        Rect bounds,
        const RoundedRectStyle& style,
        float border_width = 0.0f) noexcept;
    [[nodiscard]] CornerRadii normalize_corner_radii(Rect bounds, CornerRadii radii) noexcept;
    [[nodiscard]] RoundedRectMesh make_rounded_rect_mesh(const RoundedRectOptions& options);
}
