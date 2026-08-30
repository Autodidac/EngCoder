module;

#include <gui/font.hpp>
#include <string_view>

export module epoch.gui.font;

export import epoch.gui;

export namespace epochengine::gui_lib::font
{
    using ::epochengine::gui_lib::font::BitmapFontMetrics;
    using ::epochengine::gui_lib::font::BitmapGlyph;
    using ::epochengine::gui_lib::font::FontSize;
    using ::epochengine::gui_lib::font::TextExtent;

    using ::epochengine::gui_lib::font::default_glyph;
    using ::epochengine::gui_lib::font::make_bitmap_font_metrics;
    using ::epochengine::gui_lib::font::measure_text;
    using ::epochengine::gui_lib::font::pixel_on;
    using ::epochengine::gui_lib::font::resolved_pixel_height;

    using ::epochengine::gui_lib::font::default_logical_height;
    using ::epochengine::gui_lib::font::glyph_advance;
    using ::epochengine::gui_lib::font::glyph_height;
    using ::epochengine::gui_lib::font::glyph_width;
    using ::epochengine::gui_lib::font::line_advance;
    using ::epochengine::gui_lib::font::minimum_readable_logical_height;

    [[nodiscard]] constexpr Vec2 measure_text_pixels(
        std::string_view text,
        FontSize size = {},
        float letter_spacing = 0.0F,
        float line_spacing = 0.0F) noexcept
    {
        const TextExtent extent = ::epochengine::gui_lib::font::measure_text(
            text,
            size,
            letter_spacing,
            line_spacing);
        return { extent.width, extent.height };
    }

}
