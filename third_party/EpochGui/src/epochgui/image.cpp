module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module epoch.gui.image;

namespace epochengine::gui_lib::image
{
    namespace
    {
        class TokenReader
        {
        public:
            explicit TokenReader(std::span<const std::byte> bytes) noexcept
                : bytes_(bytes)
            {
            }

            [[nodiscard]] bool next(std::string_view& token) noexcept
            {
                skip_prefix();
                if (position_ >= bytes_.size())
                    return false;

                const std::size_t begin = position_;
                while (position_ < bytes_.size())
                {
                    const unsigned char value = byte_at(position_);
                    if (is_space(value) || value == '#')
                        break;
                    ++position_;
                }
                if (position_ == begin)
                    return false;

                token = std::string_view{
                    reinterpret_cast<const char*>(bytes_.data() + begin),
                    position_ - begin
                };
                return true;
            }

            [[nodiscard]] bool consume_binary_separator() noexcept
            {
                if (position_ >= bytes_.size() || !is_space(byte_at(position_)))
                    return false;
                const unsigned char first = byte_at(position_++);
                if (first == '\r' && position_ < bytes_.size() && byte_at(position_) == '\n')
                    ++position_;
                return true;
            }

            [[nodiscard]] std::size_t position() const noexcept
            {
                return position_;
            }

        private:
            [[nodiscard]] static bool is_space(unsigned char value) noexcept
            {
                return value == ' ' || value == '\t' || value == '\n' || value == '\r'
                    || value == '\f' || value == '\v';
            }

            [[nodiscard]] unsigned char byte_at(std::size_t index) const noexcept
            {
                return std::to_integer<unsigned char>(bytes_[index]);
            }

            void skip_prefix() noexcept
            {
                for (;;)
                {
                    while (position_ < bytes_.size() && is_space(byte_at(position_)))
                        ++position_;
                    if (position_ >= bytes_.size() || byte_at(position_) != '#')
                        return;
                    while (position_ < bytes_.size()
                        && byte_at(position_) != '\n'
                        && byte_at(position_) != '\r')
                    {
                        ++position_;
                    }
                }
            }

            std::span<const std::byte> bytes_{};
            std::size_t position_{};
        };

        [[nodiscard]] bool parse_unsigned(std::string_view token, std::uint32_t& value) noexcept
        {
            if (token.empty())
                return false;
            const char* begin = token.data();
            const char* end = begin + token.size();
            const auto result = std::from_chars(begin, end, value, 10);
            return result.ec == std::errc{} && result.ptr == end;
        }

        [[nodiscard]] std::uint8_t scale_sample(std::uint32_t value, std::uint32_t maximum) noexcept
        {
            const std::uint64_t scaled = static_cast<std::uint64_t>(value) * 255U + maximum / 2U;
            return static_cast<std::uint8_t>(scaled / maximum);
        }

        [[nodiscard]] bool validate_dimensions(
            std::uint32_t width,
            std::uint32_t height,
            const ImageLimits& limits,
            std::size_t& pixel_count,
            ImageError& error) noexcept
        {
            if (width == 0 || height == 0
                || width > limits.maximum_width
                || height > limits.maximum_height)
            {
                error = ImageError::invalid_dimensions;
                return false;
            }
            if (static_cast<std::size_t>(width) > (std::numeric_limits<std::size_t>::max)() / height)
            {
                error = ImageError::pixel_limit_exceeded;
                return false;
            }
            pixel_count = static_cast<std::size_t>(width) * height;
            if (pixel_count > limits.maximum_pixels)
            {
                error = ImageError::pixel_limit_exceeded;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool read_ascii_pixels(
            TokenReader& reader,
            std::size_t pixel_count,
            std::uint32_t maximum,
            std::vector<Rgba8>& pixels,
            ImageError& error)
        {
            pixels.reserve(pixel_count);
            for (std::size_t index = 0; index < pixel_count; ++index)
            {
                std::uint32_t channels[3]{};
                for (std::uint32_t channel = 0; channel < 3; ++channel)
                {
                    std::string_view token;
                    if (!reader.next(token))
                    {
                        error = ImageError::truncated_data;
                        return false;
                    }
                    if (!parse_unsigned(token, channels[channel]) || channels[channel] > maximum)
                    {
                        error = ImageError::invalid_sample;
                        return false;
                    }
                }
                pixels.push_back({
                    scale_sample(channels[0], maximum),
                    scale_sample(channels[1], maximum),
                    scale_sample(channels[2], maximum),
                    255
                });
            }
            return true;
        }

        [[nodiscard]] bool read_binary_pixels(
            std::span<const std::byte> bytes,
            std::size_t offset,
            std::size_t pixel_count,
            std::uint32_t maximum,
            std::vector<Rgba8>& pixels,
            ImageError& error)
        {
            const std::size_t bytes_per_sample = maximum < 256U ? 1U : 2U;
            if (pixel_count > (std::numeric_limits<std::size_t>::max)() / (3U * bytes_per_sample))
            {
                error = ImageError::pixel_limit_exceeded;
                return false;
            }
            const std::size_t required = pixel_count * 3U * bytes_per_sample;
            if (offset > bytes.size() || required > bytes.size() - offset)
            {
                error = ImageError::truncated_data;
                return false;
            }

            pixels.reserve(pixel_count);
            std::size_t cursor = offset;
            for (std::size_t index = 0; index < pixel_count; ++index)
            {
                std::uint32_t channels[3]{};
                for (std::uint32_t channel = 0; channel < 3; ++channel)
                {
                    if (bytes_per_sample == 1U)
                    {
                        channels[channel] = std::to_integer<std::uint8_t>(bytes[cursor++]);
                    }
                    else
                    {
                        const std::uint32_t high = std::to_integer<std::uint8_t>(bytes[cursor++]);
                        const std::uint32_t low = std::to_integer<std::uint8_t>(bytes[cursor++]);
                        channels[channel] = (high << 8U) | low;
                    }
                    if (channels[channel] > maximum)
                    {
                        error = ImageError::invalid_sample;
                        return false;
                    }
                }
                pixels.push_back({
                    scale_sample(channels[0], maximum),
                    scale_sample(channels[1], maximum),
                    scale_sample(channels[2], maximum),
                    255
                });
            }
            return true;
        }
    }

    ImageResult decode_ppm(std::span<const std::byte> bytes, const ImageLimits& limits)
    {
        ImageResult result{};
        if (bytes.size() > limits.maximum_file_bytes)
        {
            result.error = ImageError::file_too_large;
            return result;
        }

        TokenReader reader{ bytes };
        std::string_view token;
        if (!reader.next(token))
        {
            result.error = ImageError::invalid_header;
            return result;
        }

        PpmEncoding encoding{};
        if (token == "P3")
            encoding = PpmEncoding::ascii_p3;
        else if (token == "P6")
            encoding = PpmEncoding::binary_p6;
        else
        {
            result.error = ImageError::unsupported_encoding;
            return result;
        }

        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t maximum{};
        if (!reader.next(token) || !parse_unsigned(token, width)
            || !reader.next(token) || !parse_unsigned(token, height)
            || !reader.next(token) || !parse_unsigned(token, maximum))
        {
            result.error = ImageError::invalid_header;
            return result;
        }
        if (maximum == 0 || maximum > 65535U)
        {
            result.error = ImageError::invalid_maximum;
            return result;
        }

        std::size_t pixel_count{};
        if (!validate_dimensions(width, height, limits, pixel_count, result.error))
            return result;

        std::vector<Rgba8> pixels;
        if (encoding == PpmEncoding::ascii_p3)
        {
            if (!read_ascii_pixels(reader, pixel_count, maximum, pixels, result.error))
                return result;
        }
        else
        {
            if (!reader.consume_binary_separator())
            {
                result.error = ImageError::invalid_header;
                return result;
            }
            if (!read_binary_pixels(bytes, reader.position(), pixel_count, maximum, pixels, result.error))
                return result;
        }

        result.image.width = width;
        result.image.height = height;
        result.image.pixels = std::move(pixels);
        result.image.encoding = encoding;
        return result;
    }

    ImageResult load_ppm_file(std::string_view path, const ImageLimits& limits)
    {
        ImageResult result{};
        if (path.empty())
        {
            result.error = ImageError::file_open_failed;
            return result;
        }

        const std::string path_string{ path };
        std::ifstream stream(path_string, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            result.error = ImageError::file_open_failed;
            return result;
        }

        const std::streampos end = stream.tellg();
        if (end < 0)
        {
            result.error = ImageError::file_open_failed;
            return result;
        }
        const auto size = static_cast<std::uintmax_t>(end);
        if (size > limits.maximum_file_bytes
            || size > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()))
        {
            result.error = ImageError::file_too_large;
            return result;
        }

        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0, std::ios::beg);
        if (bytes.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
        {
            result.error = ImageError::file_too_large;
            return result;
        }
        if (!bytes.empty()
            && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            result.error = ImageError::truncated_data;
            return result;
        }

        result = decode_ppm(bytes, limits);
        if (result)
            result.image.source = path_string;
        return result;
    }

    ImageResult extrude_edge_gutter(
        const Image& source,
        std::uint32_t gutter,
        const ImageLimits& limits)
    {
        ImageResult result{};
        if (!source.valid())
        {
            result.error = ImageError::invalid_dimensions;
            return result;
        }

        if (gutter > ((std::numeric_limits<std::uint32_t>::max)() - source.width) / 2U
            || gutter > ((std::numeric_limits<std::uint32_t>::max)() - source.height) / 2U)
        {
            result.error = ImageError::invalid_dimensions;
            return result;
        }

        const std::uint32_t width = source.width + gutter * 2U;
        const std::uint32_t height = source.height + gutter * 2U;
        std::size_t pixel_count{};
        if (!validate_dimensions(width, height, limits, pixel_count, result.error))
            return result;

        result.image.width = width;
        result.image.height = height;
        result.image.source = source.source;
        result.image.encoding = source.encoding;
        result.image.pixels.resize(pixel_count);

        for (std::uint32_t y = 0; y < height; ++y)
        {
            const std::uint32_t source_y = std::clamp(
                y,
                gutter,
                gutter + source.height - 1U) - gutter;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::uint32_t source_x = std::clamp(
                    x,
                    gutter,
                    gutter + source.width - 1U) - gutter;
                result.image.pixels[static_cast<std::size_t>(y) * width + x] =
                    source.pixels[static_cast<std::size_t>(source_y) * source.width + source_x];
            }
        }
        return result;
    }

    RasterImageLayout make_raster_image_layout(
        const Image& image,
        Rect viewport,
        ImageFit fit,
        float padding) noexcept
    {
        RasterImageLayout layout{};
        layout.viewport = viewport;
        if (!image.valid()
            || !std::isfinite(viewport.position.x)
            || !std::isfinite(viewport.position.y)
            || !std::isfinite(viewport.size.x)
            || !std::isfinite(viewport.size.y)
            || viewport.size.x <= 0.0f
            || viewport.size.y <= 0.0f
            || !std::isfinite(padding))
        {
            return layout;
        }

        const float safe_padding = (std::max)(0.0f, padding);
        Rect inner{
            { viewport.position.x + safe_padding, viewport.position.y + safe_padding },
            {
                (std::max)(0.0f, viewport.size.x - safe_padding * 2.0f),
                (std::max)(0.0f, viewport.size.y - safe_padding * 2.0f)
            }
        };
        if (inner.size.x <= 0.0f || inner.size.y <= 0.0f)
            return layout;

        float pixel_width = inner.size.x / static_cast<float>(image.width);
        float pixel_height = inner.size.y / static_cast<float>(image.height);
        Vec2 origin = inner.position;
        if (fit == ImageFit::contain)
        {
            const float pixel_size = (std::min)(pixel_width, pixel_height);
            pixel_width = pixel_size;
            pixel_height = pixel_size;
            origin.x += (inner.size.x - pixel_width * static_cast<float>(image.width)) * 0.5f;
            origin.y += (inner.size.y - pixel_height * static_cast<float>(image.height)) * 0.5f;
        }

        layout.pixel_size = { pixel_width, pixel_height };
        layout.content = {
            origin,
            {
                pixel_width * static_cast<float>(image.width),
                pixel_height * static_cast<float>(image.height)
            }
        };
        layout.valid = pixel_width > 0.0f && pixel_height > 0.0f;
        return layout;
    }

    Rect raster_pixel_rect(
        const RasterImageLayout& layout,
        std::uint32_t x,
        std::uint32_t y,
        float overlap) noexcept
    {
        if (!layout.valid || layout.pixel_size.x <= 0.0f || layout.pixel_size.y <= 0.0f)
            return {};
        const float safe_overlap = std::isfinite(overlap) ? (std::max)(0.0f, overlap) : 0.0f;
        return {
            {
                layout.content.position.x + static_cast<float>(x) * layout.pixel_size.x,
                layout.content.position.y + static_cast<float>(y) * layout.pixel_size.y
            },
            {
                layout.pixel_size.x + safe_overlap,
                layout.pixel_size.y + safe_overlap
            }
        };
    }
}
