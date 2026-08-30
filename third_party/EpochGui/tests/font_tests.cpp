import epoch.gui.font;

namespace font = epochengine::gui_lib::font;

int main()
{
    constexpr font::BitmapGlyph glyph = font::default_glyph('a');
    static_assert(glyph.rows[0] == 0x0e);
    static_assert(font::pixel_on(glyph, 1, 0));
    static_assert(!font::pixel_on(glyph, 0, 0));

    constexpr font::FontSize size{ .logical_height = 14.0F, .dpi_scale = 1.5F };
    constexpr font::BitmapFontMetrics metrics = font::make_bitmap_font_metrics(
        size,
        1.0F,
        2.0F);
    static_assert(metrics.pixel_height == 21.0F);
    static_assert(metrics.cell_size == 3.0F);
    static_assert(metrics.advance == 19.0F);
    static_assert(metrics.line_advance == 29.0F);

    constexpr epochengine::gui_lib::Vec2 measured = font::measure_text_pixels(
        "AB\nC",
        size,
        1.0F,
        2.0F);
    static_assert(measured.x == 34.0F);
    static_assert(measured.y == 50.0F);

    constexpr font::BitmapGlyph fallback = font::default_glyph('\x01');
    static_assert(font::pixel_on(fallback, 1, 0));
    return 0;
}
