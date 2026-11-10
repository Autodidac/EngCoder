#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace engcoder::controller {

// Safety-first routing policy: only explicitly simple requests are eligible for
// a smaller responder. Ambiguous, code and tool-heavy work goes to Foundation.
enum class TaskIntent { simple = 0, complex = 1, coding = 2 };
[[nodiscard]] std::string_view to_string(TaskIntent intent) noexcept;
[[nodiscard]] TaskIntent classify_task(std::string_view text);
// Exclude quantitative and sensitive tasks from tiny-model handoff. They stay
// on KeyMaker even if the text doesn't contain an obvious "math" keyword.
[[nodiscard]] bool requires_foundation_reasoning(std::string_view text);
// Only substantial tasks benefit from extra advisory inference. Ordinary code
// examples and straightforward requests go directly to their final responder.
[[nodiscard]] bool warrants_specialist_handoff(std::string_view text);

// Trainable feature/prediction *stub*, not a pretrained neural JEPA model.
// Shadow learns only to imitate the conservative policy using privacy-preserving
// numeric examples, and never overrides it without future verified evaluation.
class PredictiveRouteShadow final {
public:
    explicit PredictiveRouteShadow(std::filesystem::path data_root);
    void observe(std::string_view text, TaskIntent policy_label);
    [[nodiscard]] std::pair<TaskIntent, double> predict(std::string_view text) const;
    [[nodiscard]] std::size_t examples_seen() const noexcept;
private:
    using Features = std::array<double, 9>;
    [[nodiscard]] static Features features(std::string_view text);
    void train(const Features& values, TaskIntent label);
    std::filesystem::path journal_;
    mutable std::mutex mutex_;
    std::array<Features, 3> weights_{};
    std::size_t examples_{};
};
} // namespace engcoder::controller
