// SPDX-License-Identifier: LicenseRef-MIT-NoSell
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace epochengine::gui_lib::system_workspace
{
    enum class RowKind : std::uint8_t
    {
        system,
        task
    };

    enum class RowStatus : std::uint8_t
    {
        unknown,
        healthy,
        idle,
        queued,
        running,
        waiting,
        blocked,
        failed,
        cancelled,
        disabled,
        count
    };

    inline constexpr std::size_t row_status_count =
        static_cast<std::size_t>(RowStatus::count);

    [[nodiscard]] constexpr std::string_view row_kind_name(RowKind kind) noexcept
    {
        switch (kind)
        {
        case RowKind::system: return "System";
        case RowKind::task: return "Task";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr std::string_view row_status_name(RowStatus status) noexcept
    {
        switch (status)
        {
        case RowStatus::unknown: return "Unknown";
        case RowStatus::healthy: return "Healthy";
        case RowStatus::idle: return "Idle";
        case RowStatus::queued: return "Queued";
        case RowStatus::running: return "Running";
        case RowStatus::waiting: return "Waiting";
        case RowStatus::blocked: return "Blocked";
        case RowStatus::failed: return "Failed";
        case RowStatus::cancelled: return "Cancelled";
        case RowStatus::disabled: return "Disabled";
        case RowStatus::count: break;
        }
        return "Invalid";
    }

    struct Row final
    {
        std::string id{};
        std::string parent_id{};
        std::string label{};
        std::string category{};
        std::string detail{};
        RowKind kind{RowKind::system};
        RowStatus status{RowStatus::unknown};
        std::uint64_t adapter_order{};
        std::uint64_t revision{};
        bool expandable{};
    };

    struct Limits final
    {
        std::size_t maximum_rows{4'096u};
        std::size_t maximum_id_bytes{160u};
        std::size_t maximum_label_bytes{512u};
        std::size_t maximum_category_bytes{160u};
        std::size_t maximum_detail_bytes{2'048u};
        std::size_t maximum_filter_bytes{256u};
        std::size_t maximum_active_category_filters{64u};
        std::size_t maximum_hierarchy_depth{64u};

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return maximum_rows > 0u
                && maximum_id_bytes > 0u
                && maximum_label_bytes > 0u
                && maximum_category_bytes > 0u
                && maximum_detail_bytes > 0u
                && maximum_filter_bytes > 0u
                && maximum_active_category_filters > 0u
                && maximum_hierarchy_depth > 0u;
        }
    };

    struct AdapterRequest final
    {
        std::size_t maximum_rows{};
    };

    struct AdapterSnapshot final
    {
        std::span<const Row> rows{};
        std::string_view error_message{};
        std::uint64_t source_revision{};
        bool succeeded{true};
    };

    class Adapter
    {
    public:
        virtual ~Adapter() = default;
        [[nodiscard]] virtual AdapterSnapshot read(AdapterRequest request) = 0;
    };

    enum class ErrorCode : std::uint8_t
    {
        none,
        invalid_limits,
        adapter_failure,
        row_limit_exceeded,
        empty_id,
        empty_label,
        duplicate_id,
        invalid_kind,
        invalid_status,
        field_too_long,
        missing_parent,
        self_parent,
        hierarchy_cycle,
        hierarchy_depth_exceeded,
        filter_too_long,
        category_filter_limit,
        unknown_row
    };

    [[nodiscard]] constexpr std::string_view error_code_name(ErrorCode code) noexcept
    {
        switch (code)
        {
        case ErrorCode::none: return "none";
        case ErrorCode::invalid_limits: return "invalid_limits";
        case ErrorCode::adapter_failure: return "adapter_failure";
        case ErrorCode::row_limit_exceeded: return "row_limit_exceeded";
        case ErrorCode::empty_id: return "empty_id";
        case ErrorCode::empty_label: return "empty_label";
        case ErrorCode::duplicate_id: return "duplicate_id";
        case ErrorCode::invalid_kind: return "invalid_kind";
        case ErrorCode::invalid_status: return "invalid_status";
        case ErrorCode::field_too_long: return "field_too_long";
        case ErrorCode::missing_parent: return "missing_parent";
        case ErrorCode::self_parent: return "self_parent";
        case ErrorCode::hierarchy_cycle: return "hierarchy_cycle";
        case ErrorCode::hierarchy_depth_exceeded: return "hierarchy_depth_exceeded";
        case ErrorCode::filter_too_long: return "filter_too_long";
        case ErrorCode::category_filter_limit: return "category_filter_limit";
        case ErrorCode::unknown_row: return "unknown_row";
        }
        return "invalid";
    }

    struct Error final
    {
        ErrorCode code{ErrorCode::none};
        std::string message{};
        std::string row_id{};

        [[nodiscard]] bool present() const noexcept
        {
            return code != ErrorCode::none;
        }
    };

    enum class ContentState : std::uint8_t
    {
        ready,
        empty,
        no_matches,
        error
    };

    enum class SortKey : std::uint8_t
    {
        adapter_order,
        name,
        category,
        status,
        kind
    };

    enum class SortDirection : std::uint8_t
    {
        ascending,
        descending
    };

    struct Sort final
    {
        SortKey key{SortKey::adapter_order};
        SortDirection direction{SortDirection::ascending};
        bool systems_first{true};

        friend constexpr bool operator==(const Sort&, const Sort&) noexcept = default;
    };

    enum class StaleSelectionPolicy : std::uint8_t
    {
        clear,
        select_nearest_visible
    };

    struct RefreshOptions final
    {
        StaleSelectionPolicy stale_selection{StaleSelectionPolicy::select_nearest_visible};
        bool preserve_expansion{true};
    };

    struct RefreshResult final
    {
        ErrorCode error{ErrorCode::none};
        std::size_t supplied_rows{};
        std::size_t committed_rows{};
        std::size_t visible_rows{};
        std::size_t stale_expansions_removed{};
        bool committed{};
        bool selection_changed{};
        bool stale_selection_resolved{};
    };

    struct MutationResult final
    {
        ErrorCode error{ErrorCode::none};
        bool accepted{true};
        bool changed{};
    };

    struct Summary final
    {
        std::size_t total_rows{};
        std::size_t visible_rows{};
        std::size_t matching_rows{};
        std::size_t system_rows{};
        std::size_t task_rows{};
        std::size_t visible_system_rows{};
        std::size_t visible_task_rows{};
        std::size_t expanded_rows{};
        std::array<std::size_t, row_status_count> status_totals{};
        std::array<std::size_t, row_status_count> visible_status_totals{};
        bool selection_visible{};
    };

    struct CategorySummary final
    {
        std::string category{};
        std::size_t total_rows{};
        std::size_t visible_rows{};
        bool filter_active{};
    };

    struct VisibleRow final
    {
        const Row* row{};
        std::size_t depth{};
        bool has_children{};
        bool expanded{};
        bool expansion_forced{};
        bool selected{};
        bool direct_match{};
    };

    enum class NavigationCommand : std::uint8_t
    {
        first,
        previous,
        next,
        last,
        move_left,
        move_right,
        toggle_expansion
    };

    struct NavigationResult final
    {
        std::string selected_id{};
        bool changed{};
        bool selection_changed{};
        bool expansion_changed{};
    };

    class Controller final
    {
    public:
        explicit Controller(Limits limits = {});
        ~Controller();

        Controller(const Controller&) = delete;
        Controller& operator=(const Controller&) = delete;
        Controller(Controller&&) noexcept;
        Controller& operator=(Controller&&) noexcept;

        [[nodiscard]] const Limits& limits() const noexcept;
        [[nodiscard]] RefreshResult refresh(
            Adapter& adapter,
            RefreshOptions options = {});
        [[nodiscard]] RefreshResult replace_rows(
            std::span<const Row> rows,
            std::uint64_t source_revision = 0u,
            RefreshOptions options = {});

        [[nodiscard]] MutationResult set_text_filter(std::string_view filter);
        [[nodiscard]] MutationResult set_category_filter(
            std::string_view category,
            bool enabled);
        [[nodiscard]] MutationResult set_status_filter(RowStatus status, bool enabled);
        [[nodiscard]] MutationResult clear_filters();
        [[nodiscard]] MutationResult set_sort(Sort sort);

        [[nodiscard]] MutationResult select(std::string_view id);
        [[nodiscard]] bool clear_selection();
        [[nodiscard]] MutationResult set_expanded(std::string_view id, bool expanded);
        [[nodiscard]] MutationResult toggle_expanded(std::string_view id);
        [[nodiscard]] NavigationResult navigate(NavigationCommand command);

        [[nodiscard]] ContentState content_state() const noexcept;
        [[nodiscard]] const Error& error() const noexcept;
        [[nodiscard]] bool using_stale_rows() const noexcept;
        [[nodiscard]] std::uint64_t source_revision() const noexcept;
        [[nodiscard]] std::uint64_t view_revision() const noexcept;
        [[nodiscard]] std::string_view text_filter() const noexcept;
        [[nodiscard]] std::span<const std::string> category_filters() const noexcept;
        [[nodiscard]] bool status_filter_enabled(RowStatus status) const noexcept;
        [[nodiscard]] const Sort& sort() const noexcept;
        [[nodiscard]] std::string_view selected_id() const noexcept;
        [[nodiscard]] const Row* selected_row() const noexcept;
        [[nodiscard]] const Row* find_row(std::string_view id) const noexcept;
        [[nodiscard]] bool is_expanded(std::string_view id) const noexcept;
        [[nodiscard]] std::span<const Row> rows() const noexcept;
        [[nodiscard]] std::span<const VisibleRow> visible_rows() const noexcept;
        [[nodiscard]] const Summary& summary() const noexcept;
        [[nodiscard]] std::span<const CategorySummary> category_summaries() const noexcept;

    private:
        struct Implementation;
        std::unique_ptr<Implementation> implementation_;
    };
}
