module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module epoch.gui;

export namespace epochengine::gui_lib
{
    inline constexpr std::string_view library_name = "EpochGui";
    inline constexpr int version_major = 0;
    inline constexpr int version_minor = 89;
    inline constexpr int version_revision = 30;
    inline constexpr std::string_view version_string = "0.89.30";

    struct Vec2
    {
        float x{};
        float y{};
    };

    struct Rect
    {
        Vec2 position{};
        Vec2 size{};
    };

    [[nodiscard]] inline bool contains(Rect rect, Vec2 point) noexcept
    {
        return point.x >= rect.position.x
            && point.x <= rect.position.x + rect.size.x
            && point.y >= rect.position.y
            && point.y <= rect.position.y + rect.size.y;
    }

    class LayoutController
    {
    public:
        virtual ~LayoutController() = default;
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    };

    struct FloatingWindowState
    {
        Vec2 position{};
        Vec2 size{};
        Vec2 drag_offset{};
        Vec2 resize_origin_mouse{};
        Vec2 resize_origin_size{};
        bool open{ true };
        bool initialized{};
        bool dragging{};
        bool resizing{};
        bool close_pressed{};
        std::uint32_t focus_order{};
    };

    struct FloatingWindowOptions
    {
        Vec2 default_position{};
        Vec2 default_size{};
        Vec2 min_size{ 160.0f, 96.0f };
        Vec2 viewport_size{};
        float title_bar_height{ 32.0f };
        float content_padding{ 6.0f };
        bool movable{ true };
        bool resizable{ true };
        bool closable{ true };
    };

    struct FloatingWindowInput
    {
        Vec2 mouse_position{};
        bool mouse_down{};
        bool mouse_pressed{};
        bool mouse_released{};
    };

    struct FloatingWindowLayout
    {
        Rect window{};
        Rect title_bar{};
        Rect content{};
        Rect close_button{};
        Rect resize_handle{};
        bool visible{};
        bool hovered{};
        bool title_hovered{};
        bool close_hovered{};
        bool resize_hovered{};
        bool focused{};
        bool moved{};
        bool resized{};
        bool close_requested{};
    };

    class FloatingWindowController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void normalize(FloatingWindowState& state, const FloatingWindowOptions& options) const noexcept;
        [[nodiscard]] FloatingWindowLayout update(
            FloatingWindowState& state,
            const FloatingWindowOptions& options,
            const FloatingWindowInput& input) const noexcept;
    };

    [[nodiscard]] const FloatingWindowController& floating_window_controller() noexcept;
    void normalize_floating_window(FloatingWindowState& state, const FloatingWindowOptions& options) noexcept;
    [[nodiscard]] FloatingWindowLayout update_floating_window(
        FloatingWindowState& state,
        const FloatingWindowOptions& options,
        const FloatingWindowInput& input) noexcept;

    enum class SplitterAxis : std::uint8_t
    {
        vertical,
        horizontal
    };

    struct SplitterLayoutOptions
    {
        Rect area{};
        SplitterAxis axis{ SplitterAxis::vertical };
        float split_fraction{ 0.5f };
        float thickness{ 7.0f };
        float min_before{ 64.0f };
        float min_after{ 64.0f };
    };

    struct SplitterLayout
    {
        Rect before{};
        Rect handle{};
        Rect after{};
        float split_fraction{ 0.5f };
        float split_offset{};
        bool fits_minimums{};
    };

    enum class ProgressBarDirection : std::uint8_t
    {
        left_to_right,
        right_to_left,
        top_to_bottom,
        bottom_to_top
    };

    struct ProgressBarLayoutOptions
    {
        Rect track{};
        float value{};
        float minimum{};
        float maximum{ 1.0f };
        float padding{};
        ProgressBarDirection direction{ ProgressBarDirection::left_to_right };
    };

    struct ProgressBarLayout
    {
        Rect track{};
        Rect inner{};
        Rect fill{};
        float fraction{};
        bool has_range{};
    };

    struct LoadingScreenLayoutOptions
    {
        Rect viewport{};
        Vec2 preferred_panel_size{ 640.0f, 300.0f };
        Vec2 minimum_panel_size{ 360.0f, 220.0f };
        float margin{ 32.0f };
        float padding{ 28.0f };
        float gap{ 14.0f };
        float title_height{ 30.0f };
        float message_height{ 72.0f };
        float progress_height{ 24.0f };
        float status_height{ 22.0f };
        float action_height{ 64.0f };
        float progress_padding{ 2.0f };
        float progress_value{};
    };

    struct LoadingScreenLayout
    {
        Rect viewport{};
        Rect panel{};
        Rect title{};
        Rect message{};
        ProgressBarLayout progress{};
        Rect status{};
        Rect action{};
        float progress_fraction{};
        bool visible{};
    };

    inline constexpr std::uint32_t invalid_selectable_row_index = 0xffffffffU;

    struct SelectableListLayoutOptions
    {
        Rect viewport{};
        std::uint32_t row_count{};
        float row_height{ 22.0f };
        float row_gap{};
        float scroll_offset{};
        float content_padding_x{};
        float content_padding_y{};
    };

    struct SelectableListVisibleRange
    {
        std::uint32_t first_index{};
        std::uint32_t past_last_index{};
        float content_height{};
        bool has_visible_rows{};
    };

    struct SelectableRowLayout
    {
        std::uint32_t index{ invalid_selectable_row_index };
        Rect row{};
        Rect content{};
        bool visible{};
        bool hovered{};
        bool selected{};
    };

    class LayoutPrimitiveController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] SplitterLayout make_splitter(const SplitterLayoutOptions& options) const noexcept;
        [[nodiscard]] float splitter_fraction_from(Vec2 point, const SplitterLayoutOptions& options) const noexcept;
        [[nodiscard]] bool splitter_hit_test(const SplitterLayout& layout, Vec2 point, float hit_padding = 0.0f) const noexcept;
        [[nodiscard]] ProgressBarLayout make_progress_bar(const ProgressBarLayoutOptions& options) const noexcept;
        [[nodiscard]] LoadingScreenLayout make_loading_screen(const LoadingScreenLayoutOptions& options) const noexcept;
        [[nodiscard]] SelectableListVisibleRange visible_range(const SelectableListLayoutOptions& options) const noexcept;
        [[nodiscard]] SelectableRowLayout make_selectable_row(
            const SelectableListLayoutOptions& options,
            std::uint32_t index,
            Vec2 mouse_position,
            bool selected = false) const noexcept;
        [[nodiscard]] std::uint32_t selectable_row_at(const SelectableListLayoutOptions& options, Vec2 point) const noexcept;
    };

    [[nodiscard]] const LayoutPrimitiveController& layout_primitive_controller() noexcept;
    [[nodiscard]] SplitterLayout make_splitter_layout(const SplitterLayoutOptions& options) noexcept;
    [[nodiscard]] float splitter_fraction_from_point(const SplitterLayoutOptions& options, Vec2 point) noexcept;
    [[nodiscard]] bool splitter_hit_test(const SplitterLayout& layout, Vec2 point, float hit_padding = 0.0f) noexcept;
    [[nodiscard]] ProgressBarLayout make_progress_bar_layout(const ProgressBarLayoutOptions& options) noexcept;
    [[nodiscard]] LoadingScreenLayout make_loading_screen_layout(const LoadingScreenLayoutOptions& options) noexcept;
    [[nodiscard]] SelectableListVisibleRange selectable_list_visible_range(const SelectableListLayoutOptions& options) noexcept;
    [[nodiscard]] SelectableRowLayout make_selectable_row_layout(
        const SelectableListLayoutOptions& options,
        std::uint32_t index,
        Vec2 mouse_position,
        bool selected = false) noexcept;
    [[nodiscard]] std::uint32_t selectable_row_index_at(const SelectableListLayoutOptions& options, Vec2 point) noexcept;

    struct SegmentedControlLayoutOptions
    {
        Vec2 position{};
        std::span<const float> item_widths{};
        float height{ 26.0f };
        float gap{ 2.0f };
    };

    struct SegmentedControlLayout
    {
        Rect bounds{};
        std::uint32_t item_count{};
        float height{};
        float gap{};
        bool valid{};
    };

    enum class ImageFitMode : std::uint8_t
    {
        stretch,
        contain
    };

    struct ImageBoxLayoutOptions
    {
        Rect bounds{};
        Vec2 source_extent{};
        ImageFitMode fit{ImageFitMode::contain};
        float padding{4.0f};
        float caption_height{};
    };

    struct ImageBoxLayout
    {
        Rect frame{};
        Rect viewport{};
        Rect content{};
        Rect caption{};
        bool valid{};
    };

    struct TabButtonLayoutOptions
    {
        SegmentedControlLayoutOptions strip{};
        float indicator_height{3.0f};
        float close_extent{16.0f};
        float label_padding{8.0f};
    };

    struct TabButtonLayout
    {
        Rect button{};
        Rect label{};
        Rect indicator{};
        Rect close_button{};
        bool active{};
        bool closable{};
        bool valid{};
    };

    struct ToolTabSizingPolicy
    {
        float minimum_width{ 96.0f };
        float maximum_width{ 176.0f };
        float horizontal_padding{ 10.0f };
        float close_extent{ 18.0f };
        float dirty_extent{ 8.0f };
    };

    struct ResponsiveTabStripOptions
    {
        std::span<const float> item_widths{};
        std::size_t active_index{ (std::numeric_limits<std::size_t>::max)() };
        float available_width{};
        float gap{ 2.0f };
        float overflow_width{ 120.0f };
    };

    struct ResponsiveTabWidthOptions
    {
        float requested_width{};
        float measured_label_width{};
        float minimum_hit_width{ 72.0f };
        float horizontal_padding{ 24.0f };
        float close_extent{};
        float dirty_extent{};
    };

    [[nodiscard]] inline float resolve_responsive_tab_width(
        const ResponsiveTabWidthOptions& options) noexcept
    {
        const float minimum = std::isfinite(options.minimum_hit_width)
            ? (std::max)(1.0f, options.minimum_hit_width)
            : 72.0f;
        const float requested = std::isfinite(options.requested_width)
            ? (std::max)(0.0f, options.requested_width)
            : 0.0f;
        const float label = std::isfinite(options.measured_label_width)
            ? (std::max)(0.0f, options.measured_label_width)
            : 0.0f;
        const float padding = std::isfinite(options.horizontal_padding)
            ? (std::max)(0.0f, options.horizontal_padding)
            : 0.0f;
        const float close = std::isfinite(options.close_extent)
            ? (std::max)(0.0f, options.close_extent)
            : 0.0f;
        const float dirty = std::isfinite(options.dirty_extent)
            ? (std::max)(0.0f, options.dirty_extent)
            : 0.0f;
        return (std::max)({
            minimum,
            requested,
            label + padding + close + dirty});
    }

    struct ResponsiveTabStripLayout
    {
        std::vector<std::uint32_t> visible_indices{};
        std::vector<std::uint32_t> overflow_indices{};
        float visible_width{};
        float overflow_width{};
        bool overflowed{};
        bool valid{};

        [[nodiscard]] bool is_visible(std::size_t index) const noexcept
        {
            return std::find(visible_indices.begin(), visible_indices.end(), index)
                != visible_indices.end();
        }
    };

    enum class ResponsiveTabNavigationIntent : std::uint8_t
    {
        previous,
        next,
        first,
        last
    };

    struct ResponsiveTabNavigationOptions
    {
        std::span<const std::uint8_t> enabled{};
        std::size_t active_index{ (std::numeric_limits<std::size_t>::max)() };
        ResponsiveTabNavigationIntent intent{ ResponsiveTabNavigationIntent::next };
        bool wrap{ true };
    };

    [[nodiscard]] inline std::optional<std::size_t>
        navigate_responsive_tab_strip(
            const ResponsiveTabNavigationOptions& options) noexcept
    {
        if (options.enabled.empty())
            return std::nullopt;
        const auto enabled = [&](const std::size_t index) noexcept
        {
            return index < options.enabled.size() && options.enabled[index] != 0u;
        };
        if (options.intent == ResponsiveTabNavigationIntent::first)
        {
            for (std::size_t index = 0u; index < options.enabled.size(); ++index)
                if (enabled(index)) return index;
            return std::nullopt;
        }
        if (options.intent == ResponsiveTabNavigationIntent::last)
        {
            for (std::size_t index = options.enabled.size(); index > 0u; --index)
                if (enabled(index - 1u)) return index - 1u;
            return std::nullopt;
        }

        const bool forward =
            options.intent == ResponsiveTabNavigationIntent::next;
        std::size_t cursor = options.active_index < options.enabled.size()
            ? options.active_index
            : (forward ? options.enabled.size() - 1u : 0u);
        for (std::size_t visited = 0u; visited < options.enabled.size(); ++visited)
        {
            if (forward)
            {
                if (cursor + 1u >= options.enabled.size())
                {
                    if (!options.wrap) return std::nullopt;
                    cursor = 0u;
                }
                else ++cursor;
            }
            else if (cursor == 0u)
            {
                if (!options.wrap) return std::nullopt;
                cursor = options.enabled.size() - 1u;
            }
            else --cursor;
            if (enabled(cursor)) return cursor;
        }
        return std::nullopt;
    }

    struct BottomDockHeightOptions
    {
        float viewport_height{};
        float toolbar_height{};
        float splitter_height{ 7.0f };
        float requested_fraction{ 0.24f };
        float minimum_bottom_height{ 120.0f };
        float minimum_center_height{ 240.0f };
        float maximum_bottom_fraction{ 0.58f };
        bool visible{ true };
    };

    struct BottomDockHeightLayout
    {
        float bottom_height{};
        float center_height{};
        float splitter_height{};
        float normalized_fraction{};
        bool visible{};
        bool valid{};
    };

    [[nodiscard]] inline BottomDockHeightLayout
        make_bottom_dock_height_layout(
            const BottomDockHeightOptions& options) noexcept
    {
        BottomDockHeightLayout result{};
        if (!std::isfinite(options.viewport_height)
            || !std::isfinite(options.toolbar_height)
            || !std::isfinite(options.splitter_height)
            || !std::isfinite(options.requested_fraction)
            || !std::isfinite(options.minimum_bottom_height)
            || !std::isfinite(options.minimum_center_height)
            || !std::isfinite(options.maximum_bottom_fraction)
            || options.viewport_height < 0.0f
            || options.toolbar_height < 0.0f)
        {
            return result;
        }

        result.visible = options.visible;
        result.splitter_height = options.visible
            ? (std::max)(0.0f, options.splitter_height)
            : 0.0f;
        const float available = (std::max)(
            0.0f,
            options.viewport_height - options.toolbar_height
                - result.splitter_height);
        if (!options.visible)
        {
            result.center_height = available;
            result.valid = true;
            return result;
        }

        const float minimumBottom = (std::min)(
            available, (std::max)(0.0f, options.minimum_bottom_height));
        const float minimumCenter = (std::min)(
            available, (std::max)(0.0f, options.minimum_center_height));
        const float maximumFraction = std::clamp(
            options.maximum_bottom_fraction, 0.10f, 0.90f);
        const float maximumBottom = (std::max)(
            minimumBottom,
            (std::min)(available - minimumCenter, available * maximumFraction));
        const float requested = available * std::clamp(
            options.requested_fraction, 0.0f, 1.0f);
        result.bottom_height = std::clamp(
            requested, minimumBottom, maximumBottom);
        result.center_height = (std::max)(
            0.0f, available - result.bottom_height);
        result.normalized_fraction = available > 0.0f
            ? result.bottom_height / available
            : 0.0f;
        result.valid = true;
        return result;
    }

    enum class ChromeDensity : std::uint8_t
    {
        full,
        compact,
        minimal
    };

    struct ChromeBarItemOptions
    {
        float preferred_width{ 72.0f };
        float compact_width{ 52.0f };
        std::uint16_t priority{};
        bool pinned{};
        bool overflowable{ true };
    };

    struct ChromeBarZoneLayout
    {
        std::vector<Rect> item_bounds{};
        std::vector<std::uint32_t> visible_indices{};
        std::vector<std::uint32_t> overflow_indices{};
        Rect overflow_button{};
        float occupied_width{};
        bool used_compact_widths{};
        bool overflowed{};

        [[nodiscard]] bool is_visible(std::size_t index) const noexcept
        {
            return index < item_bounds.size()
                && item_bounds[index].size.x > 0.0f
                && item_bounds[index].size.y > 0.0f;
        }
    };

    struct ChromeBarOptions
    {
        Rect bounds{};
        std::span<const ChromeBarItemOptions> left_items{};
        std::span<const ChromeBarItemOptions> center_items{};
        std::span<const ChromeBarItemOptions> right_items{};
        float item_gap{ 4.0f };
        float zone_gap{ 12.0f };
        float overflow_width{ 82.0f };
        float horizontal_padding{ 12.0f };
    };

    struct ChromeBarLayout
    {
        Rect bounds{};
        ChromeBarZoneLayout left{};
        ChromeBarZoneLayout center{};
        ChromeBarZoneLayout right{};
        ChromeDensity density{ ChromeDensity::full };
        bool valid{};
    };

    [[nodiscard]] inline std::string scoped_control_key(
        std::string_view host,
        std::string_view window,
        std::string_view control)
    {
        std::string key{};
        key.reserve(
            host.size() + window.size() + control.size() + 32u);
        key.append(host);
        key.push_back('|');
        key.append(std::to_string(window.size()));
        key.push_back(':');
        key.append(window);
        key.push_back('|');
        key.append(control);
        return key;
    }

    [[nodiscard]] inline float preferred_tool_tab_width(
        float measured_label_width,
        bool closable,
        bool dirty,
        const ToolTabSizingPolicy& policy = {}) noexcept
    {
        const float minimumWidth = (std::max)(1.0f, policy.minimum_width);
        const float maximumWidth = (std::max)(minimumWidth, policy.maximum_width);
        const float labelWidth = std::isfinite(measured_label_width)
            ? (std::max)(0.0f, measured_label_width)
            : 0.0f;
        const float padding = (std::max)(0.0f, policy.horizontal_padding);
        const float closeExtent = closable
            ? (std::max)(0.0f, policy.close_extent)
            : 0.0f;
        const float dirtyExtent = dirty
            ? (std::max)(0.0f, policy.dirty_extent)
            : 0.0f;
        return (std::clamp)(
            labelWidth + padding * 2.0f + closeExtent + dirtyExtent,
            minimumWidth,
            maximumWidth);
    }

    [[nodiscard]] inline ResponsiveTabStripLayout
        make_responsive_tab_strip_layout(
            const ResponsiveTabStripOptions& options)
    {
        ResponsiveTabStripLayout result{};
        if (options.item_widths.empty())
            return result;

        std::vector<float> widths{};
        widths.reserve(options.item_widths.size());
        for (const float requested : options.item_widths)
        {
            widths.push_back(std::isfinite(requested)
                ? (std::max)(1.0f, requested)
                : 1.0f);
        }

        const float gap = std::isfinite(options.gap)
            ? (std::max)(0.0f, options.gap)
            : 0.0f;
        const float available = std::isfinite(options.available_width)
            ? (std::max)(0.0f, options.available_width)
            : 0.0f;
        float completeWidth{};
        for (std::size_t index = 0; index < widths.size(); ++index)
        {
            if (index > 0u)
                completeWidth += gap;
            completeWidth += widths[index];
        }

        result.valid = true;
        if (available <= 0.0f || completeWidth <= available)
        {
            result.visible_indices.reserve(widths.size());
            for (std::size_t index = 0; index < widths.size(); ++index)
                result.visible_indices.push_back(static_cast<std::uint32_t>(index));
            result.visible_width = completeWidth;
            return result;
        }

        result.overflowed = true;
        const float requestedOverflow = std::isfinite(options.overflow_width)
            ? (std::max)(1.0f, options.overflow_width)
            : 120.0f;
        result.overflow_width = (std::min)(available, requestedOverflow);
        const float overflowGap = available > result.overflow_width ? gap : 0.0f;
        const float visibleBudget = (std::max)(
            0.0f,
            available - result.overflow_width - overflowGap);

        auto width_with = [&](std::size_t index) noexcept
        {
            return result.visible_width
                + (result.visible_indices.empty() ? 0.0f : gap)
                + widths[index];
        };
        for (std::size_t index = 0; index < widths.size(); ++index)
        {
            if (width_with(index) > visibleBudget)
                break;
            result.visible_width = width_with(index);
            result.visible_indices.push_back(static_cast<std::uint32_t>(index));
        }

        const bool activeValid = options.active_index < widths.size();
        if (activeValid && !result.is_visible(options.active_index)
            && widths[options.active_index] <= visibleBudget)
        {
            while (!result.visible_indices.empty()
                && width_with(options.active_index) > visibleBudget)
            {
                const std::size_t removed = result.visible_indices.back();
                result.visible_indices.pop_back();
                result.visible_width -= widths[removed];
                if (!result.visible_indices.empty())
                    result.visible_width -= gap;
            }
            if (width_with(options.active_index) <= visibleBudget)
            {
                result.visible_width = width_with(options.active_index);
                result.visible_indices.push_back(
                    static_cast<std::uint32_t>(options.active_index));
                std::sort(
                    result.visible_indices.begin(),
                    result.visible_indices.end());
            }
        }

        result.overflow_indices.reserve(
            widths.size() - result.visible_indices.size());
        for (std::size_t index = 0; index < widths.size(); ++index)
        {
            if (!result.is_visible(index))
                result.overflow_indices.push_back(static_cast<std::uint32_t>(index));
        }
        return result;
    }

    [[nodiscard]] inline ChromeBarLayout make_chrome_bar_layout(
        const ChromeBarOptions& options)
    {
        ChromeBarLayout result{};
        const auto finite_rect = [](Rect rect) noexcept
        {
            return std::isfinite(rect.position.x)
                && std::isfinite(rect.position.y)
                && std::isfinite(rect.size.x)
                && std::isfinite(rect.size.y)
                && rect.size.x > 0.0f
                && rect.size.y > 0.0f;
        };
        if (!finite_rect(options.bounds))
            return result;

        const float padding = std::isfinite(options.horizontal_padding)
            ? (std::max)(0.0f, options.horizontal_padding)
            : 0.0f;
        const float gap = std::isfinite(options.item_gap)
            ? (std::max)(0.0f, options.item_gap)
            : 0.0f;
        const float zoneGap = std::isfinite(options.zone_gap)
            ? (std::max)(0.0f, options.zone_gap)
            : 0.0f;
        const float overflowWidth = std::isfinite(options.overflow_width)
            ? (std::max)(1.0f, options.overflow_width)
            : 82.0f;
        result.bounds = options.bounds;

        const float contentLeft = options.bounds.position.x + padding;
        const float contentRight = options.bounds.position.x
            + options.bounds.size.x - padding;
        const float contentWidth = contentRight - contentLeft;
        if (contentWidth <= 0.0f)
            return result;

        struct PlannedZone
        {
            std::vector<float> widths{};
            std::vector<std::uint32_t> visible{};
            std::vector<std::uint32_t> overflow{};
            float items_width{};
            float overflow_width{};
            float total_width{};
            bool compact{};
        };

        const auto sane_width = [](float value, float fallback) noexcept
        {
            return std::isfinite(value)
                ? (std::max)(1.0f, value)
                : fallback;
        };
        const auto plan_zone = [&](
            std::span<const ChromeBarItemOptions> items,
            float requestedBudget) -> PlannedZone
        {
            PlannedZone plan{};
            plan.widths.assign(items.size(), 0.0f);
            const float budget = std::isfinite(requestedBudget)
                ? (std::max)(0.0f, requestedBudget)
                : 0.0f;
            if (items.empty() || budget <= 0.0f)
            {
                for (std::size_t index = 0; index < items.size(); ++index)
                {
                    if (items[index].overflowable)
                        plan.overflow.push_back(
                            static_cast<std::uint32_t>(index));
                }
                if (!plan.overflow.empty())
                {
                    plan.overflow_width = (std::min)(budget, overflowWidth);
                    plan.total_width = plan.overflow_width;
                }
                return plan;
            }

            const auto complete_width = [&](bool compact) noexcept
            {
                float width{};
                for (std::size_t index = 0; index < items.size(); ++index)
                {
                    if (index > 0u)
                        width += gap;
                    const float preferred = sane_width(
                        items[index].preferred_width, 72.0f);
                    const float minimum = (std::min)(
                        preferred,
                        sane_width(items[index].compact_width, preferred));
                    width += compact ? minimum : preferred;
                }
                return width;
            };

            const float preferredWidth = complete_width(false);
            const float compactWidth = complete_width(true);
            if (preferredWidth <= budget || compactWidth <= budget)
            {
                plan.compact = preferredWidth > budget;
                plan.items_width = plan.compact
                    ? compactWidth
                    : preferredWidth;
                plan.total_width = plan.items_width;
                for (std::size_t index = 0; index < items.size(); ++index)
                {
                    const float preferred = sane_width(
                        items[index].preferred_width, 72.0f);
                    const float minimum = (std::min)(
                        preferred,
                        sane_width(items[index].compact_width, preferred));
                    plan.widths[index] = plan.compact ? minimum : preferred;
                    plan.visible.push_back(
                        static_cast<std::uint32_t>(index));
                }
                return plan;
            }

            plan.compact = true;
            std::vector<std::uint32_t> order{};
            order.reserve(items.size());
            for (std::size_t index = 0; index < items.size(); ++index)
                order.push_back(static_cast<std::uint32_t>(index));
            std::stable_sort(
                order.begin(),
                order.end(),
                [&](std::uint32_t lhs, std::uint32_t rhs)
                {
                    if (items[lhs].pinned != items[rhs].pinned)
                        return items[lhs].pinned;
                    if (items[lhs].priority != items[rhs].priority)
                        return items[lhs].priority < items[rhs].priority;
                    return lhs < rhs;
                });

            const bool hasOverflowable = std::any_of(
                items.begin(),
                items.end(),
                [](const ChromeBarItemOptions& item)
                {
                    return item.overflowable;
                });
            const float reservedOverflow = hasOverflowable
                ? (std::min)(budget, overflowWidth)
                : 0.0f;
            const float selectionBudget = (std::max)(
                0.0f,
                budget - reservedOverflow
                    - (reservedOverflow > 0.0f ? gap : 0.0f));
            float selectedWidth{};
            for (const std::uint32_t index : order)
            {
                const float preferred = sane_width(
                    items[index].preferred_width, 72.0f);
                const float minimum = (std::min)(
                    preferred,
                    sane_width(items[index].compact_width, preferred));
                const float candidate = selectedWidth
                    + (plan.visible.empty() ? 0.0f : gap)
                    + minimum;
                if (candidate <= selectionBudget)
                {
                    selectedWidth = candidate;
                    plan.widths[index] = minimum;
                    plan.visible.push_back(index);
                }
            }
            std::sort(plan.visible.begin(), plan.visible.end());
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                if (std::find(
                        plan.visible.begin(),
                        plan.visible.end(),
                        static_cast<std::uint32_t>(index))
                    == plan.visible.end()
                    && items[index].overflowable)
                {
                    plan.overflow.push_back(
                        static_cast<std::uint32_t>(index));
                }
            }
            plan.items_width = selectedWidth;
            if (plan.overflow.empty())
            {
                plan.total_width = selectedWidth;
            }
            else
            {
                plan.overflow_width = reservedOverflow;
                plan.total_width = selectedWidth
                    + (selectedWidth > 0.0f && reservedOverflow > 0.0f
                        ? gap
                        : 0.0f)
                    + reservedOverflow;
            }
            return plan;
        };

        const bool hasLeft = !options.left_items.empty();
        const bool hasRight = !options.right_items.empty();
        const float leftReserve = hasLeft
            ? (std::min)(overflowWidth, contentWidth)
            : 0.0f;
        const float rightReserve = hasRight
            ? (std::min)(overflowWidth, contentWidth)
            : 0.0f;
        const float centerBudget = (std::max)(
            0.0f,
            contentWidth - leftReserve - rightReserve
                - (hasLeft ? zoneGap : 0.0f)
                - (hasRight ? zoneGap : 0.0f));
        const PlannedZone centerPlan = plan_zone(
            options.center_items,
            centerBudget);
        const float centerX = contentLeft
            + (std::max)(
                0.0f,
                (contentWidth - centerPlan.total_width) * 0.5f);
        const float leftBudget = options.center_items.empty()
            ? contentWidth * 0.55f
            : (std::max)(0.0f, centerX - zoneGap - contentLeft);
        const float rightStart = centerX + centerPlan.total_width;
        const float rightBudget = options.center_items.empty()
            ? (std::max)(0.0f, contentWidth - leftBudget - zoneGap)
            : (std::max)(
                0.0f,
                contentRight - rightStart - zoneGap);
        const PlannedZone leftPlan = plan_zone(
            options.left_items,
            leftBudget);
        const PlannedZone rightPlan = plan_zone(
            options.right_items,
            rightBudget);

        const auto place_zone = [&](
            ChromeBarZoneLayout& target,
            const PlannedZone& plan,
            std::span<const ChromeBarItemOptions> items,
            float x)
        {
            target.item_bounds.assign(items.size(), {});
            target.visible_indices = plan.visible;
            target.overflow_indices = plan.overflow;
            target.occupied_width = plan.total_width;
            target.used_compact_widths = plan.compact;
            target.overflowed = !plan.overflow.empty();
            float cursorX = x;
            for (const std::uint32_t index : plan.visible)
            {
                target.item_bounds[index] = {
                    { cursorX, options.bounds.position.y },
                    { plan.widths[index], options.bounds.size.y }
                };
                cursorX += plan.widths[index] + gap;
            }
            if (plan.overflow_width > 0.0f)
            {
                cursorX = plan.visible.empty()
                    ? x
                    : x + plan.items_width + gap;
                target.overflow_button = {
                    { cursorX, options.bounds.position.y },
                    { plan.overflow_width, options.bounds.size.y }
                };
            }
        };

        place_zone(
            result.left,
            leftPlan,
            options.left_items,
            contentLeft);
        place_zone(
            result.center,
            centerPlan,
            options.center_items,
            centerX);
        place_zone(
            result.right,
            rightPlan,
            options.right_items,
            contentRight - rightPlan.total_width);

        const bool anyOverflow = result.left.overflowed
            || result.center.overflowed
            || result.right.overflowed;
        const bool anyCompact = result.left.used_compact_widths
            || result.center.used_compact_widths
            || result.right.used_compact_widths;
        result.density = anyOverflow
            ? ChromeDensity::minimal
            : anyCompact ? ChromeDensity::compact : ChromeDensity::full;
        result.valid = true;
        return result;
    }

    struct SliderLayoutOptions
    {
        Rect bounds{};
        float minimum{};
        float maximum{1.0f};
        float value{};
        float step{0.01f};
        float horizontal_padding{4.0f};
        float track_height{3.0f};
        float thumb_width{5.0f};
        float thumb_height{11.0f};
    };

    struct SliderLayout
    {
        Rect bounds{};
        Rect track{};
        Rect fill{};
        Rect thumb{};
        float value{};
        float fraction{};
        bool valid{};
    };

    struct ToggleSwitchLayoutOptions
    {
        Rect bounds{};
        bool value{};
        float padding{ 3.0f };
    };

    struct ToggleSwitchLayout
    {
        Rect track{};
        Rect thumb{};
        bool value{};
        bool valid{};
    };

    class SelectionControlController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "Selection controls";
        }

        [[nodiscard]] SegmentedControlLayout make_segmented_control(
            const SegmentedControlLayoutOptions& options) const noexcept
        {
            const float height = options.height > 1.0f ? options.height : 1.0f;
            const float gap = options.gap > 0.0f ? options.gap : 0.0f;
            float width = 0.0f;
            for (const float requestedWidth : options.item_widths)
                width += requestedWidth > 1.0f ? requestedWidth : 1.0f;
            if (options.item_widths.size() > 1)
                width += gap * static_cast<float>(options.item_widths.size() - 1);

            return SegmentedControlLayout{
                .bounds = Rect{ options.position, Vec2{ width, height } },
                .item_count = static_cast<std::uint32_t>(options.item_widths.size()),
                .height = height,
                .gap = gap,
                .valid = !options.item_widths.empty()
            };
        }

        [[nodiscard]] Rect segmented_item(
            const SegmentedControlLayoutOptions& options,
            std::uint32_t index) const noexcept
        {
            const SegmentedControlLayout layout = make_segmented_control(options);
            if (!layout.valid || index >= layout.item_count)
                return {};

            float x = options.position.x;
            for (std::uint32_t current = 0; current < index; ++current)
            {
                const float requestedWidth = options.item_widths[current];
                x += (requestedWidth > 1.0f ? requestedWidth : 1.0f) + layout.gap;
            }

            const float requestedWidth = options.item_widths[index];
            return Rect{
                .position = Vec2{ x, options.position.y },
                .size = Vec2{ requestedWidth > 1.0f ? requestedWidth : 1.0f, layout.height }
            };
        }

        [[nodiscard]] std::uint32_t segmented_item_at(
            const SegmentedControlLayoutOptions& options,
            Vec2 point) const noexcept
        {
            const SegmentedControlLayout layout = make_segmented_control(options);
            if (!layout.valid || !contains(layout.bounds, point))
                return invalid_selectable_row_index;

            for (std::uint32_t index = 0; index < layout.item_count; ++index)
            {
                if (contains(segmented_item(options, index), point))
                    return index;
            }
            return invalid_selectable_row_index;
        }

        [[nodiscard]] ImageBoxLayout make_image_box(
            const ImageBoxLayoutOptions& options) const noexcept
        {
            const float width = options.bounds.size.x > 1.0f
                ? options.bounds.size.x
                : 1.0f;
            const float height = options.bounds.size.y > 1.0f
                ? options.bounds.size.y
                : 1.0f;
            const float maximumPadding = (std::min)(width, height) * 0.5f;
            const float padding = options.padding > 0.0f
                ? (std::min)(options.padding, maximumPadding)
                : 0.0f;
            const float innerWidth = (std::max)(1.0f, width - padding * 2.0f);
            const float innerHeight = (std::max)(1.0f, height - padding * 2.0f);
            const float captionHeight = options.caption_height > 0.0f
                ? (std::min)(options.caption_height, innerHeight)
                : 0.0f;
            const float viewportHeight = (std::max)(1.0f, innerHeight - captionHeight);

            ImageBoxLayout layout{};
            layout.frame = Rect{options.bounds.position, {width, height}};
            layout.viewport = Rect{
                {options.bounds.position.x + padding, options.bounds.position.y + padding},
                {innerWidth, viewportHeight}};
            if (captionHeight > 0.0f)
            {
                layout.caption = Rect{
                    {options.bounds.position.x + padding,
                     options.bounds.position.y + height - padding - captionHeight},
                    {innerWidth, captionHeight}};
            }

            const float sourceWidth = options.source_extent.x > 0.0f
                ? options.source_extent.x
                : 0.0f;
            const float sourceHeight = options.source_extent.y > 0.0f
                ? options.source_extent.y
                : 0.0f;
            if (sourceWidth == 0.0f || sourceHeight == 0.0f)
                return layout;

            if (options.fit == ImageFitMode::stretch)
            {
                layout.content = layout.viewport;
            }
            else
            {
                const float scale = (std::min)(
                    layout.viewport.size.x / sourceWidth,
                    layout.viewport.size.y / sourceHeight);
                const Vec2 contentSize{sourceWidth * scale, sourceHeight * scale};
                layout.content = Rect{
                    {layout.viewport.position.x
                        + (layout.viewport.size.x - contentSize.x) * 0.5f,
                     layout.viewport.position.y
                        + (layout.viewport.size.y - contentSize.y) * 0.5f},
                    contentSize};
            }
            layout.valid = true;
            return layout;
        }

        [[nodiscard]] TabButtonLayout make_tab_button(
            const TabButtonLayoutOptions& options,
            std::uint32_t index,
            bool active,
            bool closable) const noexcept
        {
            const Rect button = segmented_item(options.strip, index);
            if (button.size.x <= 0.0f || button.size.y <= 0.0f)
                return {};

            const float indicatorHeight = (std::min)(
                (std::max)(1.0f, options.indicator_height),
                button.size.y);
            const float padding = (std::max)(0.0f, options.label_padding);
            const float closeExtent = closable
                ? (std::min)((std::max)(8.0f, options.close_extent), button.size.y)
                : 0.0f;
            const float labelWidth = (std::max)(
                1.0f,
                button.size.x - padding * 2.0f - closeExtent);

            TabButtonLayout layout{};
            layout.button = button;
            layout.label = Rect{
                {button.position.x + padding, button.position.y},
                {labelWidth, button.size.y - indicatorHeight}};
            layout.indicator = Rect{
                {button.position.x, button.position.y + button.size.y - indicatorHeight},
                {button.size.x, indicatorHeight}};
            if (closable)
            {
                layout.close_button = Rect{
                    {button.position.x + button.size.x - closeExtent,
                     button.position.y + (button.size.y - closeExtent) * 0.5f},
                    {closeExtent, closeExtent}};
            }
            layout.active = active;
            layout.closable = closable;
            layout.valid = true;
            return layout;
        }

        [[nodiscard]] SliderLayout make_slider(
            const SliderLayoutOptions& options) const noexcept
        {
            SliderLayout layout{};
            layout.bounds = options.bounds;
            if (!std::isfinite(options.bounds.position.x)
                || !std::isfinite(options.bounds.position.y)
                || !std::isfinite(options.bounds.size.x)
                || !std::isfinite(options.bounds.size.y)
                || !std::isfinite(options.minimum)
                || !std::isfinite(options.maximum)
                || !std::isfinite(options.value)
                || !std::isfinite(options.step)
                || options.bounds.size.x < 1.0f
                || options.bounds.size.y < 1.0f
                || options.maximum <= options.minimum
                || options.step <= 0.0f)
            {
                return layout;
            }

            const float padding = (std::clamp)(
                options.horizontal_padding,
                0.0f,
                options.bounds.size.x * 0.5f);
            const float trackWidth = (std::max)(
                0.0f, options.bounds.size.x - padding * 2.0f);
            const float trackHeight = (std::clamp)(
                options.track_height, 1.0f, options.bounds.size.y);
            const float thumbWidth = (std::clamp)(
                options.thumb_width, 1.0f, options.bounds.size.x);
            const float thumbHeight = (std::clamp)(
                options.thumb_height, 1.0f, options.bounds.size.y);
            layout.value = (std::clamp)(
                options.value, options.minimum, options.maximum);
            layout.fraction = (layout.value - options.minimum)
                / (options.maximum - options.minimum);
            layout.track = {
                {options.bounds.position.x + padding,
                 options.bounds.position.y + options.bounds.size.y
                    - trackHeight},
                {trackWidth, trackHeight}};
            layout.fill = layout.track;
            layout.fill.size.x = trackWidth * layout.fraction;
            const float thumbTravel = (std::max)(
                0.0f, trackWidth - thumbWidth);
            layout.thumb = {
                {layout.track.position.x + thumbTravel * layout.fraction,
                 options.bounds.position.y + options.bounds.size.y
                    - thumbHeight},
                {thumbWidth, thumbHeight}};
            layout.valid = true;
            return layout;
        }

        [[nodiscard]] float slider_value_at(
            const SliderLayoutOptions& options,
            float horizontal_position) const noexcept
        {
            const SliderLayout layout = make_slider(options);
            if (!layout.valid || layout.track.size.x <= 0.0f
                || !std::isfinite(horizontal_position))
            {
                return options.value;
            }

            const float fraction = (std::clamp)(
                (horizontal_position - layout.track.position.x)
                    / layout.track.size.x,
                0.0f,
                1.0f);
            const float raw = options.minimum
                + (options.maximum - options.minimum) * fraction;
            const float steps = std::round(
                (raw - options.minimum) / options.step);
            return (std::clamp)(
                options.minimum + steps * options.step,
                options.minimum,
                options.maximum);
        }

        [[nodiscard]] ToggleSwitchLayout make_toggle_switch(
            const ToggleSwitchLayoutOptions& options) const noexcept
        {
            const float width = options.bounds.size.x > 1.0f
                ? options.bounds.size.x
                : 1.0f;
            const float height = options.bounds.size.y > 1.0f
                ? options.bounds.size.y
                : 1.0f;
            const float padding = options.padding > 0.0f
                ? (std::min)(options.padding, height * 0.25f)
                : 0.0f;
            const float thumb_extent = (std::max)(1.0f, height - padding * 2.0f);
            const float thumb_x = options.value
                ? options.bounds.position.x + width - padding - thumb_extent
                : options.bounds.position.x + padding;

            return ToggleSwitchLayout{
                .track = Rect{ options.bounds.position, Vec2{ width, height } },
                .thumb = Rect{
                    Vec2{ thumb_x, options.bounds.position.y + padding },
                    Vec2{ thumb_extent, thumb_extent }
                },
                .value = options.value,
                .valid = width >= height && height >= 8.0f
            };
        }
    };

    [[nodiscard]] inline const SelectionControlController& selection_control_controller() noexcept
    {
        static const SelectionControlController controller{};
        return controller;
    }

    [[nodiscard]] inline SegmentedControlLayout make_segmented_control_layout(
        const SegmentedControlLayoutOptions& options) noexcept
    {
        return selection_control_controller().make_segmented_control(options);
    }

    [[nodiscard]] inline Rect segmented_control_item_layout(
        const SegmentedControlLayoutOptions& options,
        std::uint32_t index) noexcept
    {
        return selection_control_controller().segmented_item(options, index);
    }

    [[nodiscard]] inline std::uint32_t segmented_control_item_at(
        const SegmentedControlLayoutOptions& options,
        Vec2 point) noexcept
    {
        return selection_control_controller().segmented_item_at(options, point);
    }

    [[nodiscard]] inline ImageBoxLayout make_image_box_layout(
        const ImageBoxLayoutOptions& options) noexcept
    {
        return selection_control_controller().make_image_box(options);
    }

    [[nodiscard]] inline TabButtonLayout make_tab_button_layout(
        const TabButtonLayoutOptions& options,
        std::uint32_t index,
        bool active = false,
        bool closable = false) noexcept
    {
        return selection_control_controller().make_tab_button(
            options, index, active, closable);
    }

    [[nodiscard]] inline SliderLayout make_slider_layout(
        const SliderLayoutOptions& options) noexcept
    {
        return selection_control_controller().make_slider(options);
    }

    [[nodiscard]] inline float slider_value_from_position(
        const SliderLayoutOptions& options,
        float horizontal_position) noexcept
    {
        return selection_control_controller().slider_value_at(
            options, horizontal_position);
    }

    [[nodiscard]] inline ToggleSwitchLayout make_toggle_switch_layout(
        const ToggleSwitchLayoutOptions& options) noexcept
    {
        return selection_control_controller().make_toggle_switch(options);
    }

    inline constexpr std::size_t asset_grid_maximum_items = 4096;
    inline constexpr std::size_t asset_grid_maximum_visible_tiles = 512;
    inline constexpr std::size_t asset_grid_maximum_filter_bytes = 96;
    inline constexpr std::size_t asset_grid_maximum_searchable_field_bytes = 512;
    inline constexpr float asset_grid_maximum_tile_extent = 16384.0f;
    inline constexpr std::uint32_t invalid_asset_grid_source_index = 0xffffffffU;

    struct AssetGridItemId
    {
        std::uint64_t value{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return value != 0;
        }

        friend bool operator==(AssetGridItemId, AssetGridItemId) noexcept = default;
    };

    enum class AssetGridItemKind : std::uint8_t
    {
        generic,
        folder,
        image,
        texture,
        material,
        model,
        audio,
        scene,
        document
    };

    enum class AssetGridActivationRole : std::uint8_t
    {
        none,
        invoke,
        open_in_tab
    };

    struct AssetGridImageMetadata
    {
        std::uint64_t content_key{};
        std::uint32_t pixel_width{};
        std::uint32_t pixel_height{};
        ImageFitMode fit{ImageFitMode::contain};
        bool has_alpha{};
    };

    struct AssetGridItem
    {
        AssetGridItemId id{};
        std::string_view label{};
        std::string_view detail{};
        std::string_view search_terms{};
        AssetGridItemKind kind{AssetGridItemKind::generic};
        AssetGridActivationRole activation{AssetGridActivationRole::open_in_tab};
        std::optional<AssetGridImageMetadata> image{};
        bool enabled{true};
    };

    struct AssetGridFilterOptions
    {
        std::string_view query{};
        std::size_t maximum_results{asset_grid_maximum_items};
        bool include_disabled{true};
    };

    struct AssetGridFilterResult
    {
        std::vector<std::uint32_t> source_indices{};
        std::size_t inspected_count{};
        std::size_t matched_count{};
        std::size_t invalid_id_count{};
        std::size_t duplicate_id_count{};
        bool source_truncated{};
        bool results_truncated{};
        bool query_truncated{};
    };

    struct AssetGridLayoutOptions
    {
        Rect viewport{};
        Vec2 tile_extent{144.0f, 164.0f};
        Vec2 gap{8.0f, 8.0f};
        Vec2 padding{8.0f, 8.0f};
        float image_height{108.0f};
        float label_height{24.0f};
        float detail_height{18.0f};
        float scroll_offset{};
        std::size_t maximum_visible_tiles{asset_grid_maximum_visible_tiles};
        bool clear_selection_on_empty_press{true};
    };

    struct AssetGridState
    {
        std::optional<AssetGridItemId> selected_id{};
    };

    struct AssetGridInput
    {
        Vec2 pointer_position{};
        bool pointer_pressed{};
        bool pointer_activated{};
        bool context_requested{};
        bool activate_selected{};
        bool clear_selection{};
        bool pointer_present{};
        std::optional<AssetGridItemId> requested_selection{};
    };

    struct AssetGridTileLayout
    {
        AssetGridItemId id{};
        std::uint32_t source_index{invalid_asset_grid_source_index};
        std::uint32_t filtered_index{invalid_asset_grid_source_index};
        std::uint32_t row{};
        std::uint32_t column{};
        Rect tile{};
        ImageBoxLayout image{};
        Rect label{};
        Rect detail{};
        Rect selection_indicator{};
        AssetGridItemKind kind{AssetGridItemKind::generic};
        AssetGridActivationRole activation{AssetGridActivationRole::none};
        bool has_image{};
        bool image_metadata_valid{};
        bool enabled{};
        bool hovered{};
        bool selected{};
    };

    struct AssetGridLayout
    {
        Rect viewport{};
        Vec2 content_extent{};
        std::vector<AssetGridTileLayout> visible_tiles{};
        std::uint32_t column_count{};
        std::uint32_t row_count{};
        std::uint32_t item_count{};
        std::uint32_t first_visible_row{};
        std::uint32_t past_last_visible_row{};
        float scroll_offset{};
        float maximum_scroll_offset{};
        bool valid{};
        bool items_truncated{};
        bool visible_tiles_truncated{};
    };

    struct AssetGridView
    {
        AssetGridFilterResult filter{};
        AssetGridLayout layout{};
    };

    struct AssetGridUpdateResult
    {
        AssetGridView view{};
        std::optional<AssetGridItemId> selected_id{};
        std::optional<AssetGridItemId> activated_id{};
        std::optional<AssetGridItemId> context_requested_id{};
        AssetGridActivationRole activation{AssetGridActivationRole::none};
        std::uint32_t selected_source_index{invalid_asset_grid_source_index};
        std::uint32_t activated_source_index{invalid_asset_grid_source_index};
        std::uint32_t context_requested_source_index{
            invalid_asset_grid_source_index};
        bool selection_changed{};
        bool activation_requested{};
        bool context_request_valid{};
    };

    [[nodiscard]] inline char asset_grid_fold_ascii(char value) noexcept
    {
        return value >= 'A' && value <= 'Z'
            ? static_cast<char>(value + ('a' - 'A'))
            : value;
    }

    [[nodiscard]] inline bool asset_grid_text_matches_filter(
        std::string_view text,
        std::string_view query) noexcept
    {
        const std::size_t querySize = (std::min)(
            query.size(), asset_grid_maximum_filter_bytes);
        if (querySize == 0)
            return true;

        const std::size_t textSize = (std::min)(
            text.size(), asset_grid_maximum_searchable_field_bytes);
        if (querySize > textSize)
            return false;

        for (std::size_t start = 0; start + querySize <= textSize; ++start)
        {
            bool matches = true;
            for (std::size_t offset = 0; offset < querySize; ++offset)
            {
                if (asset_grid_fold_ascii(text[start + offset])
                    != asset_grid_fold_ascii(query[offset]))
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return true;
        }
        return false;
    }

    [[nodiscard]] inline bool asset_grid_item_matches_filter(
        const AssetGridItem& item,
        std::string_view query) noexcept
    {
        return asset_grid_text_matches_filter(item.label, query)
            || asset_grid_text_matches_filter(item.detail, query)
            || asset_grid_text_matches_filter(item.search_terms, query);
    }

    class AssetGridController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "Asset grid";
        }

        [[nodiscard]] AssetGridFilterResult filter(
            std::span<const AssetGridItem> items,
            const AssetGridFilterOptions& options) const
        {
            AssetGridFilterResult result{};
            const std::size_t inspectCount = (std::min)(
                items.size(), asset_grid_maximum_items);
            const std::size_t resultLimit = (std::min)(
                options.maximum_results, asset_grid_maximum_items);
            const std::string_view query = options.query.substr(
                0, (std::min)(options.query.size(), asset_grid_maximum_filter_bytes));

            result.source_indices.reserve((std::min)(inspectCount, resultLimit));
            result.inspected_count = inspectCount;
            result.source_truncated = items.size() > inspectCount;
            result.query_truncated = options.query.size() > query.size();

            std::vector<AssetGridItemId> acceptedIds{};
            acceptedIds.reserve(inspectCount);
            for (std::size_t index = 0; index < inspectCount; ++index)
            {
                const AssetGridItem& item = items[index];
                if (!item.id)
                {
                    ++result.invalid_id_count;
                    continue;
                }

                if (std::find(acceptedIds.begin(), acceptedIds.end(), item.id)
                    != acceptedIds.end())
                {
                    ++result.duplicate_id_count;
                    continue;
                }
                acceptedIds.push_back(item.id);

                if ((!options.include_disabled && !item.enabled)
                    || !asset_grid_item_matches_filter(item, query))
                {
                    continue;
                }

                ++result.matched_count;
                if (result.source_indices.size() < resultLimit)
                    result.source_indices.push_back(static_cast<std::uint32_t>(index));
                else
                    result.results_truncated = true;
            }
            return result;
        }

        [[nodiscard]] AssetGridLayout layout(
            std::span<const AssetGridItem> items,
            const AssetGridFilterResult& filtered,
            const AssetGridLayoutOptions& options,
            const AssetGridState& state,
            Vec2 pointerPosition,
            bool pointerPresent) const
        {
            AssetGridLayout result{};
            result.viewport = options.viewport;
            if (!std::isfinite(options.viewport.position.x)
                || !std::isfinite(options.viewport.position.y)
                || !std::isfinite(options.viewport.size.x)
                || !std::isfinite(options.viewport.size.y)
                || !std::isfinite(options.tile_extent.x)
                || !std::isfinite(options.tile_extent.y)
                || !std::isfinite(options.gap.x)
                || !std::isfinite(options.gap.y)
                || !std::isfinite(options.padding.x)
                || !std::isfinite(options.padding.y)
                || options.viewport.size.x <= 0.0f
                || options.viewport.size.y <= 0.0f
                || options.tile_extent.x <= 0.0f
                || options.tile_extent.y <= 0.0f
                || options.tile_extent.x > asset_grid_maximum_tile_extent
                || options.tile_extent.y > asset_grid_maximum_tile_extent)
            {
                return result;
            }

            const float gapX = (std::clamp)(
                options.gap.x, 0.0f, asset_grid_maximum_tile_extent);
            const float gapY = (std::clamp)(
                options.gap.y, 0.0f, asset_grid_maximum_tile_extent);
            const float paddingX = (std::clamp)(
                options.padding.x, 0.0f, options.viewport.size.x * 0.5f);
            const float paddingY = (std::clamp)(
                options.padding.y, 0.0f, options.viewport.size.y * 0.5f);
            const float availableWidth = (std::max)(
                0.0f, options.viewport.size.x - paddingX * 2.0f);
            const float columnStride = options.tile_extent.x + gapX;
            const float rowStride = options.tile_extent.y + gapY;
            const float requestedColumns = (availableWidth + gapX) / columnStride;
            const std::uint32_t columns = static_cast<std::uint32_t>(
                (std::clamp)(requestedColumns, 1.0f, 512.0f));
            const std::size_t itemCount = (std::min)(
                filtered.source_indices.size(), asset_grid_maximum_items);
            const std::uint32_t rows = itemCount == 0
                ? 0U
                : static_cast<std::uint32_t>((itemCount + columns - 1U) / columns);
            const float contentHeight = paddingY * 2.0f
                + (rows == 0 ? 0.0f
                    : static_cast<float>(rows) * options.tile_extent.y
                        + static_cast<float>(rows - 1U) * gapY);
            const float contentWidth = paddingX * 2.0f
                + static_cast<float>(columns) * options.tile_extent.x
                + static_cast<float>(columns - 1U) * gapX;
            const float maximumScroll = (std::max)(
                0.0f, contentHeight - options.viewport.size.y);
            const float scroll = std::isfinite(options.scroll_offset)
                ? (std::clamp)(options.scroll_offset, 0.0f, maximumScroll)
                : 0.0f;
            const std::uint32_t firstRow = rows == 0
                ? 0U
                : (std::min)(
                    rows,
                    static_cast<std::uint32_t>(
                        (std::max)(0.0f, scroll - paddingY) / rowStride));
            const float visibleBottom = scroll + options.viewport.size.y;
            const std::uint32_t pastLastRow = rows == 0
                ? 0U
                : (std::min)(
                    rows,
                    static_cast<std::uint32_t>(
                        ((std::max)(0.0f, visibleBottom - paddingY) / rowStride) + 1.0f));
            const std::size_t visibleLimit = (std::min)(
                options.maximum_visible_tiles, asset_grid_maximum_visible_tiles);

            result.content_extent = {contentWidth, contentHeight};
            result.column_count = columns;
            result.row_count = rows;
            result.item_count = static_cast<std::uint32_t>(itemCount);
            result.first_visible_row = firstRow;
            result.past_last_visible_row = pastLastRow;
            result.scroll_offset = scroll;
            result.maximum_scroll_offset = maximumScroll;
            result.valid = true;
            result.items_truncated =
                filtered.source_indices.size() > itemCount;
            result.visible_tiles.reserve((std::min)(itemCount, visibleLimit));

            for (std::uint32_t row = firstRow; row < pastLastRow; ++row)
            {
                for (std::uint32_t column = 0; column < columns; ++column)
                {
                    const std::size_t filteredIndex =
                        static_cast<std::size_t>(row) * columns + column;
                    if (filteredIndex >= itemCount)
                        break;
                    if (result.visible_tiles.size() >= visibleLimit)
                    {
                        result.visible_tiles_truncated = true;
                        return result;
                    }

                    const std::uint32_t sourceIndex = filtered.source_indices[filteredIndex];
                    if (sourceIndex >= items.size())
                        continue;
                    const AssetGridItem& item = items[sourceIndex];
                    const Rect tile{
                        {options.viewport.position.x + paddingX
                            + static_cast<float>(column) * columnStride,
                         options.viewport.position.y + paddingY
                            + static_cast<float>(row) * rowStride - scroll},
                        options.tile_extent};
                    const float imageHeight = (std::clamp)(
                        options.image_height, 0.0f, options.tile_extent.y);
                    const float remainingHeight = (std::max)(
                        0.0f, options.tile_extent.y - imageHeight);
                    const float labelHeight = (std::clamp)(
                        options.label_height, 0.0f, remainingHeight);
                    const float detailHeight = (std::clamp)(
                        options.detail_height, 0.0f, remainingHeight - labelHeight);

                    AssetGridTileLayout tileLayout{};
                    tileLayout.id = item.id;
                    tileLayout.source_index = sourceIndex;
                    tileLayout.filtered_index = static_cast<std::uint32_t>(filteredIndex);
                    tileLayout.row = row;
                    tileLayout.column = column;
                    tileLayout.tile = tile;
                    tileLayout.label = {
                        {tile.position.x, tile.position.y + imageHeight},
                        {tile.size.x, labelHeight}};
                    tileLayout.detail = {
                        {tile.position.x, tile.position.y + imageHeight + labelHeight},
                        {tile.size.x, detailHeight}};
                    tileLayout.selection_indicator = {
                        {tile.position.x, tile.position.y + tile.size.y - 3.0f},
                        {tile.size.x, 3.0f}};
                    tileLayout.kind = item.kind;
                    tileLayout.activation = item.activation;
                    tileLayout.has_image = item.image.has_value();
                    tileLayout.enabled = item.enabled;
                    tileLayout.hovered = pointerPresent
                        && item.enabled
                        && contains(tile, pointerPosition);
                    tileLayout.selected = state.selected_id.has_value()
                        && state.selected_id.value() == item.id;
                    Vec2 sourceExtent{};
                    ImageFitMode imageFit = ImageFitMode::contain;
                    if (item.image.has_value())
                    {
                        const AssetGridImageMetadata& metadata = item.image.value();
                        tileLayout.image_metadata_valid = metadata.pixel_width > 0
                            && metadata.pixel_height > 0;
                        sourceExtent = {
                            static_cast<float>(metadata.pixel_width),
                            static_cast<float>(metadata.pixel_height)};
                        imageFit = metadata.fit;
                    }
                    if (imageHeight > 0.0f)
                    {
                        tileLayout.image = make_image_box_layout({
                            .bounds = {tile.position, {tile.size.x, imageHeight}},
                            .source_extent = sourceExtent,
                            .fit = imageFit,
                            .padding = 4.0f});
                    }
                    result.visible_tiles.push_back(tileLayout);
                }
            }
            return result;
        }

        [[nodiscard]] AssetGridUpdateResult update(
            AssetGridState& state,
            std::span<const AssetGridItem> items,
            const AssetGridFilterOptions& filterOptions,
            const AssetGridLayoutOptions& layoutOptions,
            const AssetGridInput& input) const
        {
            AssetGridUpdateResult result{};
            result.view.filter = filter(items, filterOptions);

            const std::size_t inspectCount = (std::min)(
                items.size(), asset_grid_maximum_items);
            auto sourceIndexFor = [&](AssetGridItemId id) noexcept
                -> std::uint32_t
            {
                if (!id)
                    return invalid_asset_grid_source_index;
                for (std::size_t index = 0; index < inspectCount; ++index)
                {
                    if (items[index].id == id)
                        return static_cast<std::uint32_t>(index);
                }
                return invalid_asset_grid_source_index;
            };

            if (state.selected_id.has_value()
                && sourceIndexFor(state.selected_id.value()) == invalid_asset_grid_source_index)
            {
                state.selected_id.reset();
                result.selection_changed = true;
            }
            if (input.clear_selection && state.selected_id.has_value())
            {
                state.selected_id.reset();
                result.selection_changed = true;
            }
            if (input.requested_selection.has_value())
            {
                const std::uint32_t requestedIndex = sourceIndexFor(
                    input.requested_selection.value());
                if (requestedIndex != invalid_asset_grid_source_index
                    && items[requestedIndex].enabled
                    && state.selected_id != input.requested_selection)
                {
                    state.selected_id = input.requested_selection;
                    result.selection_changed = true;
                }
            }

            result.view.layout = layout(
                items,
                result.view.filter,
                layoutOptions,
                state,
                input.pointer_position,
                input.pointer_present
                    || input.pointer_pressed
                    || input.pointer_activated
                    || input.context_requested);
            const AssetGridTileLayout* hitTile = nullptr;
            for (const AssetGridTileLayout& tile : result.view.layout.visible_tiles)
            {
                if (tile.enabled && contains(tile.tile, input.pointer_position))
                {
                    hitTile = &tile;
                    break;
                }
            }

            if (input.pointer_pressed)
            {
                if (hitTile != nullptr)
                {
                    if (!state.selected_id.has_value()
                        || state.selected_id.value() != hitTile->id)
                    {
                        state.selected_id = hitTile->id;
                        result.selection_changed = true;
                    }
                }
                else if (layoutOptions.clear_selection_on_empty_press
                    && contains(layoutOptions.viewport, input.pointer_position)
                    && state.selected_id.has_value())
                {
                    state.selected_id.reset();
                    result.selection_changed = true;
                }
            }

            if (input.context_requested && hitTile != nullptr)
            {
                if (!state.selected_id.has_value()
                    || state.selected_id.value() != hitTile->id)
                {
                    state.selected_id = hitTile->id;
                    result.selection_changed = true;
                }
                result.context_requested_id = hitTile->id;
                result.context_requested_source_index = hitTile->source_index;
                result.context_request_valid = true;
            }

            if (input.pointer_activated && hitTile != nullptr
                && hitTile->activation != AssetGridActivationRole::none)
            {
                if (!state.selected_id.has_value()
                    || state.selected_id.value() != hitTile->id)
                {
                    result.selection_changed = true;
                }
                state.selected_id = hitTile->id;
                result.activated_id = hitTile->id;
                result.activated_source_index = hitTile->source_index;
                result.activation = hitTile->activation;
                result.activation_requested = true;
            }
            else if (input.activate_selected && state.selected_id.has_value())
            {
                const std::uint32_t selectedIndex = sourceIndexFor(
                    state.selected_id.value());
                if (selectedIndex != invalid_asset_grid_source_index
                    && items[selectedIndex].enabled
                    && items[selectedIndex].activation != AssetGridActivationRole::none)
                {
                    result.activated_id = state.selected_id;
                    result.activated_source_index = selectedIndex;
                    result.activation = items[selectedIndex].activation;
                    result.activation_requested = true;
                }
            }

            result.selected_id = state.selected_id;
            if (state.selected_id.has_value())
            {
                result.selected_source_index = sourceIndexFor(state.selected_id.value());
                for (AssetGridTileLayout& tile : result.view.layout.visible_tiles)
                    tile.selected = tile.id == state.selected_id.value();
            }
            return result;
        }
    };

    [[nodiscard]] inline const AssetGridController& asset_grid_controller() noexcept
    {
        static const AssetGridController controller{};
        return controller;
    }

    [[nodiscard]] inline AssetGridFilterResult filter_asset_grid_items(
        std::span<const AssetGridItem> items,
        const AssetGridFilterOptions& options = {})
    {
        return asset_grid_controller().filter(items, options);
    }

    [[nodiscard]] inline AssetGridLayout make_asset_grid_layout(
        std::span<const AssetGridItem> items,
        const AssetGridFilterResult& filtered,
        const AssetGridLayoutOptions& options,
        const AssetGridState& state = {},
        Vec2 pointerPosition = {},
        bool pointerPresent = false)
    {
        return asset_grid_controller().layout(
            items, filtered, options, state, pointerPosition, pointerPresent);
    }

    [[nodiscard]] inline AssetGridUpdateResult update_asset_grid(
        AssetGridState& state,
        std::span<const AssetGridItem> items,
        const AssetGridFilterOptions& filterOptions,
        const AssetGridLayoutOptions& layoutOptions,
        const AssetGridInput& input = {})
    {
        return asset_grid_controller().update(
            state, items, filterOptions, layoutOptions, input);
    }
    enum class PopupPlacement : std::uint8_t
    {
        below,
        above,
        right,
        left,
        centered,
        cursor
    };

    struct PopupState
    {
        Rect rect{};
        PopupPlacement placement{ PopupPlacement::below };
        bool open{};
        bool initialized{};
        bool pressed_inside{};
        bool pressed_owner{};
        std::uint32_t focus_order{};
    };

    struct PopupOptions
    {
        Rect owner{};
        Vec2 preferred_size{ 240.0f, 160.0f };
        Vec2 viewport_size{};
        Vec2 cursor_offset{ 12.0f, 12.0f };
        PopupPlacement placement{ PopupPlacement::below };
        float gap{ 4.0f };
        float margin{ 4.0f };
        bool flip_to_fit{ true };
        bool clamp_to_viewport{ true };
        bool close_on_outside_press{ true };
    };

    struct PopupInput
    {
        Vec2 mouse_position{};
        bool mouse_pressed{};
        bool mouse_released{};
        bool open_requested{};
        bool toggle_requested{};
        bool close_requested{};
        bool owner_pressed{};
        bool escape_pressed{};
    };

    struct PopupLayout
    {
        Rect popup{};
        Rect owner{};
        PopupPlacement placement{ PopupPlacement::below };
        bool visible{};
        bool hovered{};
        bool owner_hovered{};
        bool opened{};
        bool closed{};
        bool flipped{};
        bool clamped{};
    };

    class PopupLayoutController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] Rect place(
            const PopupOptions& options,
            const PopupInput& input,
            PopupPlacement* used_placement = nullptr,
            bool* flipped = nullptr,
            bool* clamped = nullptr) const noexcept;
        void normalize(PopupState& state, const PopupOptions& options, const PopupInput& input) const noexcept;
        [[nodiscard]] PopupLayout update(PopupState& state, const PopupOptions& options, const PopupInput& input) const noexcept;
    };

    [[nodiscard]] const PopupLayoutController& popup_layout_controller() noexcept;
    [[nodiscard]] Rect place_popup(
        const PopupOptions& options,
        const PopupInput& input,
        PopupPlacement* used_placement = nullptr,
        bool* flipped = nullptr,
        bool* clamped = nullptr) noexcept;
    void normalize_popup(PopupState& state, const PopupOptions& options, const PopupInput& input) noexcept;
    [[nodiscard]] PopupLayout update_popup(PopupState& state, const PopupOptions& options, const PopupInput& input) noexcept;

    enum class DockSlot : std::uint8_t
    {
        none,
        left,
        right,
        top,
        bottom,
        center
    };

    enum class DockGuideTarget : std::uint8_t
    {
        none,
        left_tabs,
        right_tabs,
        bottom_left_tabs,
        bottom_right_tabs,
        left_context,
        right_context,
        float_window
    };

    struct DockGuideOptions
    {
        Rect guide_bounds{};
        Rect left_tabs_preview{};
        Rect right_tabs_preview{};
        Rect bottom_left_tabs_preview{};
        Rect bottom_right_tabs_preview{};
        Rect left_context_preview{};
        Rect right_context_preview{};
        Rect floating_preview{};
        Vec2 pointer{};
        float guide_extent{ 94.0f };
        float guide_gap{ 8.0f };
        bool allow_side_tabs{ true };
        bool allow_bottom_tabs{ true };
        bool allow_contexts{};
        bool allow_float{ true };
        bool center_context_guides_in_previews{};
    };

    struct DockGuide
    {
        DockGuideTarget target{ DockGuideTarget::none };
        Rect target_bounds{};
        Rect preview_bounds{};
        bool hovered{};
    };

    struct DockGuideLayout
    {
        DockGuide guides[7]{};
        std::uint32_t count{};
        DockGuideTarget hovered_target{ DockGuideTarget::none };
        Rect hovered_preview{};
    };

    inline constexpr std::uint32_t maximum_dock_tabs{ 64U };
    inline constexpr std::uint32_t invalid_dock_tab_index{
        (std::numeric_limits<std::uint32_t>::max)()
    };

    struct DockTabItem
    {
        std::uint64_t id{};
        std::uint64_t remembered_group_id{};
        std::uint32_t keyboard_order{};
        bool active{};
        bool closable{ true };
    };

    struct DockTabGroup
    {
        std::uint64_t id{};
        DockTabItem tabs[maximum_dock_tabs]{};
        std::uint32_t count{};
    };

    struct DockTabStripOptions
    {
        Rect strip_bounds{};
        const Rect* tab_bounds{};
        std::uint32_t tab_count{};
        Vec2 pointer{};
        Vec2 dragged_tab_size{ 120.0f, 28.0f };
        std::uint64_t source_group_id{};
        std::uint64_t target_group_id{};
        std::uint32_t source_index{ invalid_dock_tab_index };
        float marker_extent{ 3.0f };
        bool drag_active{};
        bool target_compatible{ true };
        bool cancelled{};
    };

    struct DockTabStripLayout
    {
        Rect insertion_marker{};
        Rect insertion_ghost{};
        std::uint32_t insertion_index{ invalid_dock_tab_index };
        bool target_hovered{};
        bool direct_drop_available{};
        bool suppress_outer_guides{};
        bool no_op{};
        bool cancelled{};
    };

    enum class DockTabMoveCode : std::uint8_t
    {
        moved,
        unchanged,
        invalid_group,
        invalid_source,
        invalid_insertion,
        duplicate_tab,
        target_full
    };

    struct DockTabMoveResult
    {
        DockTabMoveCode code{ DockTabMoveCode::invalid_source };
        std::uint32_t source_index{ invalid_dock_tab_index };
        std::uint32_t target_index{ invalid_dock_tab_index };
        std::uint64_t active_tab_id{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return code == DockTabMoveCode::moved
                || code == DockTabMoveCode::unchanged;
        }
    };

    [[nodiscard]] DockTabStripLayout make_dock_tab_strip_layout(
        const DockTabStripOptions& options) noexcept;
    [[nodiscard]] DockTabMoveResult move_dock_tab(
        DockTabGroup& source,
        DockTabGroup& target,
        std::uint32_t source_index,
        std::uint32_t insertion_index) noexcept;

    struct DockPaneState
    {
        std::uint32_t id{};
        DockSlot slot{ DockSlot::center };
        Rect docked_rect{};
        Rect popout_rect{};
        Vec2 min_size{ 120.0f, 80.0f };
        float weight{ 1.0f };
        bool visible{ true };
        bool initialized{};
        bool popped_out{};
        bool popout_context_open{};
        bool active{};
        std::uint32_t focus_order{};
    };

    struct DockLayoutState
    {
        std::uint32_t active_pane_id{};
        std::uint32_t next_focus_order{ 1 };
        bool initialized{};
    };

    struct DockLayoutOptions
    {
        Rect workspace{};
        Vec2 min_pane_size{ 96.0f, 64.0f };
        Vec2 default_popout_size{ 360.0f, 260.0f };
        float edge_fraction{ 0.24f };
        float title_bar_height{ 28.0f };
        float content_padding{ 6.0f };
        float popout_spacing{ 24.0f };
        float visible_margin{ 48.0f };
    };

    struct DockLayoutInput
    {
        Vec2 mouse_position{};
        bool mouse_pressed{};
        bool mouse_released{};
        std::uint32_t activate_pane_id{};
        std::uint32_t popout_pane_id{};
        std::uint32_t redock_pane_id{};
        std::uint32_t toggle_popout_pane_id{};
        DockSlot redock_slot{ DockSlot::none };
    };

    struct DockPaneLayout
    {
        std::uint32_t id{};
        DockSlot slot{ DockSlot::center };
        Rect frame{};
        Rect title_bar{};
        Rect content{};
        bool visible{};
        bool hovered{};
        bool docked{};
        bool popped_out{};
        bool context_window_requested{};
        bool active{};
    };

    struct DockLayoutResult
    {
        bool changed{};
        bool focus_changed{};
        bool context_windows_changed{};
        std::uint32_t active_pane_id{};
        std::uint32_t context_window_count{};
    };

    class DockLayoutController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] bool is_valid_slot(DockSlot slot) const noexcept;
        [[nodiscard]] bool pane_requests_context_window(const DockPaneState& pane) const noexcept;
        [[nodiscard]] DockPaneLayout make_pane_layout(
            const DockPaneState& pane,
            const DockLayoutOptions& options,
            const DockLayoutInput& input) const noexcept;
        void activate_pane(
            DockLayoutState& state,
            DockPaneState* panes,
            std::uint32_t pane_count,
            std::uint32_t pane_id) const noexcept;
        void normalize(
            DockLayoutState& state,
            DockPaneState* panes,
            std::uint32_t pane_count,
            const DockLayoutOptions& options) const noexcept;
        [[nodiscard]] DockLayoutResult update(
            DockLayoutState& state,
            DockPaneState* panes,
            std::uint32_t pane_count,
            const DockLayoutOptions& options,
            const DockLayoutInput& input) const noexcept;
    };

    [[nodiscard]] const DockLayoutController& dock_layout_controller() noexcept;
    [[nodiscard]] bool is_valid_dock_slot(DockSlot slot) noexcept;
    [[nodiscard]] DockGuideLayout make_dock_guide_layout(
        const DockGuideOptions& options) noexcept;
    [[nodiscard]] bool dock_pane_requests_context_window(const DockPaneState& pane) noexcept;
    [[nodiscard]] DockPaneLayout make_dock_pane_layout(
        const DockPaneState& pane,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) noexcept;
    void activate_dock_pane(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        std::uint32_t pane_id) noexcept;
    void normalize_dock_layout(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options) noexcept;
    [[nodiscard]] DockLayoutResult update_dock_layout(
        DockLayoutState& state,
        DockPaneState* panes,
        std::uint32_t pane_count,
        const DockLayoutOptions& options,
        const DockLayoutInput& input) noexcept;

    enum class DockableWindowMode : std::uint8_t
    {
        docked,
        floating,
        detached
    };

    enum class DockableWindowAction : std::uint8_t
    {
        none,
        focus,
        dock,
        float_window,
        detach,
        close
    };

    struct DockableWindowHostState
    {
        std::uint32_t active_window_id{};
        std::uint32_t next_focus_order{ 1 };
        bool changed_this_frame{};
    };

    struct DockableWindowState
    {
        std::uint32_t id{};
        DockableWindowMode mode{ DockableWindowMode::floating };
        DockSlot dock_slot{ DockSlot::right };
        FloatingWindowState floating{};
        bool visible{ true };
        bool initialized{};
        bool active{};
        bool detach_requested{};
        bool close_requested{};
        std::uint32_t focus_order{};
    };

    struct DockableWindowOptions
    {
        std::string_view title{};
        Rect docked_frame{};
        FloatingWindowOptions floating{};
        Vec2 viewport_size{};
        float title_bar_height{ 30.0f };
        float content_padding{ 6.0f };
        float action_button_width{ 72.0f };
        float action_button_gap{ 4.0f };
        bool allow_dock{ true };
        bool allow_float{ true };
        bool allow_detach{ true };
        bool allow_close{ true };
        DockSlot fallback_dock_slot{ DockSlot::right };
    };

    struct DockableWindowInput
    {
        Vec2 mouse_position{};
        bool mouse_down{};
        bool mouse_pressed{};
        bool mouse_released{};
        DockableWindowAction requested_action{ DockableWindowAction::none };
        DockSlot requested_dock_slot{ DockSlot::none };
    };

    struct DockableWindowChrome
    {
        Rect frame{};
        Rect title_bar{};
        Rect content{};
        Rect dock_button{};
        Rect float_button{};
        Rect detach_button{};
        Rect close_button{};
        bool visible{};
        bool hovered{};
        bool title_hovered{};
        bool dock_hovered{};
        bool float_hovered{};
        bool detach_hovered{};
        bool close_hovered{};
        bool active{};
    };

    struct DockableWindowResult
    {
        DockableWindowChrome chrome{};
        DockableWindowMode mode{ DockableWindowMode::floating };
        DockableWindowAction action{ DockableWindowAction::none };
        DockSlot dock_slot{ DockSlot::none };
        bool changed{};
        bool focused{};
        bool dock_requested{};
        bool float_requested{};
        bool detach_requested{};
        bool close_requested{};
    };

    class DockableWindowController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void focus(
            DockableWindowHostState& host,
            DockableWindowState& state) const noexcept;
        void normalize(
            DockableWindowHostState& host,
            DockableWindowState& state,
            const DockableWindowOptions& options) const noexcept;
        [[nodiscard]] DockableWindowChrome make_chrome(
            const DockableWindowState& state,
            const DockableWindowOptions& options,
            const DockableWindowInput& input) const noexcept;
        [[nodiscard]] DockableWindowResult update(
            DockableWindowHostState& host,
            DockableWindowState& state,
            const DockableWindowOptions& options,
            const DockableWindowInput& input) const noexcept;
    };

    [[nodiscard]] const DockableWindowController& dockable_window_controller() noexcept;
    void focus_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state) noexcept;
    void normalize_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options) noexcept;
    [[nodiscard]] DockableWindowChrome make_dockable_window_chrome(
        const DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) noexcept;
    [[nodiscard]] DockableWindowResult update_dockable_window(
        DockableWindowHostState& host,
        DockableWindowState& state,
        const DockableWindowOptions& options,
        const DockableWindowInput& input) noexcept;

    inline constexpr std::size_t invalid_text_index = (std::numeric_limits<std::size_t>::max)();

    enum class TextControlCommand : std::uint8_t
    {
        none,
        set_caret,
        move_left,
        move_right,
        move_up,
        move_down,
        move_line_start,
        move_line_end,
        move_document_start,
        move_document_end,
        move_word_left,
        move_word_right,
        select_all,
        erase_backward,
        erase_forward,
        insert_text,
        copy_selection,
        cut_selection,
        paste_text
    };

    struct TextSelectionRange
    {
        std::size_t first{};
        std::size_t past_last{};

        [[nodiscard]] bool empty() const noexcept
        {
            return first == past_last;
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return past_last - first;
        }
    };

    struct TextPosition
    {
        std::size_t byte_index{};
        std::size_t line{};
        std::size_t column{};
    };

    struct TextControlState
    {
        std::string text{};
        std::size_t anchor{};
        std::size_t caret{};
        std::size_t preferred_column{ invalid_text_index };
        Vec2 scroll{};
        bool focused{};
        bool changed_this_frame{};
    };

    struct TextControlOptions
    {
        Vec2 viewport_size{};
        Vec2 content_padding{ 4.0f, 4.0f };
        std::size_t maximum_bytes{};
        bool multiline{ true };
        bool read_only{};
        bool accept_tab{};
    };

    struct TextControlMetrics
    {
        Vec2 content_size{};
        Vec2 caret_position{};
        Vec2 caret_size{ 1.0f, 20.0f };
        bool valid{};
    };

    struct TextControlInput
    {
        TextControlCommand command{ TextControlCommand::none };
        std::string_view text{};
        std::size_t requested_caret{ invalid_text_index };
        Vec2 scroll_delta{};
        TextControlMetrics metrics{};
        bool extend_selection{};
        bool focus_requested{};
        bool blur_requested{};
    };

    struct TextControlResult
    {
        TextSelectionRange selection{};
        TextPosition caret{};
        std::string clipboard_text{};
        Vec2 scroll{};
        bool changed{};
        bool text_changed{};
        bool selection_changed{};
        bool focus_changed{};
        bool scroll_changed{};
        bool clipboard_write_requested{};
    };

    class TextControlController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void normalize(TextControlState& state) const noexcept;
        [[nodiscard]] TextSelectionRange selection(const TextControlState& state) const noexcept;
        [[nodiscard]] TextPosition position(const TextControlState& state) const noexcept;
        [[nodiscard]] std::string selected_text(const TextControlState& state) const;
        [[nodiscard]] bool replace_selection(
            TextControlState& state,
            const TextControlOptions& options,
            std::string_view text) const;
        void update_scroll(
            TextControlState& state,
            const TextControlOptions& options,
            const TextControlMetrics& metrics,
            Vec2 scroll_delta = {}) const noexcept;
        [[nodiscard]] TextControlResult update(
            TextControlState& state,
            const TextControlOptions& options,
            const TextControlInput& input) const;
    };

    [[nodiscard]] const TextControlController& text_control_controller() noexcept;
    void normalize_text_control(TextControlState& state) noexcept;
    [[nodiscard]] TextSelectionRange text_selection(const TextControlState& state) noexcept;
    [[nodiscard]] TextPosition text_position(const TextControlState& state) noexcept;
    [[nodiscard]] std::string selected_text(const TextControlState& state);
    [[nodiscard]] bool replace_text_selection(
        TextControlState& state,
        const TextControlOptions& options,
        std::string_view text);
    void update_text_control_scroll(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlMetrics& metrics,
        Vec2 scroll_delta = {}) noexcept;
    [[nodiscard]] TextControlResult update_text_control(
        TextControlState& state,
        const TextControlOptions& options,
        const TextControlInput& input);

    struct TextEditorLineRange
    {
        std::size_t first{};
        std::size_t past_last{};

        [[nodiscard]] bool empty() const noexcept
        {
            return first == past_last;
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return past_last - first;
        }
    };

    struct TextEditorState
    {
        TextControlState text{};
        std::vector<std::size_t> line_starts{ 0u };
        std::string find_query{};
        TextEditorLineRange active_match{};
        std::size_t find_cursor{};
        std::uint64_t revision{ 1u };
        bool dirty{};
    };

    struct TextEditorSnapshot
    {
        TextSelectionRange selection{};
        TextPosition caret{};
        TextEditorLineRange active_match{};
        std::size_t line_count{ 1u };
        std::uint64_t revision{ 1u };
        bool dirty{};
    };

    struct TextEditorResult
    {
        TextControlResult text{};
        TextEditorSnapshot snapshot{};
        bool document_changed{};
        bool find_changed{};
        bool find_wrapped{};
    };

    class TextEditorController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void normalize(TextEditorState& state) const;
        void set_text(TextEditorState& state, std::string value) const;
        [[nodiscard]] TextEditorResult update(
            TextEditorState& state,
            const TextControlOptions& options,
            const TextControlInput& input) const;
        [[nodiscard]] TextEditorSnapshot snapshot(const TextEditorState& state) const noexcept;
        [[nodiscard]] TextEditorLineRange line_range(
            const TextEditorState& state,
            std::size_t line) const noexcept;
        [[nodiscard]] std::string_view line(
            const TextEditorState& state,
            std::size_t line) const noexcept;
        [[nodiscard]] bool find_next(
            TextEditorState& state,
            std::string_view query,
            bool case_sensitive = true) const;
        [[nodiscard]] bool replace_active_match(
            TextEditorState& state,
            const TextControlOptions& options,
            std::string_view replacement) const;
        void mark_saved(TextEditorState& state) const noexcept;
    };

    [[nodiscard]] const TextEditorController& text_editor_controller() noexcept;
    void normalize_text_editor(TextEditorState& state);
    void set_text_editor_text(TextEditorState& state, std::string value);
    [[nodiscard]] TextEditorResult update_text_editor(
        TextEditorState& state,
        const TextControlOptions& options,
        const TextControlInput& input);
    [[nodiscard]] TextEditorSnapshot text_editor_snapshot(
        const TextEditorState& state) noexcept;
    [[nodiscard]] TextEditorLineRange text_editor_line_range(
        const TextEditorState& state,
        std::size_t line) noexcept;
    [[nodiscard]] std::string_view text_editor_line(
        const TextEditorState& state,
        std::size_t line) noexcept;
    [[nodiscard]] bool find_next_text_editor_match(
        TextEditorState& state,
        std::string_view query,
        bool case_sensitive = true);
    [[nodiscard]] bool replace_text_editor_match(
        TextEditorState& state,
        const TextControlOptions& options,
        std::string_view replacement);
    void mark_text_editor_saved(TextEditorState& state) noexcept;
    enum class PanelHostMode : std::uint8_t
    {
        docked,
        floating,
        popup,
        external_host
    };

    enum class PanelHostAction : std::uint8_t
    {
        none,
        focus,
        dock,
        float_panel,
        show_popup,
        request_external_host,
        redock_from_external_host,
        close
    };

    struct PanelHostState
    {
        std::uint32_t id{};
        PanelHostMode mode{ PanelHostMode::docked };
        DockSlot dock_slot{ DockSlot::right };
        Rect docked_frame{};
        FloatingWindowState floating{};
        PopupState popup{};
        Rect external_frame{};
        bool visible{ true };
        bool initialized{};
        bool active{};
        bool external_host_requested{};
        bool external_host_active{};
        std::uint64_t external_host_token{};
        std::uint32_t focus_order{};
    };

    struct PanelHostOptions
    {
        std::string_view title{};
        Rect docked_frame{};
        FloatingWindowOptions floating{};
        PopupOptions popup{};
        Rect default_external_frame{};
        DockSlot fallback_dock_slot{ DockSlot::right };
        bool allow_dock{ true };
        bool allow_float{ true };
        bool allow_popup{ true };
        bool allow_external_host{ true };
        bool allow_close{ true };
    };

    struct PanelHostInput
    {
        DockableWindowHostState* focus_host{};
        Vec2 mouse_position{};
        bool mouse_down{};
        bool mouse_pressed{};
        bool mouse_released{};
        PanelHostAction requested_action{ PanelHostAction::none };
        DockSlot requested_dock_slot{ DockSlot::none };
        std::uint64_t external_host_token{};
        bool external_host_confirmed{};
        bool external_host_closed{};
        bool escape_pressed{};
    };

    struct PanelHostMetadata
    {
        std::uint32_t id{};
        PanelHostMode mode{ PanelHostMode::docked };
        DockSlot dock_slot{ DockSlot::none };
        Rect frame{};
        bool visible{};
        bool active{};
        bool wants_external_host{};
        bool external_host_active{};
        std::uint64_t external_host_token{};
    };

    struct PanelHostResult
    {
        PanelHostMetadata metadata{};
        PanelHostAction action{ PanelHostAction::none };
        bool changed{};
        bool focus_changed{};
        bool placement_changed{};
        bool external_host_changed{};
        bool close_requested{};
    };

    class PanelHostController final : public LayoutController
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override;
        void focus(PanelHostState& state, DockableWindowHostState* host = nullptr) const noexcept;
        void normalize(PanelHostState& state, const PanelHostOptions& options) const noexcept;
        [[nodiscard]] PanelHostMetadata metadata(const PanelHostState& state) const noexcept;
        [[nodiscard]] PanelHostResult update(
            PanelHostState& state,
            const PanelHostOptions& options,
            const PanelHostInput& input) const noexcept;
    };

    [[nodiscard]] const PanelHostController& panel_host_controller() noexcept;
    void focus_panel_host(
        PanelHostState& state,
        DockableWindowHostState* host = nullptr) noexcept;
    void normalize_panel_host(
        PanelHostState& state,
        const PanelHostOptions& options) noexcept;
    [[nodiscard]] PanelHostMetadata panel_host_metadata(
        const PanelHostState& state) noexcept;
    [[nodiscard]] PanelHostResult update_panel_host(
        PanelHostState& state,
        const PanelHostOptions& options,
        const PanelHostInput& input) noexcept;
}

//export namespace epochengine::gui
//{
//    // Returns true if the close button in a panel titlebar is clicked.
//    // panel_pos: Top-left position of the panel.
//    // panel_size: Size of the panel.
//    inline bool titlebar_close_button(Vec2 panel_pos, Vec2 panel_size)
//    {
//        // Define close button size and position (right side of titlebar)
//        constexpr float close_btn_size = 24.0f;
//        constexpr float close_btn_margin = 4.0f;
//        Vec2 btn_pos{
//            panel_pos.x + panel_size.x - close_btn_size - close_btn_margin,
//            panel_pos.y + close_btn_margin
//        };
//        Vec2 mouse = /* You must provide a way to get the mouse position here, e.g. from your input system */;
//        bool mouse_down = /* You must provide a way to check mouse button state here */;
//
//        // Simple rectangle hit test
//        bool hovered = mouse.x >= btn_pos.x && mouse.x <= btn_pos.x + close_btn_size &&
//                       mouse.y >= btn_pos.y && mouse.y <= btn_pos.y + close_btn_size;
//
//        static bool was_down = false;
//        bool pressed = false;
//        if (hovered && mouse_down && !was_down)
//            pressed = true;
//        was_down = mouse_down;
//
//        // Optionally: draw the button here using your rendering system
//
//        return pressed;
//    }
//}
