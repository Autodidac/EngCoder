#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace epochengine::gui_lib::font
{
    inline constexpr std::uint32_t glyph_width = 5U;
    inline constexpr std::uint32_t glyph_height = 7U;
    inline constexpr std::uint32_t glyph_advance = 6U;
    inline constexpr std::uint32_t line_advance = 9U;
    inline constexpr float default_logical_height = 16.0F;
    inline constexpr float minimum_readable_logical_height = 12.0F;

    struct FontSize final
    {
        // Logical UI pixels. dpi_scale converts these to framebuffer pixels.
        float logical_height{ default_logical_height };
        float dpi_scale{ 1.0F };
    };

    struct BitmapFontMetrics final
    {
        float pixel_height{};
        float cell_size{};
        float glyph_width{};
        float glyph_height{};
        float advance{};
        float line_advance{};
    };

    struct TextExtent final
    {
        float width{};
        float height{};
    };

    struct BitmapGlyph final
    {
        std::array<std::uint8_t, glyph_height> rows{};
    };

    [[nodiscard]] constexpr float resolved_pixel_height(FontSize size) noexcept
    {
        const float logical_height = size.logical_height > 0.0F
            ? size.logical_height
            : default_logical_height;
        const float dpi_scale = size.dpi_scale > 0.0F ? size.dpi_scale : 1.0F;
        return logical_height * dpi_scale;
    }

    [[nodiscard]] constexpr BitmapFontMetrics make_bitmap_font_metrics(
        FontSize size = {},
        float letter_spacing = 0.0F,
        float line_spacing = 0.0F) noexcept
    {
        const float pixel_height = resolved_pixel_height(size);
        const float cell_size = pixel_height / static_cast<float>(glyph_height);
        const float safe_letter_spacing = letter_spacing > 0.0F ? letter_spacing : 0.0F;
        const float safe_line_spacing = line_spacing > 0.0F ? line_spacing : 0.0F;
        return {
            .pixel_height = pixel_height,
            .cell_size = cell_size,
            .glyph_width = cell_size * static_cast<float>(glyph_width),
            .glyph_height = pixel_height,
            .advance = cell_size * static_cast<float>(glyph_advance) + safe_letter_spacing,
            .line_advance = cell_size * static_cast<float>(line_advance) + safe_line_spacing
        };
    }

    [[nodiscard]] constexpr BitmapGlyph default_glyph(char character) noexcept
    {
        char c = character;
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');

        switch (c)
        {
        case 'A': return { { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } };
        case 'B': return { { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e } };
        case 'C': return { { 0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f } };
        case 'D': return { { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e } };
        case 'E': return { { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f } };
        case 'F': return { { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 } };
        case 'G': return { { 0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f } };
        case 'H': return { { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } };
        case 'I': return { { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f } };
        case 'J': return { { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c } };
        case 'K': return { { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } };
        case 'L': return { { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f } };
        case 'M': return { { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 } };
        case 'N': return { { 0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 } };
        case 'O': return { { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } };
        case 'P': return { { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 } };
        case 'Q': return { { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d } };
        case 'R': return { { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 } };
        case 'S': return { { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e } };
        case 'T': return { { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } };
        case 'U': return { { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } };
        case 'V': return { { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 } };
        case 'W': return { { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a } };
        case 'X': return { { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 } };
        case 'Y': return { { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 } };
        case 'Z': return { { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f } };
        case '0': return { { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e } };
        case '1': return { { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e } };
        case '2': return { { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f } };
        case '3': return { { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e } };
        case '4': return { { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 } };
        case '5': return { { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e } };
        case '6': return { { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e } };
        case '7': return { { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } };
        case '8': return { { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e } };
        case '9': return { { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e } };
        case '-': return { { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 } };
        case '.': return { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 } };
        case ':': return { { 0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00 } };
        case '/': return { { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } };
        case '\\': return { { 0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01 } };
        case '_': return { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f } };
        case '+': return { { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 } };
        case '!': return { { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } };
        case '?': return { { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } };
        case ',': return { { 0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08 } };
        case ';': return { { 0x00, 0x06, 0x06, 0x00, 0x06, 0x04, 0x08 } };
        case '=': return { { 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00 } };
        case '(': return { { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } };
        case ')': return { { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } };
        case '[': return { { 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e } };
        case ']': return { { 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e } };
        case ' ': return {};
        default: return { { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } };
        }
    }

    [[nodiscard]] constexpr bool pixel_on(
        const BitmapGlyph& glyph,
        std::uint32_t column,
        std::uint32_t row) noexcept
    {
        if (column >= glyph_width || row >= glyph_height)
            return false;
        const std::uint8_t mask = static_cast<std::uint8_t>(
            1U << (glyph_width - 1U - column));
        return (glyph.rows[row] & mask) != 0U;
    }

    [[nodiscard]] constexpr TextExtent measure_text(
        std::string_view text,
        FontSize size = {},
        float letter_spacing = 0.0F,
        float line_spacing = 0.0F) noexcept
    {
        if (text.empty())
            return {};

        const BitmapFontMetrics metrics = make_bitmap_font_metrics(
            size,
            letter_spacing,
            line_spacing);
        std::size_t line_length{};
        std::size_t maximum_line_length{};
        std::size_t line_count{ 1U };
        for (const char character : text)
        {
            if (character == '\n')
            {
                maximum_line_length = maximum_line_length < line_length
                    ? line_length
                    : maximum_line_length;
                line_length = 0U;
                ++line_count;
            }
            else
            {
                ++line_length;
            }
        }
        maximum_line_length = maximum_line_length < line_length
            ? line_length
            : maximum_line_length;

        const float width = maximum_line_length == 0U
            ? 0.0F
            : static_cast<float>(maximum_line_length - 1U) * metrics.advance
                + metrics.glyph_width;
        const float height = metrics.glyph_height
            + static_cast<float>(line_count - 1U) * metrics.line_advance;
        return { width, height };
    }

}
