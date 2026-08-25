module;
#include "module_prelude.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif

module threadspire.learning;
import threadspire.core;

namespace threadspire::ai::learning {
namespace {

[[nodiscard]] std::int64_t now_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

[[nodiscard]] std::string make_job_id() {
    static std::atomic_uint64_t serial{1U};
    return "training-" + std::to_string(now_ms()) + "-" +
        std::to_string(serial.fetch_add(1U, std::memory_order_relaxed));
}

[[nodiscard]] bool terminal(const TrainingState state) noexcept {
    return state == TrainingState::rejected || state == TrainingState::completed;
}

[[nodiscard]] bool inflight(const TrainingState state) noexcept {
    return state == TrainingState::preparing || state == TrainingState::training ||
        state == TrainingState::evaluating || state == TrainingState::promoting;
}

void atomic_replace(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
#ifdef _WIN32
    constexpr auto maximum_attempts = 100U;
    DWORD last_error = ERROR_SUCCESS;
    for (auto attempt = 0U; attempt < maximum_attempts; ++attempt) {
        if (::MoveFileExW(
                temporary.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
            return;
        }
        last_error = ::GetLastError();
        const auto transient_reader = last_error == ERROR_SHARING_VIOLATION ||
            last_error == ERROR_LOCK_VIOLATION || last_error == ERROR_ACCESS_DENIED;
        if (!transient_reader || attempt + 1U == maximum_attempts) {
            break;
        }
        // CRT readers can briefly omit FILE_SHARE_DELETE. Wait boundedly instead
        // of terminating the serialized training worker during a status poll.
        ::Sleep(5U);
    }
    throw std::system_error(
        static_cast<int>(last_error), std::system_category(),
        "unable to atomically replace state file");
#else
    std::filesystem::rename(temporary, destination);
#endif
}

[[nodiscard]] std::size_t size_value(const core::json::Value* value) noexcept {
    const auto number = core::json::number_or(value, 0.0);
    return number <= 0.0 ? 0U : static_cast<std::size_t>(number);
}

} // namespace

std::string_view to_string(const TrainingState state) noexcept {
    switch (state) {
    case TrainingState::queued: return "queued";
    case TrainingState::preparing: return "preparing";
    case TrainingState::training: return "training";
    case TrainingState::evaluating: return "evaluating";
    case TrainingState::promoting: return "promoting";
    case TrainingState::rejected: return "rejected";
    case TrainingState::completed: return "completed";
    case TrainingState::recoverable_failed: return "recoverable-failed";
    }
    return "queued";
}

TrainingState training_state_from_string(const std::string_view value) noexcept {
    if (value == "preparing") return TrainingState::preparing;
    if (value == "training") return TrainingState::training;
    if (value == "evaluating") return TrainingState::evaluating;
    if (value == "promoting") return TrainingState::promoting;
    if (value == "rejected") return TrainingState::rejected;
    if (value == "completed") return TrainingState::completed;
    if (value == "recoverable-failed") return TrainingState::recoverable_failed;
    return TrainingState::queued;
}

bool valid_training_transition(
    const TrainingState from,
    const TrainingState to) noexcept {
    switch (from) {
    case TrainingState::queued:
        return to == TrainingState::preparing || to == TrainingState::rejected;
    case TrainingState::preparing:
        return to == TrainingState::training || to == TrainingState::recoverable_failed ||
            to == TrainingState::rejected;
    case TrainingState::training:
        return to == TrainingState::evaluating || to == TrainingState::recoverable_failed;
    case TrainingState::evaluating:
        return to == TrainingState::promoting || to == TrainingState::rejected ||
            to == TrainingState::recoverable_failed;
    case TrainingState::promoting:
        return to == TrainingState::completed || to == TrainingState::rejected ||
            to == TrainingState::recoverable_failed;
    case TrainingState::recoverable_failed:
        return to == TrainingState::queued || to == TrainingState::preparing ||
            to == TrainingState::rejected;
    case TrainingState::rejected:
    case TrainingState::completed:
        return false;
    }
    return false;
}

PersistentTrainingQueue::PersistentTrainingQueue(std::filesystem::path state_file)
    : state_file_(std::move(state_file)) {
}

void PersistentTrainingQueue::load() {
    std::lock_guard lock(mutex_);
    jobs_.clear();
    std::ifstream stream(state_file_, std::ios::binary);
    if (!stream) {
        return;
    }
    const std::string text{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::string error;
    const auto document = core::json::try_parse(text, error);
    if (!document.has_value() || !document->is_array()) {
        throw std::runtime_error("persistent training state is unreadable: " + error);
    }
    for (std::size_t index = 0U; index < document->as_array()->size(); ++index) {
        const auto* value = document->at(index);
        if (value == nullptr || !value->is_object()) {
            continue;
        }
        SerializedTrainingJob job{
            .id = core::json::string_or(value->find("id")),
            .coalesce_key = core::json::string_or(value->find("coalesce_key")),
            .skill = core::json::string_or(value->find("skill")),
            .candidate_id = core::json::string_or(value->find("candidate_id")),
            .parent_generation = core::json::string_or(value->find("parent_generation")),
            .state = training_state_from_string(core::json::string_or(value->find("state"))),
            .checkpoint_path = std::filesystem::path(core::json::string_or(value->find("checkpoint_path"))),
            .message = core::json::string_or(value->find("message")),
            .attempts = size_value(value->find("attempts")),
            .created_unix_ms = static_cast<std::int64_t>(core::json::number_or(value->find("created_unix_ms"), 0.0)),
            .updated_unix_ms = static_cast<std::int64_t>(core::json::number_or(value->find("updated_unix_ms"), 0.0)),
        };
        if (!job.id.empty() && !job.coalesce_key.empty()) {
            jobs_.push_back(std::move(job));
        }
    }
}

SubmitTrainingResult PersistentTrainingQueue::submit(
    std::string coalesce_key,
    std::string skill,
    std::string candidate_id,
    std::string parent_generation) {
    if (coalesce_key.empty()) {
        throw std::invalid_argument("training coalesce key must not be empty");
    }
    std::lock_guard lock(mutex_);
    const auto duplicate = std::ranges::find_if(jobs_, [&coalesce_key](const SerializedTrainingJob& job) {
        return job.coalesce_key == coalesce_key && !terminal(job.state);
    });
    if (duplicate != jobs_.end()) {
        return {.job = *duplicate, .coalesced = true};
    }
    const auto timestamp = now_ms();
    SerializedTrainingJob job{
        .id = make_job_id(),
        .coalesce_key = std::move(coalesce_key),
        .skill = std::move(skill),
        .candidate_id = std::move(candidate_id),
        .parent_generation = std::move(parent_generation),
        .state = TrainingState::queued,
        .message = "queued beside the active model",
        .created_unix_ms = timestamp,
        .updated_unix_ms = timestamp,
    };
    jobs_.push_back(job);
    try {
        persist_locked();
    } catch (...) {
        jobs_.pop_back();
        throw;
    }
    return {.job = std::move(job), .coalesced = false};
}

std::optional<SerializedTrainingJob> PersistentTrainingQueue::claim_next() {
    std::lock_guard lock(mutex_);
    if (interactive_busy_ || std::ranges::any_of(jobs_, [](const SerializedTrainingJob& job) {
            return inflight(job.state);
        })) {
        return std::nullopt;
    }
    const auto next = std::ranges::find_if(jobs_, [](const SerializedTrainingJob& job) {
        return job.state == TrainingState::queued || job.state == TrainingState::recoverable_failed;
    });
    if (next == jobs_.end()) {
        return std::nullopt;
    }
    const auto previous = *next;
    next->state = TrainingState::preparing;
    ++next->attempts;
    next->updated_unix_ms = now_ms();
    next->message = next->checkpoint_path.empty()
        ? "preparing shadow candidate"
        : "preparing exact checkpoint resume";
    try {
        persist_locked();
    } catch (...) {
        *next = previous;
        throw;
    }
    return *next;
}

bool PersistentTrainingQueue::transition(
    const std::string_view id,
    const TrainingState expected,
    const TrainingState next,
    std::filesystem::path checkpoint_path,
    std::string message) {
    std::lock_guard lock(mutex_);
    const auto job = std::ranges::find(jobs_, id, &SerializedTrainingJob::id);
    if (job == jobs_.end() || job->state != expected ||
        !valid_training_transition(expected, next)) {
        return false;
    }
    const auto previous = *job;
    job->state = next;
    if (!checkpoint_path.empty()) {
        job->checkpoint_path = std::move(checkpoint_path);
    }
    job->message = std::move(message);
    job->updated_unix_ms = now_ms();
    try {
        persist_locked();
    } catch (...) {
        *job = previous;
        throw;
    }
    return true;
}

std::size_t PersistentTrainingQueue::recover_inflight(std::string message) {
    std::lock_guard lock(mutex_);
    const auto previous = jobs_;
    std::size_t recovered = 0U;
    for (auto& job : jobs_) {
        if (!inflight(job.state)) {
            continue;
        }
        job.state = TrainingState::recoverable_failed;
        job.message = message;
        job.updated_unix_ms = now_ms();
        ++recovered;
    }
    if (recovered > 0U) {
        try {
            persist_locked();
        } catch (...) {
            jobs_ = previous;
            throw;
        }
    }
    return recovered;
}

void PersistentTrainingQueue::set_interactive_busy(const bool busy) {
    std::lock_guard lock(mutex_);
    interactive_busy_ = busy;
}

bool PersistentTrainingQueue::interactive_busy() const noexcept {
    std::lock_guard lock(mutex_);
    return interactive_busy_;
}

std::vector<SerializedTrainingJob> PersistentTrainingQueue::snapshot() const {
    std::lock_guard lock(mutex_);
    return jobs_;
}

std::filesystem::path PersistentTrainingQueue::state_file() const {
    return state_file_;
}

void PersistentTrainingQueue::persist_locked() const {
    core::json::Value::Array records;
    records.reserve(jobs_.size());
    for (const auto& job : jobs_) {
        records.emplace_back(core::json::Value::Object{
            {"id", job.id}, {"coalesce_key", job.coalesce_key}, {"skill", job.skill},
            {"candidate_id", job.candidate_id}, {"parent_generation", job.parent_generation},
            {"state", std::string(to_string(job.state))},
            {"checkpoint_path", job.checkpoint_path.string()}, {"message", job.message},
            {"attempts", static_cast<double>(job.attempts)},
            {"created_unix_ms", static_cast<double>(job.created_unix_ms)},
            {"updated_unix_ms", static_cast<double>(job.updated_unix_ms)},
        });
    }
    std::filesystem::create_directories(state_file_.parent_path());
    auto temporary = state_file_;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << core::json::stringify(core::json::Value(std::move(records))) << '\n';
        stream.flush();
        if (!stream) {
            throw std::runtime_error("unable to serialize persistent training state");
        }
    }
    atomic_replace(temporary, state_file_);
}

bool write_atomic_checkpoint(
    const std::filesystem::path& path,
    const std::string_view bytes,
    std::string& error) noexcept {
    try {
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream.flush();
            if (!stream) {
                throw std::runtime_error("unable to write checkpoint bytes");
            }
        }
        atomic_replace(temporary, path);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace threadspire::ai::learning