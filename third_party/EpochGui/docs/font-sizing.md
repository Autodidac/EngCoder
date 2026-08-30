# Font sizing contract

EpochGui's built-in 5x7 font is a renderer-neutral fallback. The 5x7 dimensions describe the bitmap topology, not the requested on-screen font size.

New code should express text size through `epochengine::gui_lib::font::FontSize`:

```cpp
#include <gui/font.hpp>

namespace font = epochengine::gui_lib::font;

constexpr font::FontSize body{
    .logical_height = 16.0F,
    .dpi_scale = 1.5F
};

constexpr font::BitmapFontMetrics metrics =
    font::make_bitmap_font_metrics(body);

static_assert(metrics.pixel_height == 24.0F);
```

`logical_height` is the intended glyph height in logical UI pixels. `dpi_scale` converts logical pixels to framebuffer pixels. A 16-pixel body style at 150% DPI therefore renders at 24 framebuffer pixels.

Font measurement accepts `FontSize` only. Callers must provide logical height and DPI explicitly so layout cannot silently fall back to bitmap-cell scaling.

The consuming renderer should use the returned `cell_size`, `advance`, and `line_advance` values for geometry and measurement. UI layout, hit testing, caret placement, and rendering must all use the same metrics instance.
