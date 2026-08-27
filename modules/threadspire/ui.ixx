module;
#include <Windows.h>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module threadspire.ui;

export import threadspire.core;
export import threadspire.learning;
export import threadspire.mcp;
export import threadspire.media;
export import threadspire.models;
export import threadspire.platform;
export import threadspire.providers;
export import threadspire.scheduler;
export import threadspire.storage;
export import threadspire.tasks;

export {
namespace threadspire::ai::app {

class MainWindow final {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    [[nodiscard]] bool create(int show_command);
    [[nodiscard]] HWND handle() const noexcept;

private:
    enum class Page {
        chat,
        coding,
        models,
        learning,
        skills,
        long_horizon,
        logs,
    };

    enum class RequestSurface {
        assistant,
        workspace_coding,
        coding,
    };

    enum class InferenceSource {
        automatic,
        local_bridge,
        custom_model,
    };

    enum class WorkspaceTask {
        automatic,
        ask,
        code_help,
        repository,
        image,
        audio,
        tool,
    };

    enum class LifecycleAction {
        none,
        install_backend,
        verify_backend,
        bootstrap_model,
        start_host,
    };
    enum class ResponseReview {
        unreviewed,
        accepted,
        rejected,
        blocked,
        already_known,
    };

    struct ChatSummary final {
        std::string id;
        std::string title;
        std::filesystem::path path;
        std::filesystem::file_time_type modified{};
    };

    struct ResponseTurn final {
        std::string prompt;
        std::string response;
        std::string earlier_context;
        routing::TaskAnalysis analysis;
        std::string model_id;
        std::vector<std::string> models_used;
        std::vector<std::string> tool_events;
        std::vector<std::filesystem::path> input_files;
        RequestSurface surface{RequestSurface::assistant};
        bool learning_weight_eligible{false};
        std::string learning_destination;
        std::filesystem::path repository_source;
        std::filesystem::path repository_staging;
        std::filesystem::path repository_manifest;
        std::string repository_source_head;
        std::string repository_candidate_commit;
        std::string repository_promoted_branch;
        std::string repository_build_command;
        std::string repository_test_command;
        bool repository_compiler_passed{false};
        bool repository_tests_passed{false};
        ResponseReview review{ResponseReview::unreviewed};
    };

    static LRESULT CALLBACK window_procedure(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    bool register_window_class();
    void create_controls();
    void create_chat_page();
    void create_coding_page();
    void create_models_page();
    void create_skills_page();
    void create_learning_page();
    void create_long_horizon_page();
    void create_logs_page();
    void layout_controls();
    void show_page(Page page);
    void update_periodic_state();
    void drive_automatic_lifecycle();
    void update_internal_model_lifecycle_ui();
    void handle_lifecycle_completion();
    void schedule_lifecycle_retry(std::string message);
    void update_chat_result();
    void update_discovery_result();
    void update_mcp_result();
    void update_piggyback_result();
    void update_learning_report();
    void update_training_library();
    void update_user_memory_controls(std::string_view selected_id = {});
    void select_user_memory();
    void save_user_memory();
    void forget_user_memory();
    void update_skill_report();
    void refresh_capability_pack_controls();
    void browse_capability_pack_source();
    void install_selected_capability_pack();
    void update_capability_pack_result();
    void update_media_result();
    void update_storage_report(bool force = false);
    void update_storage_cleanup_result();
    void run_storage_cleanup(bool dry_run);
    void update_log_view();
    void flush_stream_text();
    void handle_command(int identifier);
    void update_workspace_mode();
    void refresh_model_selectors();
    void update_inference_source(int source_identifier);
    void update_model_selection(int source_identifier);
    void stop_active_request();
    void yield_automatic_bootstrap_to_foreground();
    void clear_workspace();
    void load_chat_sessions();
    void refresh_chat_session_selector();
    void persist_current_chat();
    void load_chat_session(std::string_view id);
    void switch_chat_session();
    void delete_current_chat();
    void edit_selected_prompt();
    void retry_selected_response();
    void attach_files();
    void update_attachment_controls();
    void open_local_ai_page();
    void recreate_ui_font();

    void preview_route();
    void send_prompt();
    void send_coding_prompt();
    void run_media_request(backend::MediaKind kind);
    void send_request(RequestSurface surface);
    [[nodiscard]] bool handle_conversation_command(std::string_view prompt);
    void append_local_control_turn(
        std::string_view prompt,
        std::string_view heading,
        std::string_view response,
        std::wstring_view detail,
        std::string_view destination,
        std::wstring_view status);
    void accept_last_response();
    void reject_last_response();
    void copy_selected_response();
    void show_selected_learning_preview();
    void update_response_turn_controls();
    void select_response_turn();
    std::size_t apply_hard_corrections_for(const ResponseTurn& approved);
    void reconcile_response_reviews_from_memory();
    [[nodiscard]] ResponseTurn* selected_response_turn();
    [[nodiscard]] const ResponseTurn* selected_response_turn() const;
    [[nodiscard]] memory::Episode learning_episode_for(
        const ResponseTurn& turn,
        bool accepted) const;
    [[nodiscard]] std::wstring learning_preview_text(const ResponseTurn& turn) const;
    void discover_local_ai();
    void add_manual_local_ai();
    void connect_mcp();
    void generate_piggyback(bool automatic = false, bool trusted_only = false);
    void handle_training_target_change();
    void browse_custom_training_data();
    void import_custom_training_data();
    void compile_and_train_model();
    void create_internal_model(bool bootstrap, bool automatic = false);
    void install_backend(bool automatic = false);
    void verify_backend(bool automatic = false);
    void start_backend(bool automatic = false);
    void stop_backend();
    void browse_python();
    void load_models_editor();
    void save_models_editor();
    void bootstrap_core_skills();
    void queue_skill_update();
    void rollback_skill();
    [[nodiscard]] std::optional<runtime::CapabilityPackDescriptor> selected_capability_pack() const;
    void audit_selected_capability_pack();
    void browse_coding_repository();
    void run_coding_agent();
    void browse_repository();
    void start_long_horizon();
    void cancel_long_horizon();
    void update_long_horizon_result();
    void select_page_tab(Page page);

    [[nodiscard]] models::ModelRegistry runtime_model_snapshot() const;
    [[nodiscard]] std::optional<models::ModelProfile> best_coding_model() const;
    [[nodiscard]] WorkspaceTask workspace_task() const noexcept;
    [[nodiscard]] WorkspaceTask effective_workspace_task(std::string_view prompt) const;
    [[nodiscard]] bool workspace_coding_mode() const;
    [[nodiscard]] bool model_supports_workspace_task(const models::ModelProfile& model) const noexcept;
    [[nodiscard]] bool model_supports_workspace_task(
        const models::ModelProfile& model,
        WorkspaceTask task) const noexcept;
    [[nodiscard]] std::optional<std::string> selected_model_id() const;
    [[nodiscard]] bool source_accepts(const models::ModelProfile& model) const noexcept;
    void apply_source_filter(models::ModelRegistry& registry) const;
    void apply_model_choice_filter(models::ModelRegistry& registry) const;
    [[nodiscard]] std::wstring runtime_summary(
        const models::ModelRegistry& registry,
        bool custom_host_running) const;
    [[nodiscard]] std::string requested_runtime_heading() const;
    [[nodiscard]] std::string actual_runtime_heading(std::string_view model_id) const;
    [[nodiscard]] bool is_workspace_surface(RequestSurface surface) const noexcept;
    [[nodiscard]] bool is_coding_surface(RequestSurface surface) const noexcept;
    [[nodiscard]] std::string selected_training_skill() const;
    [[nodiscard]] std::filesystem::path selected_training_dataset() const;
    [[nodiscard]] std::string training_cursor_key(
        std::string_view skill,
        const std::filesystem::path& dataset) const;
    void select_training_skill(std::string_view skill);
    [[nodiscard]] std::size_t piggyback_cursor_for(std::string_view key);
    void persist_piggyback_cursor(std::string_view key, std::size_t cursor);
    void persist_training_configuration() const;

    HWND create_control(
        const wchar_t* class_name,
        const wchar_t* text,
        DWORD style,
        DWORD extended_style,
        int identifier,
        HWND parent);
    [[nodiscard]] HWND control(int identifier) const;
    [[nodiscard]] std::wstring control_text(int identifier) const;
    void set_control_text(int identifier, std::wstring text) const;
    void append_control_text(int identifier, std::wstring_view text) const;
    [[nodiscard]] bool checked(int identifier) const;
    void set_checked(int identifier, bool value) const;
    void apply_font(HWND control) const;
    void set_status(std::wstring text) const;
    void set_active_log(std::filesystem::path path);
    void open_path(const std::filesystem::path& path) const;
    [[nodiscard]] std::optional<std::filesystem::path> choose_python() const;
    [[nodiscard]] std::optional<std::filesystem::path> choose_training_dataset() const;
    [[nodiscard]] std::vector<std::filesystem::path> choose_files() const;
    [[nodiscard]] std::optional<std::filesystem::path> choose_directory(
        std::wstring_view title) const;

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    HFONT font_{nullptr};
    HBRUSH window_brush_{nullptr};
    HBRUSH card_brush_{nullptr};
    HBRUSH edit_brush_{nullptr};
    unsigned int dpi_{96};
    Page page_{Page::chat};
    AppPaths paths_;
    memory::UserMemoryStore user_memory_store_;
    std::filesystem::path selected_python_;
    std::filesystem::path selected_pack_source_;
    std::filesystem::path selected_capability_pack_source_;
    std::vector<std::string> capability_pack_ids_;
    std::string selected_capability_pack_id_;
    std::string active_capability_pack_name_;
    models::ModelRegistry model_registry_;
    mutable std::mutex model_mutex_;
    std::shared_ptr<mcp::Client> mcp_client_;
    std::unique_ptr<learning::ContinualLearningService> learning_service_;
    runtime::BackendServer backend_server_;
    util::AsyncOperation backend_operation_;
    LifecycleAction lifecycle_action_{LifecycleAction::none};
    bool automatic_lifecycle_enabled_{true};
    bool automatic_training_enabled_{true};
    bool bootstrap_priority_lease_{false};
    bool backend_verified_{false};
    bool backend_repair_required_{false};
    std::size_t lifecycle_failures_{0};
    std::chrono::steady_clock::time_point lifecycle_retry_at_{};
    std::string lifecycle_last_error_;
    util::AsyncOperation discovery_operation_;
    util::AsyncOperation mcp_operation_;
    util::AsyncOperation piggyback_operation_;
    util::AsyncOperation chat_operation_;
    util::AsyncOperation long_horizon_operation_;
    util::AsyncOperation media_operation_;
    util::AsyncOperation storage_operation_;
    util::AsyncOperation storage_cleanup_operation_;
    util::AsyncOperation capability_pack_operation_;
    std::unordered_map<int, HWND> controls_;
    std::vector<HWND> page_windows_;
    std::filesystem::path active_log_;
    mutable std::mutex result_mutex_;
    std::optional<backend::AgentResult> pending_chat_result_;
    std::optional<RequestSurface> pending_chat_surface_;
    std::optional<models::ProviderDiscoveryReport> pending_discovery_report_;
    std::optional<mcp::ConnectionResult> pending_mcp_result_;
    std::optional<learning::PiggybackResult> pending_piggyback_result_;
    std::optional<long_horizon::Result> pending_long_horizon_result_;
    std::optional<backend::MediaResult> pending_media_result_;
    std::optional<runtime::StorageSnapshot> pending_storage_snapshot_;
    std::optional<runtime::StorageCleanupResult> pending_storage_cleanup_result_;
    std::optional<runtime::CapabilityPackInstallResult> pending_capability_pack_result_;
    std::optional<runtime::CapabilityPackAudit> pending_capability_pack_audit_;
    std::optional<runtime::CapabilityPackAudit> selected_capability_pack_audit_;
    mutable std::mutex stream_mutex_;
    std::string pending_stream_text_;
    int stream_target_control_{0};
    std::vector<std::string> model_selector_ids_;
    std::vector<std::string> user_memory_ids_;
    std::string preferred_model_id_;
    InferenceSource inference_source_{InferenceSource::automatic};
    std::string workspace_transcript_;
    std::vector<std::pair<std::string, std::string>> conversation_history_;
    std::size_t pending_transcript_heading_offset_{std::string::npos};
    std::vector<ResponseTurn> response_turns_;
    std::vector<ChatSummary> chat_sessions_;
    std::string current_chat_id_;
    std::vector<std::filesystem::path> attached_files_;
    std::vector<std::filesystem::path> last_input_files_;
    std::size_t selected_response_turn_index_{std::string::npos};
    std::size_t pending_transcript_heading_length_{0};
    bool interactive_model_verified_{false};
    bool backend_running_last_{false};
    bool bootstrap_running_last_{false};
    std::string last_prompt_;
    std::string last_response_;
    routing::TaskAnalysis last_analysis_;
    std::string last_model_id_;
    std::vector<std::string> last_models_used_;
    std::vector<std::string> last_tool_events_;
    RequestSurface last_response_surface_{RequestSurface::assistant};
    bool chat_result_consumed_{true};
    bool long_horizon_from_coding_{false};
    std::unordered_map<std::string, std::size_t> piggyback_seed_offsets_;
    std::string active_piggyback_skill_;
    std::string active_piggyback_cursor_key_;
    bool active_piggyback_trusted_only_{false};
    std::string training_target_{"english-foundation"};
    std::string custom_training_skill_{"custom-skill"};
    std::filesystem::path custom_training_dataset_;
    std::chrono::steady_clock::time_point next_piggyback_at_{};
    std::chrono::steady_clock::time_point next_storage_refresh_{};
    std::uintmax_t previous_storage_bytes_{0};
    std::uintmax_t previous_models_bytes_{0};
    std::uintmax_t previous_ram_bytes_{0};
    bool storage_sample_available_{false};
    std::wstring storage_cleanup_summary_{L"No cleanup has run. Preview shows exactly what would be removed."};
    bool media_result_pending_{false};
};

} // namespace threadspire::ai::app
}
