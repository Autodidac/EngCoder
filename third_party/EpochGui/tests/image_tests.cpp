#include <cstddef>

import epoch.gui.image;

namespace image = epochengine::gui_lib::image;

int main()
{
    constexpr char p3[] = "P3\n# comment\n2 1\n255\n255 0 0 0 128 255\n";
    const image::ImageResult p3_result = image::decode_ppm({
        reinterpret_cast<const std::byte*>(p3),
        sizeof(p3) - 1U
    });
    if (!p3_result
        || p3_result.image.width != 2
        || p3_result.image.height != 1
        || p3_result.image.pixels[0].r != 255
        || p3_result.image.pixels[1].g != 128
        || p3_result.image.pixels[1].b != 255)
    {
        return 1;
    }

    constexpr std::byte p6[]{
        std::byte{'P'}, std::byte{'6'}, std::byte{'\n'},
        std::byte{'2'}, std::byte{' '}, std::byte{'1'}, std::byte{'\n'},
        std::byte{'2'}, std::byte{'5'}, std::byte{'5'}, std::byte{'\n'},
        std::byte{255}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{128}, std::byte{255}
    };
    const image::ImageResult p6_result = image::decode_ppm(p6);
    if (!p6_result
        || p6_result.image.encoding != image::PpmEncoding::binary_p6
        || p6_result.image.pixels[1].b != 255)
    {
        return 2;
    }

    const image::RasterImageLayout layout = image::make_raster_image_layout(
        p6_result.image,
        { { 0.0f, 0.0f }, { 100.0f, 100.0f } },
        image::ImageFit::contain);
    if (!layout.valid
        || layout.content.size.x != 100.0f
        || layout.content.size.y != 50.0f
        || layout.content.position.y != 25.0f)
    {
        return 3;
    }

    const epochengine::gui_lib::Rect second_pixel = image::raster_pixel_rect(layout, 1, 0, 0.25f);
    if (second_pixel.position.x != 50.0f
        || second_pixel.position.y != 25.0f
        || second_pixel.size.x != 50.25f)
    {
        return 4;
    }

    constexpr char invalid[] = "P9\n1 1\n255\n0 0 0\n";
    const image::ImageResult invalid_result = image::decode_ppm({
        reinterpret_cast<const std::byte*>(invalid),
        sizeof(invalid) - 1U
    });
    if (invalid_result.error != image::ImageError::unsupported_encoding)
        return 5;

    const image::ImageResult padded = image::extrude_edge_gutter(p3_result.image, 1U);
    if (!padded
        || padded.image.width != 4U
        || padded.image.height != 3U
        || padded.image.pixels[0].r != 255U
        || padded.image.pixels[3].g != 128U
        || padded.image.pixels[3].b != 255U
        || padded.image.pixels[5].r != 255U
        || padded.image.pixels[6].g != 128U)
    {
        return 6;
    }

    image::Image invalid_image{};
    if (image::extrude_edge_gutter(invalid_image, 1U).error
        != image::ImageError::invalid_dimensions)
    {
        return 7;
    }

    image::ImageLimits narrow_limits{};
    narrow_limits.maximum_width = 3U;
    if (image::extrude_edge_gutter(p3_result.image, 1U, narrow_limits).error
        != image::ImageError::invalid_dimensions)
    {
        return 8;
    }

    return 0;
}
