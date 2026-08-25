module;
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module threadspire.learning:state_machine;

export {
namespace threadspire::ai::learning {

enum class TrainingState {
    queued,
    preparing,
    training,
    evaluating,
    promoting,
    rejected,
    completed,
    recoverable_failed,
};

struct SerializedTrainingJob final {
    std::string id;
    std::string coalesce_key;
    std::string skill;
    std::string candidate_id;
    std::string parent_generation;
    TrainingState state{TrainingState::queued};
    std::filesystem::path checkpoint_path;
    std::string message;
    std::uint64_t attempts{0};
    std::int64_t created_unix_ms{0};
    std::int64_t updated_unix_ms{0};
};

struct SubmitTrainingResult final {
    SerializedTrainingJob job;
    bool coalesced{false};
};

class PersistentTrainingQueue final {
public:
    explicit PersistentTrainingQueue(std::filesystem::path state_file);

    void load();
    [[nodiscard]] SubmitTrainingResult submit(
        std::string coalesce_key,
        std::string skill,
        std::string candidate_id = {},
        std::string parent_generation = {});
    [[nodiscard]] std::optional<SerializedTrainingJob> claim_next();
    [[nodiscard]] bool transition(
        std::string_view id,
        TrainingState expected,
        TrainingState next,
        std::filesystem::path checkpoint_path = {},
        std::string message = {});
    [[nodiscard]] std::size_t recover_inflight(std::string message);
    void set_interactive_busy(bool busy);
    [[nodiscard]] bool interactive_busy() const noexcept;
    [[nodiscard]] std::vector<SerializedTrainingJob> snapshot() const;
    [[nodiscard]] std::filesystem::path state_file() const;

private:
    void persist_locked() const;

    std::filesystem::path state_file_;
    mutable std::mutex mutex_;
    std::vector<SerializedTrainingJob> jobs_;
    bool interactive_busy_{false};
};

[[nodiscard]] bool write_atomic_checkpoint(
    const std::filesystem::path& path,
    std::string_view bytes,
    std::string& error) noexcept;
[[nodiscard]] std::string_view to_string(TrainingState state) noexcept;
[[nodiscard]] TrainingState training_state_from_string(std::string_view value) noexcept;
[[nodiscard]] bool valid_training_transition(
    TrainingState from,
    TrainingState to) noexcept;

} // namespace threadspire::ai::learning
}