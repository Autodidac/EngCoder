#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

import epoch.gui;

namespace
{
    using namespace epochengine::gui_lib;

    int check(bool condition, int line)
    {
        return condition ? 0 : line;
    }

    [[nodiscard]] bool approximately(float left, float right) noexcept
    {
        return std::abs(left - right) <= 0.001f;
    }

#define EPOCHGUI_CHECK(condition) \
    do { const int failure = check((condition), __LINE__); if (failure != 0) return failure; } while (false)

    int replacement_and_clipboard()
    {
        TextControlState state{ .text = "hello world", .anchor = 6, .caret = 11 };
        TextControlOptions options{};
        TextControlResult result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::insert_text, .text = "Epoch" });
        EPOCHGUI_CHECK(result.text_changed);
        EPOCHGUI_CHECK(state.text == "hello Epoch");
        EPOCHGUI_CHECK(state.caret == state.text.size());

        state.anchor = 6;
        state.caret = state.text.size();
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::copy_selection });
        EPOCHGUI_CHECK(result.clipboard_write_requested);
        EPOCHGUI_CHECK(result.clipboard_text == "Epoch");
        EPOCHGUI_CHECK(state.text == "hello Epoch");

        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::cut_selection });
        EPOCHGUI_CHECK(result.clipboard_text == "Epoch");
        EPOCHGUI_CHECK(state.text == "hello ");
        return 0;
    }

    int utf8_and_limits()
    {
        TextControlState state{ .text = "A\xc3\xa9" "B", .anchor = 3, .caret = 3 };
        TextControlOptions options{};
        TextControlResult result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::erase_backward });
        EPOCHGUI_CHECK(result.text_changed);
        EPOCHGUI_CHECK(state.text == "AB");
        EPOCHGUI_CHECK(state.caret == 1);

        state = TextControlState{ .text = "ab", .anchor = 2, .caret = 2 };
        options.maximum_bytes = 4;
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::insert_text, .text = "\xc3\xa9" "X" });
        EPOCHGUI_CHECK(state.text == "ab\xc3\xa9");
        EPOCHGUI_CHECK(state.text.size() == 4);
        return 0;
    }

    int navigation_and_selection()
    {
        TextControlState state{ .text = "one\n12\nabcdef", .anchor = 2, .caret = 2 };
        TextControlOptions options{};
        TextControlResult result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::move_down });
        EPOCHGUI_CHECK(result.caret.line == 1);
        EPOCHGUI_CHECK(result.caret.column == 2);
        EPOCHGUI_CHECK(state.caret == 6);

        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::move_down });
        EPOCHGUI_CHECK(result.caret.line == 2);
        EPOCHGUI_CHECK(result.caret.column == 2);
        EPOCHGUI_CHECK(state.caret == 9);

        state = TextControlState{ .text = "alpha  beta", .anchor = 11, .caret = 11 };
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::move_word_left });
        EPOCHGUI_CHECK(state.caret == 7);
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::move_word_left, .extend_selection = true });
        EPOCHGUI_CHECK(text_selection(state).first == 0);
        EPOCHGUI_CHECK(selected_text(state) == "alpha  ");
        return 0;
    }

    int filtering_read_only_and_scroll()
    {
        TextControlState state{};
        TextControlOptions options{ .viewport_size = { 100.0f, 40.0f }, .multiline = false };
        TextControlResult result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::paste_text, .text = "a\r\nb\tc" });
        EPOCHGUI_CHECK(state.text == "abc");

        state.anchor = 0;
        state.caret = state.text.size();
        options.read_only = true;
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::cut_selection });
        EPOCHGUI_CHECK(state.text == "abc");
        EPOCHGUI_CHECK(!result.clipboard_write_requested);

        options.read_only = false;
        options.multiline = true;
        result = update_text_control(
            state,
            options,
            TextControlInput{
                .metrics = TextControlMetrics{
                    .content_size = { 200.0f, 100.0f },
                    .caret_position = { 150.0f, 70.0f },
                    .caret_size = { 2.0f, 20.0f },
                    .valid = true } });
        EPOCHGUI_CHECK(result.scroll_changed);
        EPOCHGUI_CHECK(state.scroll.x == 60.0f);
        EPOCHGUI_CHECK(state.scroll.y == 58.0f);
        return 0;
    }

    int whitespace_and_control_filtering()
    {
        TextControlState state{ .text = "A\xc3\xa9 B", .anchor = 5, .caret = 5 };
        TextControlOptions options{};
        TextControlResult result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::erase_backward });
        EPOCHGUI_CHECK(result.text_changed);
        EPOCHGUI_CHECK(state.text == "A\xc3\xa9 ");
        EPOCHGUI_CHECK(state.caret == 4);

        // Backspace is an edit command; a duplicate control byte from the text lane is ignored.
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::insert_text, .text = "\b" });
        EPOCHGUI_CHECK(!result.text_changed);
        EPOCHGUI_CHECK(state.text == "A\xc3\xa9 ");
        EPOCHGUI_CHECK(state.caret == 4);

        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::erase_backward });
        EPOCHGUI_CHECK(result.text_changed);
        EPOCHGUI_CHECK(state.text == "A\xc3\xa9");
        EPOCHGUI_CHECK(state.caret == 3);

        // Key repeat remains a sequence of erase commands, one codepoint per event.
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::erase_backward });
        EPOCHGUI_CHECK(result.text_changed);
        EPOCHGUI_CHECK(state.text == "A");
        EPOCHGUI_CHECK(state.caret == 1);

        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::insert_text, .text = " X\b Y\x7f" });
        EPOCHGUI_CHECK(result.text_changed);
        EPOCHGUI_CHECK(state.text == "A X Y");
        EPOCHGUI_CHECK(state.caret == 5);

        options.accept_tab = true;
        result = update_text_control(
            state,
            options,
            TextControlInput{ .command = TextControlCommand::insert_text, .text = "\t" });
        EPOCHGUI_CHECK(result.text_changed);
        EPOCHGUI_CHECK(state.text == "A X Y\t");
        EPOCHGUI_CHECK(state.caret == 6);
        return 0;
    }

    int text_editor_document()
    {
        TextEditorState editor{};
        set_text_editor_text(editor, "alpha\x0a" "beta\x0a");
        normalize_text_editor(editor);

        TextEditorSnapshot snapshot = text_editor_snapshot(editor);
        EPOCHGUI_CHECK(snapshot.line_count == 3u);
        EPOCHGUI_CHECK(text_editor_line(editor, 0u) == "alpha");
        EPOCHGUI_CHECK(text_editor_line(editor, 1u) == "beta");
        EPOCHGUI_CHECK(text_editor_line(editor, 2u).empty());
        EPOCHGUI_CHECK(!snapshot.dirty);

        EPOCHGUI_CHECK(find_next_text_editor_match(editor, "beta"));
        EPOCHGUI_CHECK(editor.active_match.first == 6u);
        EPOCHGUI_CHECK(editor.active_match.past_last == 10u);

        TextControlOptions options{ .multiline = true };
        EPOCHGUI_CHECK(replace_text_editor_match(editor, options, "Epoch"));
        snapshot = text_editor_snapshot(editor);
        EPOCHGUI_CHECK(editor.text.text == "alpha\x0a" "Epoch\x0a");
        EPOCHGUI_CHECK(snapshot.revision > 1u);
        EPOCHGUI_CHECK(snapshot.dirty);

        mark_text_editor_saved(editor);
        EPOCHGUI_CHECK(!text_editor_snapshot(editor).dirty);
        return 0;
    }
    int segmented_control_geometry()
    {
        const std::array<float, 3> widths{ 80.0f, 120.0f, 60.0f };
        const SegmentedControlLayoutOptions options{
            .position = { 10.0f, 20.0f },
            .item_widths = widths,
            .height = 30.0f,
            .gap = 4.0f
        };

        const SegmentedControlLayout layout = make_segmented_control_layout(options);
        EPOCHGUI_CHECK(layout.valid);
        EPOCHGUI_CHECK(layout.item_count == 3);
        EPOCHGUI_CHECK(layout.bounds.position.x == 10.0f);
        EPOCHGUI_CHECK(layout.bounds.position.y == 20.0f);
        EPOCHGUI_CHECK(layout.bounds.size.x == 268.0f);
        EPOCHGUI_CHECK(layout.bounds.size.y == 30.0f);

        const Rect second = segmented_control_item_layout(options, 1);
        EPOCHGUI_CHECK(second.position.x == 94.0f);
        EPOCHGUI_CHECK(second.position.y == 20.0f);
        EPOCHGUI_CHECK(second.size.x == 120.0f);
        EPOCHGUI_CHECK(second.size.y == 30.0f);

        EPOCHGUI_CHECK(segmented_control_item_at(options, { 10.0f, 20.0f }) == 0);
        EPOCHGUI_CHECK(segmented_control_item_at(options, { 95.0f, 25.0f }) == 1);
        EPOCHGUI_CHECK(segmented_control_item_at(options, { 220.0f, 25.0f }) == 2);
        EPOCHGUI_CHECK(
            segmented_control_item_at(options, { 91.0f, 25.0f })
            == invalid_selectable_row_index);
        EPOCHGUI_CHECK(
            segmented_control_item_at(options, { 500.0f, 25.0f })
            == invalid_selectable_row_index);
        return 0;
    }

    int visual_control_geometry()
    {
        ImageBoxLayout image = make_image_box_layout({
            .bounds = { { 10.0f, 20.0f }, { 200.0f, 100.0f } },
            .source_extent = { 400.0f, 200.0f },
            .fit = ImageFitMode::contain,
            .padding = 10.0f,
            .caption_height = 20.0f
        });
        EPOCHGUI_CHECK(image.valid);
        EPOCHGUI_CHECK(image.viewport.position.x == 20.0f);
        EPOCHGUI_CHECK(image.viewport.position.y == 30.0f);
        EPOCHGUI_CHECK(image.viewport.size.x == 180.0f);
        EPOCHGUI_CHECK(image.viewport.size.y == 60.0f);
        EPOCHGUI_CHECK(approximately(image.content.position.x, 50.0f));
        EPOCHGUI_CHECK(approximately(image.content.position.y, 30.0f));
        EPOCHGUI_CHECK(approximately(image.content.size.x, 120.0f));
        EPOCHGUI_CHECK(approximately(image.content.size.y, 60.0f));
        EPOCHGUI_CHECK(image.caption.position.y == 90.0f);

        image = make_image_box_layout({
            .bounds = { { 0.0f, 0.0f }, { 64.0f, 32.0f } },
            .source_extent = { 2.0f, 1.0f },
            .fit = ImageFitMode::stretch,
            .padding = 0.0f
        });
        EPOCHGUI_CHECK(image.valid);
        EPOCHGUI_CHECK(image.content.size.x == 64.0f);
        EPOCHGUI_CHECK(image.content.size.y == 32.0f);

        const std::array<float, 2> tabWidths{ 100.0f, 120.0f };
        const TabButtonLayout tab = make_tab_button_layout({
            .strip = {
                .position = { 0.0f, 0.0f },
                .item_widths = tabWidths,
                .height = 30.0f,
                .gap = 2.0f },
            .indicator_height = 3.0f,
            .close_extent = 16.0f,
            .label_padding = 8.0f
        }, 1u, true, true);
        EPOCHGUI_CHECK(tab.valid);
        EPOCHGUI_CHECK(tab.active);
        EPOCHGUI_CHECK(tab.closable);
        EPOCHGUI_CHECK(tab.button.position.x == 102.0f);
        EPOCHGUI_CHECK(tab.button.size.x == 120.0f);
        EPOCHGUI_CHECK(tab.indicator.position.y == 27.0f);
        EPOCHGUI_CHECK(tab.indicator.size.y == 3.0f);
        EPOCHGUI_CHECK(tab.close_button.position.x == 206.0f);
        EPOCHGUI_CHECK(tab.close_button.position.y == 7.0f);
        EPOCHGUI_CHECK(tab.label.size.x == 88.0f);

        const SliderLayoutOptions sliderOptions{
            .bounds = {{10.0f, 20.0f}, {200.0f, 30.0f}},
            .minimum = -1.0f,
            .maximum = 1.0f,
            .value = 0.25f,
            .step = 0.25f
        };
        const SliderLayout slider = make_slider_layout(sliderOptions);
        EPOCHGUI_CHECK(slider.valid);
        EPOCHGUI_CHECK(slider.bounds.position.x == 10.0f);
        EPOCHGUI_CHECK(slider.track.position.x == 14.0f);
        EPOCHGUI_CHECK(slider.track.size.x == 192.0f);
        EPOCHGUI_CHECK(approximately(slider.fraction, 0.625f));
        EPOCHGUI_CHECK(approximately(slider.fill.size.x, 120.0f));
        EPOCHGUI_CHECK(approximately(slider.thumb.position.x, 130.875f));
        EPOCHGUI_CHECK(
            slider_value_from_position(sliderOptions, 14.0f) == -1.0f);
        EPOCHGUI_CHECK(
            slider_value_from_position(sliderOptions, 110.0f) == 0.0f);
        EPOCHGUI_CHECK(
            slider_value_from_position(sliderOptions, 206.0f) == 1.0f);
        EPOCHGUI_CHECK(!make_slider_layout({
            .bounds = {{0.0f, 0.0f}, {100.0f, 20.0f}},
            .minimum = 1.0f,
            .maximum = 1.0f
        }).valid);
        return 0;
    }

    int window_scoped_identity_and_tool_tabs()
    {
        const std::string worldScroll = scoped_control_key(
            "host-1", "World Outliner", "body");
        const std::string assetScroll = scoped_control_key(
            "host-1", "Asset Browser", "body");
        const std::string secondHostScroll = scoped_control_key(
            "host-2", "World Outliner", "body");

        EPOCHGUI_CHECK(worldScroll != assetScroll);
        EPOCHGUI_CHECK(worldScroll != secondHostScroll);
        EPOCHGUI_CHECK(
            scoped_control_key("a|b", "c", "d")
            != scoped_control_key("a", "b|c", "d"));

        const ToolTabSizingPolicy policy{};
        EPOCHGUI_CHECK(approximately(
            preferred_tool_tab_width(20.0f, false, false, policy),
            policy.minimum_width));
        EPOCHGUI_CHECK(approximately(
            preferred_tool_tab_width(1000.0f, true, true, policy),
            policy.maximum_width));
        EPOCHGUI_CHECK(
            preferred_tool_tab_width(84.0f, true, false, policy)
            > preferred_tool_tab_width(84.0f, false, false, policy));
        EPOCHGUI_CHECK(
            preferred_tool_tab_width(84.0f, true, true, policy)
            > preferred_tool_tab_width(84.0f, true, false, policy));

        const std::array<float, 9> workspaceWidths{
            72.0f, 104.0f, 126.0f, 92.0f, 88.0f,
            78.0f, 76.0f, 130.0f, 86.0f
        };
        EPOCHGUI_CHECK(approximately(
            resolve_responsive_tab_width({
                .requested_width = 76.0f,
                .measured_label_width = 92.0f,
                .minimum_hit_width = 72.0f,
                .horizontal_padding = 24.0f}),
            116.0f));
        EPOCHGUI_CHECK(approximately(
            resolve_responsive_tab_width({
                .requested_width = 140.0f,
                .measured_label_width = 20.0f,
                .minimum_hit_width = 72.0f,
                .horizontal_padding = 24.0f}),
            140.0f));
        EPOCHGUI_CHECK(approximately(
            resolve_responsive_tab_width({
                .requested_width = 20.0f,
                .measured_label_width = 10.0f,
                .minimum_hit_width = 72.0f,
                .horizontal_padding = 24.0f}),
            72.0f));
        ResponsiveTabStripLayout responsive =
            make_responsive_tab_strip_layout({
                .item_widths = workspaceWidths,
                .active_index = 8u,
                .available_width = 900.0f,
                .gap = 0.0f,
                .overflow_width = 120.0f
            });
        EPOCHGUI_CHECK(responsive.valid);
        EPOCHGUI_CHECK(!responsive.overflowed);
        EPOCHGUI_CHECK(responsive.visible_indices.size() == workspaceWidths.size());
        EPOCHGUI_CHECK(responsive.overflow_indices.empty());

        responsive = make_responsive_tab_strip_layout({
            .item_widths = workspaceWidths,
            .active_index = 8u,
            .available_width = 520.0f,
            .gap = 2.0f,
            .overflow_width = 120.0f
        });
        EPOCHGUI_CHECK(responsive.valid);
        EPOCHGUI_CHECK(responsive.overflowed);
        EPOCHGUI_CHECK(responsive.is_visible(8u));
        EPOCHGUI_CHECK(!responsive.overflow_indices.empty());
        EPOCHGUI_CHECK(
            responsive.visible_width + 2.0f + responsive.overflow_width
            <= 520.0f);
        for (const std::uint32_t hidden : responsive.overflow_indices)
            EPOCHGUI_CHECK(!responsive.is_visible(hidden));

        responsive = make_responsive_tab_strip_layout({
            .item_widths = workspaceWidths,
            .active_index = 4u,
            .available_width = 80.0f,
            .gap = 2.0f,
            .overflow_width = 120.0f
        });
        EPOCHGUI_CHECK(responsive.valid);
        EPOCHGUI_CHECK(responsive.overflowed);
        EPOCHGUI_CHECK(responsive.visible_indices.empty());
        EPOCHGUI_CHECK(responsive.overflow_indices.size() == workspaceWidths.size());
        EPOCHGUI_CHECK(responsive.overflow_width == 80.0f);

        std::array<std::uint8_t, 9> enabled{};
        enabled.fill(1u);
        enabled[2] = 0u;
        EPOCHGUI_CHECK(navigate_responsive_tab_strip({
            .enabled = enabled,
            .active_index = 1u,
            .intent = ResponsiveTabNavigationIntent::next
        }) == 3u);
        EPOCHGUI_CHECK(navigate_responsive_tab_strip({
            .enabled = enabled,
            .active_index = 0u,
            .intent = ResponsiveTabNavigationIntent::previous
        }) == 8u);
        EPOCHGUI_CHECK(navigate_responsive_tab_strip({
            .enabled = enabled,
            .active_index = 4u,
            .intent = ResponsiveTabNavigationIntent::first
        }) == 0u);
        EPOCHGUI_CHECK(navigate_responsive_tab_strip({
            .enabled = enabled,
            .active_index = 4u,
            .intent = ResponsiveTabNavigationIntent::last
        }) == 8u);
        enabled.fill(0u);
        EPOCHGUI_CHECK(!navigate_responsive_tab_strip({
            .enabled = enabled,
            .active_index = 4u,
            .intent = ResponsiveTabNavigationIntent::next
        }));

        const auto standardDock = make_bottom_dock_height_layout({
            .viewport_height = 1080.0f,
            .toolbar_height = 98.0f,
            .requested_fraction = 0.24f
        });
        EPOCHGUI_CHECK(standardDock.valid && standardDock.visible);
        EPOCHGUI_CHECK(standardDock.bottom_height >= 120.0f);
        EPOCHGUI_CHECK(standardDock.center_height >= 240.0f);
        EPOCHGUI_CHECK(standardDock.bottom_height < standardDock.center_height);

        const auto constrainedDock = make_bottom_dock_height_layout({
            .viewport_height = 480.0f,
            .toolbar_height = 98.0f,
            .requested_fraction = 0.80f
        });
        EPOCHGUI_CHECK(constrainedDock.valid);
        EPOCHGUI_CHECK(constrainedDock.bottom_height >= 120.0f);
        EPOCHGUI_CHECK(constrainedDock.center_height >= 240.0f);

        const auto hiddenDock = make_bottom_dock_height_layout({
            .viewport_height = 720.0f,
            .toolbar_height = 98.0f,
            .visible = false
        });
        EPOCHGUI_CHECK(hiddenDock.valid && !hiddenDock.visible);
        EPOCHGUI_CHECK(hiddenDock.bottom_height == 0.0f);
        EPOCHGUI_CHECK(hiddenDock.splitter_height == 0.0f);
        EPOCHGUI_CHECK(hiddenDock.center_height == 622.0f);

        EPOCHGUI_CHECK(!make_bottom_dock_height_layout({
            .viewport_height = std::numeric_limits<float>::quiet_NaN()
        }).valid);
        return 0;
    }
    int responsive_chrome_layout()
    {
        const std::array<ChromeBarItemOptions, 7> leftItems{{
            { 104.0f, 88.0f, 100u, false, false },
            { 68.0f, 52.0f, 0u, false, true },
            { 68.0f, 52.0f, 1u, false, true },
            { 76.0f, 56.0f, 2u, false, true },
            { 92.0f, 64.0f, 3u, false, true },
            { 76.0f, 56.0f, 4u, false, true },
            { 72.0f, 54.0f, 5u, false, true }
        }};
        const std::array<ChromeBarItemOptions, 2> centerItems{{
            { 108.0f, 96.0f, 0u, true, true },
            { 160.0f, 104.0f, 1u, true, true }
        }};
        const std::array<ChromeBarItemOptions, 5> rightItems{{
            { 86.0f, 68.0f, 2u, false, true },
            { 104.0f, 78.0f, 3u, false, true },
            { 94.0f, 72.0f, 1u, false, true },
            { 112.0f, 82.0f, 0u, false, true },
            { 76.0f, 62.0f, 4u, false, true }
        }};

        const auto layout_for = [&](float width)
        {
            return make_chrome_bar_layout({
                .bounds = {{0.0f, 0.0f}, {width, 32.0f}},
                .left_items = leftItems,
                .center_items = centerItems,
                .right_items = rightItems,
                .item_gap = 4.0f,
                .zone_gap = 12.0f,
                .overflow_width = 82.0f,
                .horizontal_padding = 12.0f
            });
        };
        const auto inside = [](Rect outer, Rect inner)
        {
            if (inner.size.x <= 0.0f || inner.size.y <= 0.0f)
                return true;
            return inner.position.x >= outer.position.x
                && inner.position.y >= outer.position.y
                && inner.position.x + inner.size.x
                    <= outer.position.x + outer.size.x + 0.001f
                && inner.position.y + inner.size.y
                    <= outer.position.y + outer.size.y + 0.001f;
        };
        const auto overlaps = [](Rect left, Rect right)
        {
            if (left.size.x <= 0.0f || left.size.y <= 0.0f
                || right.size.x <= 0.0f || right.size.y <= 0.0f)
            {
                return false;
            }
            return left.position.x < right.position.x + right.size.x
                && left.position.x + left.size.x > right.position.x
                && left.position.y < right.position.y + right.size.y
                && left.position.y + left.size.y > right.position.y;
        };
        const auto chrome_is_bounded = [&](const ChromeBarLayout& layout)
        {
            std::vector<Rect> visible{};
            const auto append_zone = [&](const ChromeBarZoneLayout& zone)
            {
                for (const std::uint32_t index : zone.visible_indices)
                    visible.push_back(zone.item_bounds[index]);
                if (zone.overflow_button.size.x > 0.0f)
                    visible.push_back(zone.overflow_button);
            };
            append_zone(layout.left);
            append_zone(layout.center);
            append_zone(layout.right);
            for (std::size_t index = 0; index < visible.size(); ++index)
            {
                if (!inside(layout.bounds, visible[index]))
                    return false;
                for (std::size_t other = index + 1u;
                    other < visible.size(); ++other)
                {
                    if (overlaps(visible[index], visible[other]))
                        return false;
                }
            }
            return true;
        };

        const ChromeBarLayout wide = layout_for(1600.0f);
        EPOCHGUI_CHECK(wide.valid);
        EPOCHGUI_CHECK(wide.density == ChromeDensity::full);
        EPOCHGUI_CHECK(wide.left.visible_indices.size() == leftItems.size());
        EPOCHGUI_CHECK(wide.center.visible_indices.size() == centerItems.size());
        EPOCHGUI_CHECK(wide.right.visible_indices.size() == rightItems.size());
        EPOCHGUI_CHECK(!wide.left.overflowed);
        EPOCHGUI_CHECK(!wide.center.overflowed);
        EPOCHGUI_CHECK(!wide.right.overflowed);
        EPOCHGUI_CHECK(chrome_is_bounded(wide));
        const float wideCenter = wide.center.item_bounds.front().position.x
            + wide.center.occupied_width * 0.5f;
        EPOCHGUI_CHECK(approximately(wideCenter, 800.0f));

        const ChromeBarLayout medium = layout_for(960.0f);
        EPOCHGUI_CHECK(medium.valid);
        EPOCHGUI_CHECK(medium.density != ChromeDensity::full);
        EPOCHGUI_CHECK(medium.center.is_visible(0u));
        EPOCHGUI_CHECK(chrome_is_bounded(medium));

        const ChromeBarLayout narrow = layout_for(720.0f);
        EPOCHGUI_CHECK(narrow.valid);
        EPOCHGUI_CHECK(narrow.density == ChromeDensity::minimal);
        EPOCHGUI_CHECK(narrow.center.is_visible(0u));
        EPOCHGUI_CHECK(narrow.left.overflowed);
        EPOCHGUI_CHECK(narrow.right.overflowed);
        EPOCHGUI_CHECK(chrome_is_bounded(narrow));
        EPOCHGUI_CHECK(std::find(
            narrow.right.visible_indices.begin(),
            narrow.right.visible_indices.end(),
            3u) != narrow.right.visible_indices.end());

        const ChromeBarLayout compact = layout_for(420.0f);
        EPOCHGUI_CHECK(compact.valid);
        EPOCHGUI_CHECK(compact.center.is_visible(0u));
        EPOCHGUI_CHECK(compact.left.overflowed);
        EPOCHGUI_CHECK(compact.right.overflowed);
        EPOCHGUI_CHECK(chrome_is_bounded(compact));

        const ChromeBarLayout repeated = layout_for(720.0f);
        EPOCHGUI_CHECK(repeated.left.visible_indices == narrow.left.visible_indices);
        EPOCHGUI_CHECK(repeated.left.overflow_indices == narrow.left.overflow_indices);
        EPOCHGUI_CHECK(repeated.right.visible_indices == narrow.right.visible_indices);
        EPOCHGUI_CHECK(repeated.right.overflow_indices == narrow.right.overflow_indices);
        EPOCHGUI_CHECK(approximately(
            repeated.center.item_bounds[0].position.x,
            narrow.center.item_bounds[0].position.x));

        EPOCHGUI_CHECK(!make_chrome_bar_layout({
            .bounds = {{0.0f, 0.0f}, {
                (std::numeric_limits<float>::quiet_NaN)(), 32.0f}}
        }).valid);
        EPOCHGUI_CHECK(!make_chrome_bar_layout({
            .bounds = {{0.0f, 0.0f}, {-1.0f, 32.0f}}
        }).valid);
        return 0;
    }


}

int main()
{
    if (const int result = replacement_and_clipboard(); result != 0)
        return result;
    if (const int result = utf8_and_limits(); result != 0)
        return result;
    if (const int result = navigation_and_selection(); result != 0)
        return result;
    if (const int result = filtering_read_only_and_scroll(); result != 0)
        return result;
    if (const int result = whitespace_and_control_filtering(); result != 0)
        return result;
    if (const int result = text_editor_document(); result != 0)
        return result;
    if (const int result = segmented_control_geometry(); result != 0)
        return result;
    if (const int result = visual_control_geometry(); result != 0)
        return result;
    if (const int result = responsive_chrome_layout(); result != 0)
        return result;
    return window_scoped_identity_and_tool_tabs();
}
