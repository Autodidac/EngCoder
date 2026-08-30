module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

module epoch.gui.rounded_rect;

namespace epochengine::gui_lib::rounded_rect
{
    namespace
    {
        constexpr float epsilon = 0.0001f;
        constexpr std::uint32_t minimum_segments = 1;
        constexpr std::uint32_t maximum_segments = 64;

        [[nodiscard]] float finite_or(float value, float fallback) noexcept
        {
            return std::isfinite(value) ? value : fallback;
        }

        [[nodiscard]] float finite_nonnegative(float value) noexcept
        {
            return (std::max)(0.0f, finite_or(value, 0.0f));
        }

        [[nodiscard]] Rect sanitize_bounds(Rect bounds) noexcept
        {
            bounds.position.x = finite_or(bounds.position.x, 0.0f);
            bounds.position.y = finite_or(bounds.position.y, 0.0f);
            bounds.size.x = finite_nonnegative(bounds.size.x);
            bounds.size.y = finite_nonnegative(bounds.size.y);
            return bounds;
        }

        [[nodiscard]] CornerRadii sanitize_radii(CornerRadii radii) noexcept
        {
            radii.top_left = finite_nonnegative(radii.top_left);
            radii.top_right = finite_nonnegative(radii.top_right);
            radii.bottom_right = finite_nonnegative(radii.bottom_right);
            radii.bottom_left = finite_nonnegative(radii.bottom_left);
            return radii;
        }

        void append_arc(
            std::vector<Vec2>& vertices,
            Vec2 center,
            float radius,
            float start_angle,
            float end_angle,
            std::uint32_t segments)
        {
            for (std::uint32_t step = 0; step <= segments; ++step)
            {
                const float t = static_cast<float>(step) / static_cast<float>(segments);
                const float angle = start_angle + (end_angle - start_angle) * t;
                vertices.push_back(Vec2{
                    center.x + std::cos(angle) * radius,
                    center.y + std::sin(angle) * radius
                });
            }
        }

        void append_contour(
            std::vector<Vec2>& vertices,
            Rect bounds,
            CornerRadii radii,
            std::uint32_t segments)
        {
            constexpr float pi = std::numbers::pi_v<float>;
            const float left = bounds.position.x;
            const float top = bounds.position.y;
            const float right = left + bounds.size.x;
            const float bottom = top + bounds.size.y;

            append_arc(vertices,
                Vec2{ left + radii.top_left, top + radii.top_left },
                radii.top_left,
                pi,
                pi * 1.5f,
                segments);
            append_arc(vertices,
                Vec2{ right - radii.top_right, top + radii.top_right },
                radii.top_right,
                pi * 1.5f,
                pi * 2.0f,
                segments);
            append_arc(vertices,
                Vec2{ right - radii.bottom_right, bottom - radii.bottom_right },
                radii.bottom_right,
                0.0f,
                pi * 0.5f,
                segments);
            append_arc(vertices,
                Vec2{ left + radii.bottom_left, bottom - radii.bottom_left },
                radii.bottom_left,
                pi * 0.5f,
                pi,
                segments);
        }

        [[nodiscard]] CornerRadii inset_radii(CornerRadii radii, float inset) noexcept
        {
            radii.top_left = (std::max)(0.0f, radii.top_left - inset);
            radii.top_right = (std::max)(0.0f, radii.top_right - inset);
            radii.bottom_right = (std::max)(0.0f, radii.bottom_right - inset);
            radii.bottom_left = (std::max)(0.0f, radii.bottom_left - inset);
            return radii;
        }
    }

    RoundedRectStyle normalize_rounded_rect_style(RoundedRectStyle style) noexcept
    {
        style.control_radius = finite_nonnegative(style.control_radius);
        style.segments_per_corner = std::clamp(
            style.segments_per_corner,
            minimum_segments,
            maximum_segments);
        return style;
    }

    RoundedRectOptions make_styled_rounded_rect_options(
        Rect bounds,
        const RoundedRectStyle& style,
        float border_width) noexcept
    {
        const RoundedRectStyle normalized = normalize_rounded_rect_style(style);
        const float radius = normalized.enabled ? normalized.control_radius : 0.0f;
        return RoundedRectOptions{
            .bounds = bounds,
            .radii = { radius, radius, radius, radius },
            .border_width = finite_nonnegative(border_width),
            .segments_per_corner = normalized.segments_per_corner
        };
    }

    CornerRadii normalize_corner_radii(Rect bounds, CornerRadii radii) noexcept
    {
        bounds = sanitize_bounds(bounds);
        radii = sanitize_radii(radii);

        if (bounds.size.x <= epsilon || bounds.size.y <= epsilon)
            return {};

        float scale = 1.0f;
        const auto constrain = [&scale](float radius_sum, float extent) noexcept
        {
            if (radius_sum > extent && radius_sum > epsilon)
                scale = (std::min)(scale, extent / radius_sum);
        };

        constrain(radii.top_left + radii.top_right, bounds.size.x);
        constrain(radii.bottom_left + radii.bottom_right, bounds.size.x);
        constrain(radii.top_left + radii.bottom_left, bounds.size.y);
        constrain(radii.top_right + radii.bottom_right, bounds.size.y);

        scale = std::clamp(scale, 0.0f, 1.0f);
        radii.top_left *= scale;
        radii.top_right *= scale;
        radii.bottom_right *= scale;
        radii.bottom_left *= scale;
        return radii;
    }

    RoundedRectMesh make_rounded_rect_mesh(const RoundedRectOptions& options)
    {
        RoundedRectMesh mesh{};
        mesh.bounds = sanitize_bounds(options.bounds);
        if (mesh.bounds.size.x <= epsilon || mesh.bounds.size.y <= epsilon)
            return mesh;

        mesh.radii = normalize_corner_radii(mesh.bounds, options.radii);
        const std::uint32_t segments = std::clamp(
            options.segments_per_corner,
            minimum_segments,
            maximum_segments);
        const std::uint32_t contour_count = 4U * (segments + 1U);

        const float maximum_border = 0.5f * (std::min)(mesh.bounds.size.x, mesh.bounds.size.y);
        mesh.border_width = std::clamp(
            finite_nonnegative(options.border_width),
            0.0f,
            maximum_border);

        mesh.vertices.reserve(1U + contour_count * 2U);
        mesh.fill_indices.reserve(static_cast<std::size_t>(contour_count) * 3U);
        if (mesh.border_width > epsilon)
            mesh.border_indices.reserve(static_cast<std::size_t>(contour_count) * 6U);

        mesh.vertices.push_back(Vec2{
            mesh.bounds.position.x + mesh.bounds.size.x * 0.5f,
            mesh.bounds.position.y + mesh.bounds.size.y * 0.5f
        });

        mesh.outer_first = 1U;
        append_contour(mesh.vertices, mesh.bounds, mesh.radii, segments);
        mesh.outer_count = contour_count;

        for (std::uint32_t index = 0; index < contour_count; ++index)
        {
            const std::uint32_t current = mesh.outer_first + index;
            const std::uint32_t next = mesh.outer_first + ((index + 1U) % contour_count);
            mesh.fill_indices.insert(mesh.fill_indices.end(), { 0U, current, next });
        }

        if (mesh.border_width > epsilon)
        {
            Rect inner_bounds{
                Vec2{
                    mesh.bounds.position.x + mesh.border_width,
                    mesh.bounds.position.y + mesh.border_width
                },
                Vec2{
                    (std::max)(0.0f, mesh.bounds.size.x - mesh.border_width * 2.0f),
                    (std::max)(0.0f, mesh.bounds.size.y - mesh.border_width * 2.0f)
                }
            };

            if (inner_bounds.size.x <= epsilon || inner_bounds.size.y <= epsilon)
            {
                mesh.border_indices = mesh.fill_indices;
            }
            else
            {
                mesh.inner_first = static_cast<std::uint32_t>(mesh.vertices.size());
                const CornerRadii inner_radii = normalize_corner_radii(
                    inner_bounds,
                    inset_radii(mesh.radii, mesh.border_width));
                append_contour(mesh.vertices, inner_bounds, inner_radii, segments);
                mesh.inner_count = contour_count;

                for (std::uint32_t index = 0; index < contour_count; ++index)
                {
                    const std::uint32_t next_index = (index + 1U) % contour_count;
                    const std::uint32_t outer_current = mesh.outer_first + index;
                    const std::uint32_t outer_next = mesh.outer_first + next_index;
                    const std::uint32_t inner_current = mesh.inner_first + index;
                    const std::uint32_t inner_next = mesh.inner_first + next_index;

                    mesh.border_indices.insert(mesh.border_indices.end(), {
                        outer_current, outer_next, inner_next,
                        outer_current, inner_next, inner_current
                    });
                }
            }
        }

        mesh.valid = true;
        return mesh;
    }
}
