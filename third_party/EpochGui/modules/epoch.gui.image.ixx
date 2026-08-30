module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module epoch.gui.image;

export import epoch.gui;

export namespace epochengine::gui_lib::image
{
    struct Rgba8
    {
        std::uint8_t r{};
        std::uint8_t g{};
        std::uint8_t b{};
        std::uint8_t a{ 255 };
    };

    enum class PpmEncoding : std::uint8_t
    {
        ascii_p3,
        binary_p6
    };

    enum class ImageError : std::uint8_t
    {
        none,
        file_open_failed,
        file_too_large,
        invalid_header,
        unsupported_encoding,
        invalid_dimensions,
        pixel_limit_exceeded,
        invalid_maximum,
        invalid_sample,
        truncated_data
    };

    enum class ImageFit : std::uint8_t
    {
        stretch,
        contain
    };

    struct ImageLimits
    {
        std::uint32_t maximum_width{ 4096 };
        std::uint32_t maximum_height{ 4096 };
        std::size_t maximum_pixels{ 16U * 1024U * 1024U };
        std::size_t maximum_file_bytes{ 64U * 1024U * 1024U };
    };

    struct Image
    {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<Rgba8> pixels{};
        std::string source{};
        PpmEncoding encoding{ PpmEncoding::ascii_p3 };

        [[nodiscard]] bool valid() const noexcept
        {
            if (width == 0 || height == 0
                || static_cast<std::size_t>(width) > (std::numeric_limits<std::size_t>::max)() / height)
            {
                return false;
            }
            const std::size_t expected = static_cast<std::size_t>(width) * height;
            return pixels.size() == expected;
        }

        [[nodiscard]] const Rgba8* pixel(std::uint32_t x, std::uint32_t y) const noexcept
        {
            if (x >= width || y >= height || !valid())
                return nullptr;
            return &pixels[static_cast<std::size_t>(y) * width + x];
        }
    };

    struct ImageResult
    {
        Image image{};
        ImageError error{ ImageError::none };

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == ImageError::none && image.valid();
        }
    };

    struct RasterImageLayout
    {
        Rect viewport{};
        Rect content{};
        Vec2 pixel_size{};
        bool valid{};
    };

    [[nodiscard]] ImageResult decode_ppm(
        std::span<const std::byte> bytes,
        const ImageLimits& limits = {});

    [[nodiscard]] ImageResult load_ppm_file(
        std::string_view path,
        const ImageLimits& limits = {});

    // Replicates source edges into a texture gutter so filtered atlas sampling
    // cannot bleed neighboring entries into a sprite.
    [[nodiscard]] ImageResult extrude_edge_gutter(
        const Image& source,
        std::uint32_t gutter,
        const ImageLimits& limits = {});

    [[nodiscard]] RasterImageLayout make_raster_image_layout(
        const Image& image,
        Rect viewport,
        ImageFit fit = ImageFit::contain,
        float padding = 0.0f) noexcept;

    [[nodiscard]] Rect raster_pixel_rect(
        const RasterImageLayout& layout,
        std::uint32_t x,
        std::uint32_t y,
        float overlap = 0.0f) noexcept;
}
