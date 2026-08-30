#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

import epoch.gui;

namespace
{
    using namespace epochengine::gui_lib;

    [[nodiscard]] int check(bool condition, int line) noexcept
    {
        return condition ? 0 : line;
    }

#define EPOCHGUI_CHECK(condition) \
    do { const int failure = check((condition), __LINE__); if (failure != 0) return failure; } while (false)

    [[nodiscard]] AssetGridLayoutOptions test_layout_options() noexcept
    {
        return {
            .viewport = {{10.0f, 20.0f}, {332.0f, 220.0f}},
            .tile_extent = {100.0f, 140.0f},
            .gap = {8.0f, 10.0f},
            .padding = {8.0f, 8.0f},
            .image_height = 92.0f,
            .label_height = 28.0f,
            .detail_height = 20.0f,
            .maximum_visible_tiles = 64
        };
    }

    int stable_ids_filter_and_metadata()
    {
        const std::array<AssetGridItem, 6> items{
            AssetGridItem{
                .id = {11},
                .label = "Brick Albedo",
                .detail = "Texture",
                .search_terms = "wall masonry",
                .kind = AssetGridItemKind::texture,
                .image = AssetGridImageMetadata{
                    .content_key = 901,
                    .pixel_width = 1024,
                    .pixel_height = 512,
                    .fit = ImageFitMode::contain,
                    .has_alpha = false}},
            AssetGridItem{
                .id = {12},
                .label = "Metal",
                .detail = "Material",
                .search_terms = "steel reflective",
                .kind = AssetGridItemKind::material,
                .image = std::nullopt},
            AssetGridItem{
                .id = {13},
                .label = "Hero",
                .detail = "Model",
                .kind = AssetGridItemKind::model,
                .image = AssetGridImageMetadata{
                    .content_key = 902,
                    .pixel_width = 0,
                    .pixel_height = 256}},
            AssetGridItem{.id = {}, .label = "Invalid"},
            AssetGridItem{.id = {12}, .label = "Duplicate Metal"},
            AssetGridItem{
                .id = {14},
                .label = "Hidden",
                .detail = "Texture",
                .enabled = false}
        };

        EPOCHGUI_CHECK(asset_grid_text_matches_filter("Brick Albedo", "ALBEDO"));
        EPOCHGUI_CHECK(asset_grid_item_matches_filter(items[0], "MASONRY"));
        EPOCHGUI_CHECK(!asset_grid_item_matches_filter(items[1], "brick"));

        AssetGridFilterResult filtered = filter_asset_grid_items(
            items,
            {.query = "texture", .include_disabled = false});
        EPOCHGUI_CHECK(filtered.inspected_count == items.size());
        EPOCHGUI_CHECK(filtered.matched_count == 1);
        EPOCHGUI_CHECK(filtered.source_indices.size() == 1);
        EPOCHGUI_CHECK(filtered.source_indices[0] == 0);
        EPOCHGUI_CHECK(filtered.invalid_id_count == 1);
        EPOCHGUI_CHECK(filtered.duplicate_id_count == 1);
        EPOCHGUI_CHECK(!filtered.source_truncated);
        EPOCHGUI_CHECK(!filtered.results_truncated);

        filtered = filter_asset_grid_items(items);
        const AssetGridLayout layout = make_asset_grid_layout(
            items, filtered, test_layout_options());
        EPOCHGUI_CHECK(layout.valid);
        EPOCHGUI_CHECK(layout.column_count == 3);
        EPOCHGUI_CHECK(layout.row_count == 2);
        EPOCHGUI_CHECK(layout.visible_tiles.size() == 4);
        EPOCHGUI_CHECK(layout.visible_tiles[0].id == AssetGridItemId{11});
        EPOCHGUI_CHECK(layout.visible_tiles[0].has_image);
        EPOCHGUI_CHECK(layout.visible_tiles[0].image_metadata_valid);
        EPOCHGUI_CHECK(layout.visible_tiles[0].image.valid);
        EPOCHGUI_CHECK(layout.visible_tiles[0].image.content.size.x == 92.0f);
        EPOCHGUI_CHECK(layout.visible_tiles[0].image.content.size.y == 46.0f);
        EPOCHGUI_CHECK(!layout.visible_tiles[1].has_image);
        EPOCHGUI_CHECK(!layout.visible_tiles[1].image.valid);
        EPOCHGUI_CHECK(layout.visible_tiles[1].image.frame.size.x == 100.0f);
        EPOCHGUI_CHECK(layout.visible_tiles[1].image.frame.size.y == 92.0f);
        EPOCHGUI_CHECK(!layout.visible_tiles[1].hovered);
        EPOCHGUI_CHECK(layout.visible_tiles[2].has_image);
        EPOCHGUI_CHECK(!layout.visible_tiles[2].image_metadata_valid);
        return 0;
    }

    int fixed_grid_selection_and_activation()
    {
        const std::array<AssetGridItem, 4> items{
            AssetGridItem{.id = {21}, .label = "Textures", .kind = AssetGridItemKind::folder},
            AssetGridItem{
                .id = {22},
                .label = "Floor",
                .kind = AssetGridItemKind::texture,
                .activation = AssetGridActivationRole::open_in_tab},
            AssetGridItem{
                .id = {23},
                .label = "Refresh",
                .activation = AssetGridActivationRole::invoke},
            AssetGridItem{
                .id = {24},
                .label = "Read Only",
                .activation = AssetGridActivationRole::none}
        };
        AssetGridState state{};
        const AssetGridLayoutOptions options = test_layout_options();

        AssetGridUpdateResult update = update_asset_grid(
            state,
            items,
            {},
            options,
            {.pointer_position = {130.0f, 40.0f}, .pointer_pressed = true});
        EPOCHGUI_CHECK(update.selection_changed);
        EPOCHGUI_CHECK(update.selected_id == AssetGridItemId{22});
        EPOCHGUI_CHECK(update.selected_source_index == 1);
        EPOCHGUI_CHECK(!update.activation_requested);
        EPOCHGUI_CHECK(update.view.layout.visible_tiles[1].selected);
        EPOCHGUI_CHECK(update.view.layout.visible_tiles[0].tile.size.x == 100.0f);
        EPOCHGUI_CHECK(update.view.layout.visible_tiles[0].tile.size.y == 140.0f);
        EPOCHGUI_CHECK(update.view.layout.visible_tiles[1].tile.position.x == 126.0f);

        update = update_asset_grid(
            state,
            items,
            {},
            options,
            {.pointer_position = {130.0f, 40.0f}, .pointer_activated = true});
        EPOCHGUI_CHECK(update.activation_requested);
        EPOCHGUI_CHECK(update.activated_id == AssetGridItemId{22});
        EPOCHGUI_CHECK(update.activated_source_index == 1);
        EPOCHGUI_CHECK(update.activation == AssetGridActivationRole::open_in_tab);

        update = update_asset_grid(
            state,
            items,
            {},
            options,
            {.pointer_position = {20.0f, 190.0f}, .context_requested = true});
        EPOCHGUI_CHECK(update.context_request_valid);
        EPOCHGUI_CHECK(update.context_requested_id == AssetGridItemId{24});
        EPOCHGUI_CHECK(update.context_requested_source_index == 3);
        EPOCHGUI_CHECK(update.selection_changed);
        EPOCHGUI_CHECK(update.selected_id == AssetGridItemId{24});
        EPOCHGUI_CHECK(!update.activation_requested);
        EPOCHGUI_CHECK(
            update.view.layout.visible_tiles[3].activation
            == AssetGridActivationRole::none);

        update = update_asset_grid(
            state,
            items,
            {},
            options,
            {.pointer_position = {330.0f, 230.0f}, .context_requested = true});
        EPOCHGUI_CHECK(!update.context_request_valid);
        EPOCHGUI_CHECK(!update.context_requested_id.has_value());
        EPOCHGUI_CHECK(
            update.context_requested_source_index
            == invalid_asset_grid_source_index);

        update = update_asset_grid(
            state,
            items,
            {},
            options,
            {.activate_selected = true, .requested_selection = AssetGridItemId{23}});
        EPOCHGUI_CHECK(update.selection_changed);
        EPOCHGUI_CHECK(update.selected_id == AssetGridItemId{23});
        EPOCHGUI_CHECK(update.activation_requested);
        EPOCHGUI_CHECK(update.activation == AssetGridActivationRole::invoke);

        update = update_asset_grid(
            state,
            items,
            {},
            options,
            {.pointer_position = {330.0f, 230.0f}, .pointer_pressed = true});
        EPOCHGUI_CHECK(update.selection_changed);
        EPOCHGUI_CHECK(!update.selected_id.has_value());
        return 0;
    }

    int bounded_and_virtualized_behavior()
    {
        std::vector<std::string> labels{};
        std::vector<AssetGridItem> items{};
        labels.reserve(asset_grid_maximum_items + 32);
        items.reserve(asset_grid_maximum_items + 32);
        for (std::size_t index = 0; index < asset_grid_maximum_items + 32; ++index)
        {
            labels.push_back("Asset " + std::to_string(index));
            items.push_back(AssetGridItem{
                .id = {static_cast<std::uint64_t>(index + 1)},
                .label = labels.back()});
        }

        const AssetGridFilterResult filtered = filter_asset_grid_items(
            items,
            {.maximum_results = asset_grid_maximum_items + 100});
        EPOCHGUI_CHECK(filtered.inspected_count == asset_grid_maximum_items);
        EPOCHGUI_CHECK(filtered.source_indices.size() == asset_grid_maximum_items);
        EPOCHGUI_CHECK(filtered.source_truncated);

        AssetGridLayoutOptions options = test_layout_options();
        options.maximum_visible_tiles = 2;
        options.scroll_offset = 1000000.0f;
        const AssetGridLayout layout = make_asset_grid_layout(
            items, filtered, options);
        EPOCHGUI_CHECK(layout.valid);
        EPOCHGUI_CHECK(layout.visible_tiles.size() == 2);
        EPOCHGUI_CHECK(layout.visible_tiles_truncated);
        EPOCHGUI_CHECK(layout.scroll_offset == layout.maximum_scroll_offset);
        EPOCHGUI_CHECK(layout.first_visible_row > 0);

        AssetGridFilterResult oversizedFilter = filtered;
        oversizedFilter.source_indices.resize(asset_grid_maximum_items + 3, 0);
        const AssetGridLayout boundedLayout = make_asset_grid_layout(
            items, oversizedFilter, options);
        EPOCHGUI_CHECK(
            boundedLayout.item_count == asset_grid_maximum_items);
        EPOCHGUI_CHECK(boundedLayout.items_truncated);

        const std::string longQuery(asset_grid_maximum_filter_bytes + 20, 'a');
        const AssetGridFilterResult longFilter = filter_asset_grid_items(
            items, {.query = longQuery, .maximum_results = 1});
        EPOCHGUI_CHECK(longFilter.query_truncated);
        EPOCHGUI_CHECK(longFilter.source_indices.empty());

        options.tile_extent.x = 0.0f;
        EPOCHGUI_CHECK(!make_asset_grid_layout(items, filtered, options).valid);
        return 0;
    }
}

int main()
{
    if (const int result = stable_ids_filter_and_metadata(); result != 0)
        return result;
    if (const int result = fixed_grid_selection_and_activation(); result != 0)
        return result;
    return bounded_and_virtualized_behavior();
}
