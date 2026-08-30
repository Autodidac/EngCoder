// SPDX-License-Identifier: LicenseRef-MIT-NoSell

#include "../include/gui/system_workspace.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace epochengine::gui_lib::system_workspace;

    [[nodiscard]] int check(bool condition, int line) noexcept
    {
        return condition ? 0 : line;
    }

#define EPOCHGUI_CHECK(condition) \
    do { const int failure = check((condition), __LINE__); if (failure != 0) return failure; } while (false)

    [[nodiscard]] Row make_row(
        std::string id,
        std::string label,
        RowKind kind,
        RowStatus status,
        std::uint64_t order,
        std::string parent = {},
        std::string category = "Runtime",
        std::string detail = {})
    {
        Row row{};
        row.id = std::move(id);
        row.parent_id = std::move(parent);
        row.label = std::move(label);
        row.category = std::move(category);
        row.detail = std::move(detail);
        row.kind = kind;
        row.status = status;
        row.adapter_order = order;
        row.expandable = kind == RowKind::system;
        return row;
    }

    [[nodiscard]] std::vector<Row> base_rows()
    {
        std::vector<Row> rows{};
        rows.push_back(make_row(
            "task.present",
            "Present frame",
            RowKind::task,
            RowStatus::running,
            2u,
            "system.render",
            "Tasks",
            "Submit the completed frame"));
        rows.push_back(make_row(
            "system.render",
            "Renderer",
            RowKind::system,
            RowStatus::healthy,
            20u));
        rows.push_back(make_row(
            "task.mix",
            "Mix audio",
            RowKind::task,
            RowStatus::waiting,
            1u,
            "system.audio",
            "Audio"));
        rows.push_back(make_row(
            "task.cull",
            "Cull visible objects",
            RowKind::task,
            RowStatus::queued,
            1u,
            "system.render",
            "Tasks"));
        rows.push_back(make_row(
            "system.audio",
            "Audio",
            RowKind::system,
            RowStatus::idle,
            10u));
        return rows;
    }

    class TestAdapter final : public Adapter
    {
    public:
        AdapterSnapshot read(AdapterRequest request) override
        {
            requested_limit = request.maximum_rows;
            return {
                .rows = values,
                .error_message = error,
                .source_revision = revision,
                .succeeded = succeeds
            };
        }

        std::vector<Row> values{};
        std::string error{};
        std::uint64_t revision{};
        std::size_t requested_limit{};
        bool succeeds{true};
    };

    [[nodiscard]] bool visible_ids_equal(
        const Controller& controller,
        std::initializer_list<std::string_view> expected)
    {
        const auto visible = controller.visible_rows();
        if (visible.size() != expected.size())
            return false;
        std::size_t index = 0u;
        for (const std::string_view id : expected)
        {
            if (visible[index].row == nullptr || visible[index].row->id != id)
                return false;
            ++index;
        }
        return true;
    }

    int refresh_sort_and_summary()
    {
        TestAdapter adapter{};
        adapter.values = base_rows();
        adapter.revision = 44u;
        Controller controller{};

        const RefreshResult refresh = controller.refresh(adapter);
        EPOCHGUI_CHECK(refresh.committed);
        EPOCHGUI_CHECK(refresh.error == ErrorCode::none);
        EPOCHGUI_CHECK(adapter.requested_limit == controller.limits().maximum_rows);
        EPOCHGUI_CHECK(controller.source_revision() == 44u);
        EPOCHGUI_CHECK(controller.content_state() == ContentState::ready);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {"system.audio", "system.render"}));

        const Summary& summary = controller.summary();
        EPOCHGUI_CHECK(summary.total_rows == 5u);
        EPOCHGUI_CHECK(summary.visible_rows == 2u);
        EPOCHGUI_CHECK(summary.matching_rows == 5u);
        EPOCHGUI_CHECK(summary.system_rows == 2u);
        EPOCHGUI_CHECK(summary.task_rows == 3u);
        EPOCHGUI_CHECK(summary.status_totals[static_cast<std::size_t>(RowStatus::running)] == 1u);

        EPOCHGUI_CHECK(controller.set_expanded("system.render", true).changed);
        EPOCHGUI_CHECK(visible_ids_equal(
            controller,
            {"system.audio", "system.render", "task.cull", "task.present"}));
        EPOCHGUI_CHECK(controller.set_expanded("system.audio", true).changed);
        EPOCHGUI_CHECK(visible_ids_equal(
            controller,
            {"system.audio", "task.mix", "system.render", "task.cull", "task.present"}));

        EPOCHGUI_CHECK(controller.category_summaries().size() == 3u);
        EPOCHGUI_CHECK(controller.category_summaries()[0].category == "Audio");
        EPOCHGUI_CHECK(controller.category_summaries()[1].category == "Runtime");
        EPOCHGUI_CHECK(controller.category_summaries()[2].category == "Tasks");

        EPOCHGUI_CHECK(controller.set_sort({
            .key = SortKey::name,
            .direction = SortDirection::descending,
            .systems_first = true }).changed);
        EPOCHGUI_CHECK(visible_ids_equal(
            controller,
            {"system.render", "task.present", "task.cull", "system.audio", "task.mix"}));
        return 0;
    }

    int filtering_and_explicit_states()
    {
        Controller controller{};
        const std::vector<Row> rows = base_rows();
        EPOCHGUI_CHECK(controller.replace_rows(rows).committed);

        EPOCHGUI_CHECK(controller.set_text_filter("PRESENT").changed);
        EPOCHGUI_CHECK(controller.content_state() == ContentState::ready);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {"system.render", "task.present"}));
        EPOCHGUI_CHECK(!controller.visible_rows()[0].direct_match);
        EPOCHGUI_CHECK(controller.visible_rows()[0].expanded);
        EPOCHGUI_CHECK(controller.visible_rows()[0].expansion_forced);
        EPOCHGUI_CHECK(controller.visible_rows()[1].direct_match);

        EPOCHGUI_CHECK(controller.clear_filters().changed);
        EPOCHGUI_CHECK(controller.set_category_filter("audio", true).changed);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {"system.audio", "task.mix"}));
        EPOCHGUI_CHECK(controller.set_status_filter(RowStatus::waiting, true).changed);
        EPOCHGUI_CHECK(visible_ids_equal(controller, {"system.audio", "task.mix"}));
        EPOCHGUI_CHECK(controller.status_filter_enabled(RowStatus::waiting));

        EPOCHGUI_CHECK(controller.set_text_filter("does-not-exist").changed);
        EPOCHGUI_CHECK(controller.content_state() == ContentState::no_matches);
        EPOCHGUI_CHECK(controller.visible_rows().empty());
        EPOCHGUI_CHECK(controller.summary().matching_rows == 0u);

        EPOCHGUI_CHECK(controller.replace_rows({}).committed);
        EPOCHGUI_CHECK(controller.content_state() == ContentState::empty);
        EPOCHGUI_CHECK(controller.rows().empty());
        return 0;
    }

    int keyboard_navigation()
    {
        Controller controller{};
        const std::vector<Row> rows = base_rows();
        EPOCHGUI_CHECK(controller.replace_rows(rows).committed);

        NavigationResult navigation = controller.navigate(NavigationCommand::first);
        EPOCHGUI_CHECK(navigation.selection_changed);
        EPOCHGUI_CHECK(navigation.selected_id == "system.audio");
        navigation = controller.navigate(NavigationCommand::next);
        EPOCHGUI_CHECK(navigation.selected_id == "system.render");

        navigation = controller.navigate(NavigationCommand::move_right);
        EPOCHGUI_CHECK(navigation.expansion_changed);
        EPOCHGUI_CHECK(controller.is_expanded("system.render"));
        EPOCHGUI_CHECK(controller.selected_id() == "system.render");
        navigation = controller.navigate(NavigationCommand::move_right);
        EPOCHGUI_CHECK(navigation.selection_changed);
        EPOCHGUI_CHECK(navigation.selected_id == "task.cull");
        navigation = controller.navigate(NavigationCommand::next);
        EPOCHGUI_CHECK(navigation.selected_id == "task.present");
        navigation = controller.navigate(NavigationCommand::move_left);
        EPOCHGUI_CHECK(navigation.selected_id == "system.render");
        navigation = controller.navigate(NavigationCommand::toggle_expansion);
        EPOCHGUI_CHECK(navigation.expansion_changed);
        EPOCHGUI_CHECK(!controller.is_expanded("system.render"));

        navigation = controller.navigate(NavigationCommand::last);
        EPOCHGUI_CHECK(navigation.selected_id == "system.render");
        navigation = controller.navigate(NavigationCommand::previous);
        EPOCHGUI_CHECK(navigation.selected_id == "system.audio");
        return 0;
    }

    int stale_selection_and_expansion()
    {
        Controller controller{};
        std::vector<Row> rows = base_rows();
        EPOCHGUI_CHECK(controller.replace_rows(rows).committed);
        EPOCHGUI_CHECK(controller.set_expanded("system.audio", true).changed);
        EPOCHGUI_CHECK(controller.set_expanded("system.render", true).changed);
        EPOCHGUI_CHECK(controller.select("task.present").changed);

        std::erase_if(rows, [](const Row& row) { return row.id == "task.present"; });
        const RefreshResult replaced = controller.replace_rows(rows, 2u);
        EPOCHGUI_CHECK(replaced.committed);
        EPOCHGUI_CHECK(replaced.stale_selection_resolved);
        EPOCHGUI_CHECK(replaced.selection_changed);
        EPOCHGUI_CHECK(controller.selected_id() == "task.cull");
        EPOCHGUI_CHECK(controller.summary().selection_visible);

        std::erase_if(
            rows,
            [](const Row& row)
            {
                return row.id == "system.audio" || row.parent_id == "system.audio";
            });
        const RefreshResult pruned = controller.replace_rows(rows, 3u);
        EPOCHGUI_CHECK(pruned.committed);
        EPOCHGUI_CHECK(pruned.stale_expansions_removed == 1u);
        EPOCHGUI_CHECK(!controller.is_expanded("system.audio"));

        EPOCHGUI_CHECK(controller.select("task.cull").accepted);
        std::erase_if(rows, [](const Row& row) { return row.id == "task.cull"; });
        const RefreshResult cleared = controller.replace_rows(
            rows,
            4u,
            {.stale_selection = StaleSelectionPolicy::clear});
        EPOCHGUI_CHECK(cleared.stale_selection_resolved);
        EPOCHGUI_CHECK(controller.selected_id().empty());
        return 0;
    }

    int bounded_and_transactional_failures()
    {
        Controller controller{};
        const std::vector<Row> valid = base_rows();
        EPOCHGUI_CHECK(controller.replace_rows(valid, 1u).committed);
        const std::size_t committed_count = controller.rows().size();

        std::vector<Row> duplicate = valid;
        duplicate.push_back(valid.front());
        RefreshResult result = controller.replace_rows(duplicate, 2u);
        EPOCHGUI_CHECK(!result.committed);
        EPOCHGUI_CHECK(result.error == ErrorCode::duplicate_id);
        EPOCHGUI_CHECK(controller.content_state() == ContentState::error);
        EPOCHGUI_CHECK(controller.using_stale_rows());
        EPOCHGUI_CHECK(controller.rows().size() == committed_count);
        EPOCHGUI_CHECK(controller.error().row_id == "task.present");
        EPOCHGUI_CHECK(controller.source_revision() == 1u);

        TestAdapter adapter{};
        adapter.values = valid;
        adapter.revision = 3u;
        adapter.succeeds = false;
        adapter.error = "scheduler snapshot unavailable";
        result = controller.refresh(adapter);
        EPOCHGUI_CHECK(result.error == ErrorCode::adapter_failure);
        EPOCHGUI_CHECK(controller.error().message == adapter.error);
        EPOCHGUI_CHECK(controller.rows().size() == committed_count);

        adapter.succeeds = true;
        result = controller.refresh(adapter);
        EPOCHGUI_CHECK(result.committed);
        EPOCHGUI_CHECK(controller.content_state() == ContentState::ready);
        EPOCHGUI_CHECK(!controller.using_stale_rows());
        EPOCHGUI_CHECK(!controller.error().present());
        EPOCHGUI_CHECK(controller.source_revision() == 3u);

        std::vector<Row> missing_parent{
            make_row(
                "task.orphan",
                "Orphan",
                RowKind::task,
                RowStatus::blocked,
                0u,
                "system.missing")
        };
        result = controller.replace_rows(missing_parent);
        EPOCHGUI_CHECK(result.error == ErrorCode::missing_parent);

        std::vector<Row> cycle{
            make_row("a", "A", RowKind::task, RowStatus::waiting, 0u, "b"),
            make_row("b", "B", RowKind::task, RowStatus::waiting, 0u, "a")
        };
        result = controller.replace_rows(cycle);
        EPOCHGUI_CHECK(result.error == ErrorCode::hierarchy_cycle);

        Controller tiny{Limits{.maximum_rows = 2u}};
        result = tiny.replace_rows(valid);
        EPOCHGUI_CHECK(result.error == ErrorCode::row_limit_exceeded);
        EPOCHGUI_CHECK(tiny.content_state() == ContentState::error);
        EPOCHGUI_CHECK(!tiny.using_stale_rows());

        const std::string long_filter(
            controller.limits().maximum_filter_bytes + 1u,
            'x');
        const MutationResult filter = controller.set_text_filter(long_filter);
        EPOCHGUI_CHECK(!filter.accepted);
        EPOCHGUI_CHECK(filter.error == ErrorCode::filter_too_long);
        return 0;
    }
}

int main()
{
    static_assert(row_kind_name(RowKind::system) == "System");
    static_assert(row_status_name(RowStatus::failed) == "Failed");
    static_assert(row_status_name(RowStatus::cancelled) == "Cancelled");

    if (const int result = refresh_sort_and_summary(); result != 0)
        return result;
    if (const int result = filtering_and_explicit_states(); result != 0)
        return result;
    if (const int result = keyboard_navigation(); result != 0)
        return result;
    if (const int result = stale_selection_and_expansion(); result != 0)
        return result;
    return bounded_and_transactional_failures();
}
