#include <Windows.h>
#include <bcrypt.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <regex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

import threadspire.core;

namespace {

using threadspire::ai::core::json::Value;
namespace json = threadspire::ai::core::json;

constexpr int pad_id = 0;
constexpr int bos_id = 1;
constexpr int eos_id = 2;
constexpr int system_id = 3;
constexpr int user_id = 4;
constexpr int assistant_id = 5;
constexpr int separator_id = 6;
constexpr int byte_offset = 16;
constexpr int byte_vocab_size = byte_offset + 256;

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to read " + path.string());
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool safe_leaf(const std::string_view value) noexcept {
    return !value.empty() && value != "." && value != ".." &&
        value.find('/') == std::string_view::npos &&
        value.find('\\') == std::string_view::npos &&
        value.find(':') == std::string_view::npos;
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::string trim(std::string value) {
    const auto visible = [](const unsigned char character) {
        return std::isspace(character) == 0;
    };
    const auto first = std::ranges::find_if(value, visible);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), visible).base();
    return std::string(first, last);
}

[[nodiscard]] std::string normalize_space(const std::string_view value) {
    std::string result;
    bool pending_space = false;
    for (const unsigned char character : value) {
        if (std::isspace(character) != 0) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(' ');
        }
        pending_space = false;
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

[[nodiscard]] std::vector<std::string> ascii_words(const std::string_view value) {
    std::vector<std::string> result;
    std::string word;
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0) {
            word.push_back(static_cast<char>(std::tolower(character)));
        } else if (!word.empty()) {
            result.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) {
        result.push_back(std::move(word));
    }
    return result;
}

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept {
    const auto continuation = [](const unsigned char character) noexcept {
        return (character & 0xC0U) == 0x80U;
    };
    std::size_t index = 0;
    while (index < value.size()) {
        const auto lead = static_cast<unsigned char>(value[index]);
        if (lead <= 0x7FU) {
            ++index;
            continue;
        }
        if (lead >= 0xC2U && lead <= 0xDFU) {
            if (index + 1U >= value.size() ||
                !continuation(static_cast<unsigned char>(value[index + 1U]))) {
                return false;
            }
            index += 2U;
            continue;
        }
        if (lead >= 0xE0U && lead <= 0xEFU) {
            if (index + 2U >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1U]);
            const auto third = static_cast<unsigned char>(value[index + 2U]);
            const auto second_valid = lead == 0xE0U ? second >= 0xA0U && second <= 0xBFU :
                lead == 0xEDU ? second >= 0x80U && second <= 0x9FU : continuation(second);
            if (!second_valid || !continuation(third)) {
                return false;
            }
            index += 3U;
            continue;
        }
        if (lead >= 0xF0U && lead <= 0xF4U) {
            if (index + 3U >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1U]);
            const auto third = static_cast<unsigned char>(value[index + 2U]);
            const auto fourth = static_cast<unsigned char>(value[index + 3U]);
            const auto second_valid = lead == 0xF0U ? second >= 0x90U && second <= 0xBFU :
                lead == 0xF4U ? second >= 0x80U && second <= 0x8FU : continuation(second);
            if (!second_valid || !continuation(third) || !continuation(fourth)) {
                return false;
            }
            index += 4U;
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] std::string sha256_hex(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_bytes = 0;
    DWORD digest_bytes = 0;
    DWORD copied = 0;
    std::vector<UCHAR> object;
    std::vector<UCHAR> digest;
    const auto close = [&] {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    };
    try {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes), &copied, 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&digest_bytes), sizeof(digest_bytes), &copied, 0) < 0) {
            throw std::runtime_error("Windows SHA-256 initialization failed");
        }
        object.resize(object_bytes);
        digest.resize(digest_bytes);
        if (BCryptCreateHash(algorithm, &hash, object.data(), object_bytes, nullptr, 0, 0) < 0) {
            throw std::runtime_error("Windows SHA-256 hash creation failed");
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("unable to hash " + path.string());
        }
        std::vector<char> buffer(1024U * 1024U);
        while (stream) {
            stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = stream.gcount();
            if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(count), 0) < 0) {
                throw std::runtime_error("Windows SHA-256 update failed");
            }
        }
        if (BCryptFinishHash(hash, digest.data(), digest_bytes, 0) < 0) {
            throw std::runtime_error("Windows SHA-256 finalize failed");
        }
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(digest.size() * 2U);
        for (const auto byte : digest) {
            result.push_back(digits[byte >> 4U]);
            result.push_back(digits[byte & 0x0FU]);
        }
        close();
        return result;
    } catch (...) {
        close();
        throw;
    }
}

struct TensorView final {
    const float* data{nullptr};
    std::vector<std::size_t> shape;

    [[nodiscard]] std::size_t elements() const noexcept {
        return std::accumulate(shape.begin(), shape.end(), std::size_t{1}, std::multiplies<>());
    }
};

class SafeTensorFile final {
public:
    explicit SafeTensorFile(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("unable to open model weights: " + path.string());
        }
        const auto end = stream.tellg();
        if (end < static_cast<std::streamoff>(8)) {
            throw std::runtime_error("safetensors file is truncated");
        }
        bytes_.resize(static_cast<std::size_t>(end));
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(bytes_.data()), static_cast<std::streamsize>(bytes_.size()));
        if (!stream) {
            throw std::runtime_error("unable to read model weights");
        }
        std::uint64_t header_bytes = 0;
        std::memcpy(&header_bytes, bytes_.data(), sizeof(header_bytes));
        if constexpr (std::endian::native == std::endian::big) {
            header_bytes = std::byteswap(header_bytes);
        }
        if (header_bytes == 0 || header_bytes > 16U * 1024U * 1024U ||
            header_bytes + 8U > bytes_.size()) {
            throw std::runtime_error("invalid safetensors header length");
        }
        data_offset_ = 8U + static_cast<std::size_t>(header_bytes);
        const auto header = json::parse(std::string_view(
            reinterpret_cast<const char*>(bytes_.data() + 8U),
            static_cast<std::size_t>(header_bytes)));
        const auto* object = header.as_object();
        if (object == nullptr) {
            throw std::runtime_error("safetensors header is not an object");
        }
        for (const auto& [name, entry] : *object) {
            if (name == "__metadata__") {
                continue;
            }
            if (json::string_or(entry.find("dtype")) != "F32") {
                throw std::runtime_error("native inference requires F32 tensor: " + name);
            }
            const auto* shape_json = entry.find("shape");
            const auto* offsets_json = entry.find("data_offsets");
            if (shape_json == nullptr || offsets_json == nullptr ||
                shape_json->as_array() == nullptr || offsets_json->as_array() == nullptr ||
                offsets_json->as_array()->size() != 2U) {
                throw std::runtime_error("invalid safetensors entry: " + name);
            }
            TensorView view;
            for (const auto& dimension : *shape_json->as_array()) {
                const auto number = dimension.as_number(-1.0);
                if (number <= 0.0 || number > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
                    throw std::runtime_error("invalid tensor dimension: " + name);
                }
                view.shape.push_back(static_cast<std::size_t>(number));
            }
            const auto begin = static_cast<std::size_t>((*offsets_json->as_array())[0].as_number(-1.0));
            const auto finish = static_cast<std::size_t>((*offsets_json->as_array())[1].as_number(-1.0));
            const auto required = view.elements() * sizeof(float);
            if (finish < begin || finish - begin != required || data_offset_ + finish > bytes_.size()) {
                throw std::runtime_error("invalid tensor byte range: " + name);
            }
            view.data = reinterpret_cast<const float*>(bytes_.data() + data_offset_ + begin);
            tensors_.emplace(name, std::move(view));
        }
    }

    [[nodiscard]] const TensorView& tensor(
        const std::string& name,
        const std::initializer_list<std::size_t> expected) const {
        const auto found = tensors_.find(name);
        if (found == tensors_.end() || found->second.shape != std::vector<std::size_t>(expected)) {
            throw std::runtime_error("missing or incompatible model tensor: " + name);
        }
        return found->second;
    }

private:
    std::vector<std::byte> bytes_;
    std::size_t data_offset_{0};
    std::unordered_map<std::string, TensorView> tensors_;
};

struct Message final {
    std::string role;
    std::string content;
};

struct ModelConfig final {
    std::size_t context_length{0};
    std::size_t width{0};
    std::size_t layers{0};
    std::size_t heads{0};
    std::size_t ffn_hidden{0};
    std::size_t expert_hidden{0};
    float rope_base{10000.0F};
    std::vector<std::string> skills;
};

struct SwiWeights final {
    TensorView gate;
    TensorView up;
    TensorView down;
};

struct BlockWeights final {
    TensorView attention_norm;
    TensorView qkv;
    TensorView attention_output;
    TensorView ffn_norm;
    SwiWeights ffn;
    TensorView expert_norm;
    std::unordered_map<std::string, SwiWeights> experts;
};

struct BlockCache final {
    std::vector<float> keys;
    std::vector<float> values;
};

struct GenerationResult final {
    std::string text;
    std::string quality_reason;
    std::string generation;
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    int attempts{1};
    double elapsed_ms{0.0};
    bool accepted{false};
    bool verified{false};
};

void matvec(const TensorView& matrix, const std::span<const float> input, std::span<float> output) {
    if (matrix.shape.size() != 2U || matrix.shape[1] != input.size() ||
        matrix.shape[0] != output.size()) {
        throw std::runtime_error("internal native matrix shape mismatch");
    }
    const auto columns = input.size();
    for (std::size_t row = 0; row < output.size(); ++row) {
        const auto* weights = matrix.data + row * columns;
        float sum0 = 0.0F;
        float sum1 = 0.0F;
        float sum2 = 0.0F;
        float sum3 = 0.0F;
        std::size_t column = 0;
        for (; column + 4U <= columns; column += 4U) {
            sum0 += weights[column] * input[column];
            sum1 += weights[column + 1U] * input[column + 1U];
            sum2 += weights[column + 2U] * input[column + 2U];
            sum3 += weights[column + 3U] * input[column + 3U];
        }
        float sum = (sum0 + sum1) + (sum2 + sum3);
        for (; column < columns; ++column) {
            sum += weights[column] * input[column];
        }
        output[row] = sum;
    }
}

void rms_norm(
    const std::span<const float> input,
    const TensorView& weight,
    std::span<float> output) {
    double squares = 0.0;
    for (const auto value : input) {
        squares += static_cast<double>(value) * static_cast<double>(value);
    }
    const auto scale = static_cast<float>(1.0 / std::sqrt(squares / input.size() + 1.0e-6));
    for (std::size_t index = 0; index < input.size(); ++index) {
        output[index] = input[index] * scale * weight.data[index];
    }
}

[[nodiscard]] std::vector<float> swiglu(
    const SwiWeights& weights,
    const std::span<const float> input) {
    const auto hidden = weights.gate.shape[0];
    std::vector<float> gate(hidden);
    std::vector<float> up(hidden);
    matvec(weights.gate, input, gate);
    matvec(weights.up, input, up);
    for (std::size_t index = 0; index < hidden; ++index) {
        const auto value = gate[index];
        gate[index] = (value / (1.0F + std::exp(-value))) * up[index];
    }
    std::vector<float> result(weights.down.shape[0]);
    matvec(weights.down, gate, result);
    return result;
}

class NativeModel final {
public:
    explicit NativeModel(std::filesystem::path state_root)
        : state_root_(std::move(state_root)) {
        load_curated_responses();
        try {
            generation_path_ = resolve_generation();
            manifest_ = json::parse(read_text(generation_path_ / "manifest.json"));
            config_ = load_config(generation_path_ / json::string_or(
                manifest_.find("config_file"), "config.json"));
            weights_ = validated_weights_path();
            safetensors_ = std::make_unique<SafeTensorFile>(weights_);
            if (json::string_or(manifest_.find("format")) != "threadspire.internal.v1" ||
                json::string_or(manifest_.find("generation")) != generation_) {
                throw std::runtime_error("active model manifest is incompatible");
            }
            embedding_ = safetensors_->tensor("embedding.weight", {byte_vocab_size, config_.width});
            final_norm_ = safetensors_->tensor("final_norm.weight", {config_.width});
            for (std::size_t layer = 0; layer < config_.layers; ++layer) {
                const auto prefix = "blocks." + std::to_string(layer) + ".";
                BlockWeights block{
                    .attention_norm = safetensors_->tensor(prefix + "attention_norm.weight", {config_.width}),
                    .qkv = safetensors_->tensor(prefix + "attention.qkv.weight", {config_.width * 3U, config_.width}),
                    .attention_output = safetensors_->tensor(prefix + "attention.output.weight", {config_.width, config_.width}),
                    .ffn_norm = safetensors_->tensor(prefix + "ffn_norm.weight", {config_.width}),
                    .ffn = {
                        .gate = safetensors_->tensor(prefix + "ffn.gate.weight", {config_.ffn_hidden, config_.width}),
                        .up = safetensors_->tensor(prefix + "ffn.up.weight", {config_.ffn_hidden, config_.width}),
                        .down = safetensors_->tensor(prefix + "ffn.down.weight", {config_.width, config_.ffn_hidden}),
                    },
                    .expert_norm = safetensors_->tensor(prefix + "expert_norm.weight", {config_.width}),
                };
                for (const auto& skill : config_.skills) {
                    const auto expert = prefix + "experts." + skill + ".";
                    block.experts.emplace(skill, SwiWeights{
                        .gate = safetensors_->tensor(expert + "gate.weight", {config_.expert_hidden, config_.width}),
                        .up = safetensors_->tensor(expert + "up.weight", {config_.expert_hidden, config_.width}),
                        .down = safetensors_->tensor(expert + "down.weight", {config_.width, config_.expert_hidden}),
                    });
                }
                blocks_.push_back(std::move(block));
            }
            neural_ready_ = true;
        } catch (const std::exception&) {
            generation_ = "foundation";
            generation_path_.clear();
            weights_.clear();
            safetensors_.reset();
            blocks_.clear();
        }
    }

    [[nodiscard]] GenerationResult complete(
        const std::vector<Message>& messages,
        const std::string_view lane,
        std::size_t maximum_new_tokens,
        const float temperature) const {
        const auto started = std::chrono::steady_clock::now();
        if (const auto verified = verified_response(messages); verified.has_value()) {
            return {
                .text = *verified,
                .quality_reason = "verified local response",
                .generation = generation_,
                .prompt_tokens = prompt_tokens(messages).size(),
                .completion_tokens = verified->size(),
                .attempts = 1,
                .elapsed_ms = elapsed_ms(started),
                .accepted = true,
                .verified = true,
            };
        }
        if (!neural_ready_) {
            return {
                .quality_reason = "the native foundation has not learned this request yet",
                .generation = generation_,
                .prompt_tokens = prompt_tokens(messages).size(),
                .completion_tokens = 0,
                .attempts = 1,
                .elapsed_ms = elapsed_ms(started),
                .accepted = false,
            };
        }
        maximum_new_tokens = std::clamp<std::size_t>(
            maximum_new_tokens, 1U, std::max<std::size_t>(1U, config_.context_length - 1U));
        const std::array temperatures{temperature, 0.0F};
        GenerationResult result;
        const auto attempts = temperature > 0.0F ? 2 : 1;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            result = generate(messages, lane, maximum_new_tokens, temperatures[attempt]);
            result.attempts = attempt + 1;
            if (result.accepted) {
                break;
            }
        }
        result.elapsed_ms = elapsed_ms(started);
        return result;
    }

    [[nodiscard]] const std::string& generation() const noexcept { return generation_; }
    [[nodiscard]] std::uintmax_t weights_bytes() const noexcept {
        if (!neural_ready_ || weights_.empty()) {
            return 0U;
        }
        std::error_code error;
        return std::filesystem::file_size(weights_, error);
    }

private:
    [[nodiscard]] static double elapsed_ms(const std::chrono::steady_clock::time_point started) {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    }

    [[nodiscard]] std::filesystem::path resolve_generation() {
        const auto internal = state_root_ / "models" / "internal";
        const auto pointer = json::parse(read_text(internal / "active.json"));
        generation_ = json::string_or(pointer.find("generation"));
        if (!safe_leaf(generation_)) {
            throw std::runtime_error("invalid active model generation");
        }
        const auto path = internal / "generations" / generation_;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
            throw std::runtime_error("active model generation is missing or unsafe");
        }
        return path;
    }

    [[nodiscard]] static ModelConfig load_config(const std::filesystem::path& path) {
        const auto document = json::parse(read_text(path));
        ModelConfig config{
            .context_length = static_cast<std::size_t>(json::number_or(document.find("context_length"))),
            .width = static_cast<std::size_t>(json::number_or(document.find("d_model"))),
            .layers = static_cast<std::size_t>(json::number_or(document.find("n_layers"))),
            .heads = static_cast<std::size_t>(json::number_or(document.find("n_heads"))),
            .ffn_hidden = static_cast<std::size_t>(json::number_or(document.find("ffn_hidden"))),
            .expert_hidden = static_cast<std::size_t>(json::number_or(document.find("expert_hidden"))),
            .rope_base = static_cast<float>(json::number_or(document.find("rope_base"), 10000.0)),
        };
        const auto* skills = document.find("skills");
        if (skills != nullptr && skills->as_array() != nullptr) {
            for (const auto& skill : *skills->as_array()) {
                config.skills.push_back(json::string_or(&skill));
            }
        }
        if (static_cast<std::size_t>(json::number_or(document.find("vocab_size"))) != byte_vocab_size ||
            config.context_length < 128U || config.width == 0U || config.layers == 0U ||
            config.heads == 0U || config.width % config.heads != 0U ||
            config.ffn_hidden < config.width || config.expert_hidden == 0U || config.skills.empty()) {
            throw std::runtime_error("unsupported native model configuration");
        }
        return config;
    }

    [[nodiscard]] std::filesystem::path validated_weights_path() const {
        const auto name = json::string_or(manifest_.find("weights_file"), "weights.safetensors");
        if (!safe_leaf(name)) {
            throw std::runtime_error("invalid active weights filename");
        }
        const auto path = generation_path_ / name;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
            throw std::runtime_error("active model weights are missing or unsafe");
        }
        const auto expected = lower_ascii(json::string_or(manifest_.find("weights_sha256")));
        if (expected.size() != 64U || sha256_hex(path) != expected) {
            throw std::runtime_error("active model weights checksum mismatch");
        }
        return path;
    }

    [[nodiscard]] std::vector<std::string> lane_skills(const std::string_view lane) const {
        const auto normalized = lower_ascii(std::string(lane));
        if (normalized == "coder" || normalized == "coding" || normalized == "cpp23") {
            return {"english-core", "cpp23-core", "cpp23-debugging"};
        }
        if (normalized == "thinker" || normalized == "planning") {
            return {"english-core", "planning", "cpp23-core"};
        }
        if (normalized == "reviewer" || normalized == "verify" || normalized == "verifier") {
            return {"english-core", "cpp23-debugging"};
        }
        return {"english-core"};
    }

    [[nodiscard]] static std::vector<int> prompt_tokens(const std::vector<Message>& messages) {
        std::vector<int> tokens{bos_id};
        for (const auto& message : messages) {
            const auto role = lower_ascii(message.role);
            tokens.push_back(role == "system" ? system_id : role == "assistant" ? assistant_id : user_id);
            for (const unsigned char byte : message.content) {
                tokens.push_back(byte_offset + byte);
            }
            tokens.push_back(separator_id);
        }
        if (messages.empty() || lower_ascii(messages.back().role) != "assistant") {
            tokens.push_back(assistant_id);
        }
        return tokens;
    }

    void load_curated_responses() {
        static constexpr std::array core_words{
            "a", "an", "and", "are", "as", "at", "be", "because", "but", "by",
            "can", "condition", "context", "do", "for", "from", "has", "have",
            "hello", "help", "hey", "how", "i", "if", "in", "is", "it", "local",
            "me", "model", "not", "of", "on", "or", "please", "ready", "resource",
            "resources", "should", "that", "the", "this", "to", "was", "we", "what",
            "when", "where", "which", "will", "with", "working", "you", "your", "yes"
        };
        vocabulary_.insert(core_words.begin(), core_words.end());
        const auto path = state_root_ / "data" / "bootstrap.jsonl";
        std::ifstream stream(path, std::ios::binary);
        std::string line;
        while (std::getline(stream, line)) {
            std::string error;
            const auto record = json::try_parse(line, error);
            if (!record.has_value() ||
                (record->find("accepted") != nullptr && !json::bool_or(record->find("accepted"))) ||
                (record->find("eligible_for_training") != nullptr &&
                    !json::bool_or(record->find("eligible_for_training")))) {
                continue;
            }
            const auto domain = lower_ascii(json::string_or(record->find("domain")));
            if (!domain.starts_with("english") && domain != "cpp23") {
                continue;
            }
            const auto* messages = record->find("messages");
            if (messages == nullptr || messages->as_array() == nullptr) {
                continue;
            }
            std::string prompt;
            std::string answer;
            for (const auto& item : *messages->as_array()) {
                const auto role = lower_ascii(json::string_or(item.find("role")));
                if (role == "user") {
                    prompt = json::string_or(item.find("content"));
                } else if (role == "assistant") {
                    answer = json::string_or(item.find("content"));
                }
            }
            if (!prompt.empty() && !answer.empty()) {
                const auto normalized_prompt = normalize_space(prompt);
                const auto normalized_answer = trim(answer);
                curated_.emplace_back(normalized_prompt, normalized_answer);
                for (const auto& word : ascii_words(normalized_prompt)) {
                    vocabulary_.insert(word);
                }
                for (const auto& word : ascii_words(normalized_answer)) {
                    vocabulary_.insert(word);
                }
            }
        }
    }

    [[nodiscard]] std::optional<std::string> verified_response(
        const std::vector<Message>& messages) const {
        std::string user;
        for (auto iterator = messages.rbegin(); iterator != messages.rend(); ++iterator) {
            if (lower_ascii(iterator->role) == "user") {
                user = trim(iterator->content);
                break;
            }
        }
        const auto normalized = normalize_space(user);
        if (normalized.empty()) {
            return std::nullopt;
        }
        const auto approved = approved_response(normalized);
        if (approved.reviewed) {
            return approved.response;
        }
        static const std::regex greeting(R"(^(hi|hello|hey)[.!?,]*$)", std::regex::icase);
        if (std::regex_match(user, greeting)) {
            return normalized.starts_with("hey") ? "Hey!" : "Hello!";
        }
        if (normalized == "what?" || normalized == "what do you mean?" ||
            normalized == "what do you mean by that?" || normalized == "what does that mean?") {
            return "What part should I explain?";
        }
        if (normalized.find("reversible") != std::string::npos &&
            (normalized.find("define") != std::string::npos ||
             normalized.find("what is") != std::string::npos ||
             normalized.find("mean") != std::string::npos)) {
            return "A reversible change can be undone to restore the previous working state.";
        }
        const auto query_words_vector = ascii_words(normalized);
        std::map<std::string, int> query_words;
        for (const auto& word : query_words_vector) {
            query_words[word] = 1;
        }
        double best_score = 0.0;
        const std::string* best = nullptr;
        for (const auto& [prompt, answer] : curated_) {
            if (prompt == normalized) {
                return answer;
            }
            std::map<std::string, int> prompt_words;
            for (const auto& word : ascii_words(prompt)) {
                prompt_words[word] = 1;
            }
            std::size_t intersection = 0;
            for (const auto& [word, unused] : query_words) {
                static_cast<void>(unused);
                intersection += prompt_words.contains(word) ? 1U : 0U;
            }
            const auto union_size = query_words.size() + prompt_words.size() - intersection;
            const auto score = union_size == 0U ? 0.0 :
                static_cast<double>(intersection) / static_cast<double>(union_size);
            if (score > best_score) {
                best_score = score;
                best = &answer;
            }
        }
        return best != nullptr && best_score >= 0.82 ? std::optional<std::string>(*best) : std::nullopt;
    }

    struct ReviewedResponse final {
        bool reviewed{false};
        std::optional<std::string> response;
    };

    [[nodiscard]] ReviewedResponse approved_response(
        const std::string_view normalized_prompt) const {
        std::ifstream stream(state_root_ / "memory" / "episodes.jsonl", std::ios::binary);
        std::string line;
        ReviewedResponse result;
        while (std::getline(stream, line)) {
            std::string error;
            const auto record = json::try_parse(line, error);
            if (!record.has_value() ||
                normalize_space(json::string_or(record->find("prompt"))) != normalized_prompt) {
                continue;
            }
            const auto rejected = json::bool_or(record->find("human_rejected"));
            const auto trusted = json::bool_or(record->find("human_accepted")) ||
                json::bool_or(record->find("deterministic_verified")) ||
                json::bool_or(record->find("reference_verified"));
            auto answer = rejected
                ? json::string_or(record->find("correction"))
                : json::string_or(record->find("response"));
            answer = trim(std::move(answer));
            if (rejected || trusted) {
                result.reviewed = true;
                result.response = answer.empty()
                    ? std::nullopt
                    : std::optional<std::string>(std::move(answer));
            }
        }
        return result;
    }

    [[nodiscard]] GenerationResult generate(
        const std::vector<Message>& messages,
        const std::string_view lane,
        const std::size_t maximum_new_tokens,
        const float temperature) const {
        auto tokens = prompt_tokens(messages);
        const auto maximum_prompt = std::max<std::size_t>(1U, config_.context_length - maximum_new_tokens);
        if (tokens.size() > maximum_prompt) {
            tokens.erase(tokens.begin(), tokens.end() - static_cast<std::ptrdiff_t>(maximum_prompt));
        }
        std::vector<BlockCache> caches(config_.layers);
        for (auto& cache : caches) {
            cache.keys.resize(config_.context_length * config_.width);
            cache.values.resize(config_.context_length * config_.width);
        }
        std::vector<float> logits(byte_vocab_size);
        std::size_t position = 0;
        const auto skills = lane_skills(lane);
        for (const auto token : tokens) {
            logits = feed(token, position++, skills, caches);
        }
        std::vector<int> generated;
        std::mt19937 random(static_cast<std::uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        for (std::size_t count = 0; count < maximum_new_tokens && position < config_.context_length; ++count) {
            const auto next = sample(logits, generated, temperature, random);
            if (next == eos_id || next == separator_id) {
                break;
            }
            if (next >= byte_offset && next < byte_vocab_size) {
                generated.push_back(next);
            }
            logits = feed(next, position++, skills, caches);
        }
        std::string text;
        text.reserve(generated.size());
        for (const auto token : generated) {
            text.push_back(static_cast<char>(token - byte_offset));
        }
        text = trim(std::move(text));
        const auto quality = assess_quality(text, messages);
        return {
            .text = std::move(text),
            .quality_reason = quality.second,
            .generation = generation_,
            .prompt_tokens = tokens.size(),
            .completion_tokens = generated.size(),
            .accepted = quality.first,
        };
    }

    [[nodiscard]] std::vector<float> feed(
        const int token,
        const std::size_t position,
        const std::vector<std::string>& skills,
        std::vector<BlockCache>& caches) const {
        if (token < 0 || token >= byte_vocab_size) {
            throw std::runtime_error("model token is out of range");
        }
        std::vector<float> value(
            embedding_.data + static_cast<std::size_t>(token) * config_.width,
            embedding_.data + (static_cast<std::size_t>(token) + 1U) * config_.width);
        const auto head_width = config_.width / config_.heads;
        const auto attention_scale = 1.0F / std::sqrt(static_cast<float>(head_width));
        for (std::size_t layer = 0; layer < blocks_.size(); ++layer) {
            const auto& block = blocks_[layer];
            std::vector<float> normalized(config_.width);
            rms_norm(value, block.attention_norm, normalized);
            std::vector<float> qkv(config_.width * 3U);
            matvec(block.qkv, normalized, qkv);
            auto* query = qkv.data();
            auto* key = qkv.data() + config_.width;
            auto* content = qkv.data() + config_.width * 2U;
            for (std::size_t head = 0; head < config_.heads; ++head) {
                const auto base = head * head_width;
                for (std::size_t pair = 0; pair + 1U < head_width; pair += 2U) {
                    const auto angle = static_cast<float>(position) /
                        std::pow(config_.rope_base, static_cast<float>(pair) / static_cast<float>(head_width));
                    const auto cosine = std::cos(angle);
                    const auto sine = std::sin(angle);
                    const auto q0 = query[base + pair];
                    const auto q1 = query[base + pair + 1U];
                    const auto k0 = key[base + pair];
                    const auto k1 = key[base + pair + 1U];
                    query[base + pair] = q0 * cosine - q1 * sine;
                    query[base + pair + 1U] = q1 * cosine + q0 * sine;
                    key[base + pair] = k0 * cosine - k1 * sine;
                    key[base + pair + 1U] = k1 * cosine + k0 * sine;
                }
            }
            auto& cache = caches[layer];
            std::copy_n(key, config_.width, cache.keys.data() + position * config_.width);
            std::copy_n(content, config_.width, cache.values.data() + position * config_.width);
            std::vector<float> attended(config_.width, 0.0F);
            std::vector<float> scores(position + 1U);
            for (std::size_t head = 0; head < config_.heads; ++head) {
                const auto base = head * head_width;
                for (std::size_t prior = 0; prior <= position; ++prior) {
                    const auto* prior_key = cache.keys.data() + prior * config_.width + base;
                    float score = 0.0F;
                    for (std::size_t index = 0; index < head_width; ++index) {
                        score += query[base + index] * prior_key[index];
                    }
                    scores[prior] = score * attention_scale;
                }
                const auto maximum = *std::ranges::max_element(scores);
                float total = 0.0F;
                for (auto& score : scores) {
                    score = std::exp(score - maximum);
                    total += score;
                }
                for (std::size_t prior = 0; prior <= position; ++prior) {
                    const auto probability = scores[prior] / total;
                    const auto* prior_value = cache.values.data() + prior * config_.width + base;
                    for (std::size_t index = 0; index < head_width; ++index) {
                        attended[base + index] += probability * prior_value[index];
                    }
                }
            }
            std::vector<float> attention_output(config_.width);
            matvec(block.attention_output, attended, attention_output);
            for (std::size_t index = 0; index < config_.width; ++index) {
                value[index] += attention_output[index];
            }
            rms_norm(value, block.ffn_norm, normalized);
            const auto ffn = swiglu(block.ffn, normalized);
            for (std::size_t index = 0; index < config_.width; ++index) {
                value[index] += ffn[index];
            }
            std::vector<const SwiWeights*> routed;
            for (const auto& skill : skills) {
                if (const auto found = block.experts.find(skill); found != block.experts.end()) {
                    routed.push_back(&found->second);
                }
            }
            if (!routed.empty()) {
                rms_norm(value, block.expert_norm, normalized);
                std::vector<float> expert_sum(config_.width, 0.0F);
                for (const auto* expert : routed) {
                    const auto output = swiglu(*expert, normalized);
                    for (std::size_t index = 0; index < config_.width; ++index) {
                        expert_sum[index] += output[index];
                    }
                }
                const auto divisor = std::sqrt(static_cast<float>(routed.size()));
                for (std::size_t index = 0; index < config_.width; ++index) {
                    value[index] += expert_sum[index] / divisor;
                }
            }
        }
        std::vector<float> normalized(config_.width);
        rms_norm(value, final_norm_, normalized);
        std::vector<float> logits(byte_vocab_size);
        matvec(embedding_, normalized, logits);
        return logits;
    }

    [[nodiscard]] static std::vector<int> blocked_tokens(const std::vector<int>& generated) {
        constexpr std::size_t ngram = 8U;
        constexpr std::size_t window = 512U;
        std::vector<int> result;
        if (generated.size() < ngram - 1U) {
            return result;
        }
        const auto prefix = generated.end() - static_cast<std::ptrdiff_t>(ngram - 1U);
        const auto first = generated.size() > window ? generated.size() - window : 0U;
        for (std::size_t index = first; index + ngram <= generated.size(); ++index) {
            if (std::equal(prefix, generated.end(), generated.begin() + static_cast<std::ptrdiff_t>(index))) {
                result.push_back(generated[index + ngram - 1U]);
            }
        }
        return result;
    }

    [[nodiscard]] static int sample(
        std::vector<float> logits,
        const std::vector<int>& generated,
        const float temperature,
        std::mt19937& random) {
        for (int token = 0; token < byte_offset; ++token) {
            if (token != eos_id && token != separator_id) {
                logits[static_cast<std::size_t>(token)] = -std::numeric_limits<float>::infinity();
            }
        }
        for (const auto blocked : blocked_tokens(generated)) {
            logits[static_cast<std::size_t>(blocked)] = -std::numeric_limits<float>::infinity();
        }
        if (generated.size() >= 6U && std::ranges::all_of(
                generated.end() - 6, generated.end(),
                [&](const int token) { return token == generated.back(); })) {
            logits[static_cast<std::size_t>(generated.back())] = -std::numeric_limits<float>::infinity();
        }
        if (temperature <= 0.0F) {
            return static_cast<int>(std::ranges::max_element(logits) - logits.begin());
        }
        std::vector<int> indices(logits.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::ranges::sort(indices, [&](const int left, const int right) {
            return logits[static_cast<std::size_t>(left)] > logits[static_cast<std::size_t>(right)];
        });
        indices.resize(std::min<std::size_t>(64U, indices.size()));
        const auto maximum = logits[static_cast<std::size_t>(indices.front())] / temperature;
        std::vector<double> probabilities;
        probabilities.reserve(indices.size());
        double total = 0.0;
        for (const auto index : indices) {
            const auto probability = std::exp(
                static_cast<double>(logits[static_cast<std::size_t>(index)] / temperature - maximum));
            probabilities.push_back(probability);
            total += probability;
        }
        double cumulative = 0.0;
        std::size_t keep = probabilities.size();
        for (std::size_t index = 0; index < probabilities.size(); ++index) {
            const auto probability = probabilities[index] / total;
            if (cumulative > 0.92) {
                keep = index;
                break;
            }
            cumulative += probability;
        }
        probabilities.resize(std::max<std::size_t>(1U, keep));
        indices.resize(probabilities.size());
        std::discrete_distribution<std::size_t> distribution(probabilities.begin(), probabilities.end());
        return indices[distribution(random)];
    }

    [[nodiscard]] std::pair<bool, std::string> assess_quality(
        const std::string& text,
        const std::vector<Message>& messages) const {
        if (text.empty()) {
            return {false, "empty internal-model draft"};
        }
        if (!valid_utf8(text)) {
            return {false, "generated text is not valid UTF-8"};
        }
        for (const unsigned char character : text) {
            if (character < 32U && character != '\r' && character != '\n' && character != '\t') {
                return {false, "control characters in generated text"};
            }
        }
        static const std::regex repeated_alphanumeric(R"(([A-Za-z0-9])\1{7,})");
        static const std::regex repeated_fragment(R"(([A-Za-z]{2,5})(?:\1){2,})", std::regex::icase);
        if (std::regex_search(text, repeated_alphanumeric) || std::regex_search(text, repeated_fragment)) {
            return {false, "repetitive internal-model draft"};
        }
        const auto words = ascii_words(text);
        std::string latest_user;
        for (auto iterator = messages.rbegin(); iterator != messages.rend(); ++iterator) {
            if (lower_ascii(iterator->role) == "user") {
                latest_user = normalize_space(iterator->content);
                break;
            }
        }
        const auto terse = latest_user.find("exactly") != std::string::npos ||
            latest_user.find("only") != std::string::npos ||
            latest_user.find("calculate") != std::string::npos ||
            latest_user.find("compute") != std::string::npos;
        std::unordered_set<std::string> context_vocabulary;
        for (const auto& message : messages) {
            for (const auto& word : ascii_words(message.content)) {
                context_vocabulary.insert(word);
            }
        }
        const auto recognized = [&](const std::string& word) {
            if (vocabulary_.contains(word) || context_vocabulary.contains(word)) {
                return true;
            }
            static constexpr std::array suffixes{"ing", "ed", "es", "s", "ly", "er"};
            for (const auto suffix : suffixes) {
                if (word.size() <= std::string_view(suffix).size() + 2U ||
                    !word.ends_with(suffix)) {
                    continue;
                }
                const auto stem = word.substr(0U, word.size() - std::string_view(suffix).size());
                if (vocabulary_.contains(stem) || context_vocabulary.contains(stem) ||
                    (std::string_view(suffix) == "ing" &&
                     (vocabulary_.contains(stem + "e") || context_vocabulary.contains(stem + "e")))) {
                    return true;
                }
            }
            return false;
        };
        const auto vocabulary_ratio = words.empty() ? 1.0 :
            static_cast<double>(std::ranges::count_if(words, recognized)) /
            static_cast<double>(words.size());
        const auto code = text.find("#include") != std::string::npos ||
            text.find("std::") != std::string::npos || text.find("```") != std::string::npos;
        if (!code && !terse && words.size() < 4U) {
            return {false, "answer is too short for an open conversational request"};
        }
        if (!code && !terse && words.size() <= 10U && vocabulary_ratio < 0.85) {
            return {false, "short answer contains untrusted or malformed English"};
        }
        if (!code && words.size() >= 6U && vocabulary_ratio < 0.80) {
            return {false, "most generated words are not trusted English vocabulary"};
        }
        if (words.size() >= 8U) {
            std::size_t longest_run = 1U;
            std::size_t run = 1U;
            for (std::size_t index = 1; index < words.size(); ++index) {
                run = words[index] == words[index - 1U] ? run + 1U : 1U;
                longest_run = std::max(longest_run, run);
            }
            if (longest_run >= (code ? 8U : 4U)) {
                return {false, "consecutive repeated-word loop"};
            }
        }
        return {true, "native structural quality gate passed"};
    }

    std::filesystem::path state_root_;
    std::string generation_;
    std::filesystem::path generation_path_;
    Value manifest_;
    ModelConfig config_;
    std::filesystem::path weights_;
    std::unique_ptr<SafeTensorFile> safetensors_;
    TensorView embedding_;
    TensorView final_norm_;
    std::vector<BlockWeights> blocks_;
    std::vector<std::pair<std::string, std::string>> curated_;
    std::unordered_set<std::string> vocabulary_;
    bool neural_ready_{false};
};

[[nodiscard]] std::string lane_from_model(const std::string_view model) {
    if (model.find("coder") != std::string_view::npos) {
        return "coder";
    }
    if (model.find("thinker") != std::string_view::npos) {
        return "thinker";
    }
    if (model.find("reviewer") != std::string_view::npos) {
        return "reviewer";
    }
    return "fast";
}

[[nodiscard]] std::vector<Message> parse_messages(const Value& payload) {
    std::vector<Message> result;
    const auto* messages = payload.find("messages");
    if (messages == nullptr || messages->as_array() == nullptr) {
        return result;
    }
    for (const auto& item : *messages->as_array()) {
        if (item.as_object() == nullptr) {
            continue;
        }
        result.push_back({
            .role = json::string_or(item.find("role"), "user"),
            .content = json::string_or(item.find("content")),
        });
    }
    return result;
}

[[nodiscard]] std::string completion_json(
    const GenerationResult& result,
    const std::string& model) {
    const auto finish = result.accepted ? "stop" : "low_confidence";
    const auto content = result.accepted ? result.text : std::string{};
    const auto origin = result.generation == "foundation"
        ? "verified-native-foundation"
        : "random-initialized-local-model";
    const auto tokens_per_second = result.elapsed_ms <= 0.0 ? 0.0 :
        static_cast<double>(result.completion_tokens) * 1000.0 / result.elapsed_ms;
    std::ostringstream output;
    output << "{\"id\":\"engcoder-native-" << GetCurrentProcessId()
           << "\",\"object\":\"chat.completion\",\"model\":\"" << json::escape(model)
           << "\",\"source\":\"engcoder-native\",\"generation\":\""
           << json::escape(result.generation)
           << "\",\"origin\":\"" << origin << "\",\"external_base_weights\":false,"
           << "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\""
           << json::escape(content) << "\"},\"finish_reason\":\"" << finish << "\"}],"
           << "\"usage\":{\"prompt_tokens\":" << result.prompt_tokens
           << ",\"completion_tokens\":" << result.completion_tokens
           << ",\"total_tokens\":" << result.prompt_tokens + result.completion_tokens << "},"
           << "\"quality\":{\"accepted\":" << (result.accepted ? "true" : "false")
           << ",\"reason\":\"" << json::escape(result.quality_reason)
           << "\",\"attempts\":" << result.attempts << "},"
           << "\"content\":\"" << json::escape(content) << "\",\"prompt_tokens\":"
           << result.prompt_tokens << ",\"completion_tokens\":" << result.completion_tokens
           << ",\"tokens_per_second\":" << tokens_per_second
           << ",\"elapsed_ms\":" << result.elapsed_ms << '}';
    return output.str();
}

[[nodiscard]] std::string streaming_json(
    const GenerationResult& result,
    const std::string& model) {
    std::string body;
    if (result.accepted) {
        for (std::size_t offset = 0; offset < result.text.size(); offset += 64U) {
            const auto piece = result.text.substr(offset, 64U);
            body += "data: {\"object\":\"chat.completion.chunk\",\"model\":\"";
            body += json::escape(model);
            body += "\",\"source\":\"engcoder-native\",\"generation\":\"";
            body += json::escape(result.generation);
            body += "\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"";
            body += json::escape(piece);
            body += "\"},\"finish_reason\":null}]}\n\n";
        }
    }
    body += "data: {\"object\":\"chat.completion.chunk\",\"model\":\"";
    body += json::escape(model);
    body += "\",\"source\":\"engcoder-native\",\"generation\":\"";
    body += json::escape(result.generation);
    body += "\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"";
    body += result.accepted ? "stop" : "low_confidence";
    body += "\"}],\"usage\":{\"completion_tokens\":" + std::to_string(result.completion_tokens) +
        "},\"quality\":{\"accepted\":" + std::string(result.accepted ? "true" : "false") +
        ",\"reason\":\"" + json::escape(result.quality_reason) + "\",\"attempts\":" +
        std::to_string(result.attempts) + "}}\n\ndata: [DONE]\n\n";
    return body;
}

[[nodiscard]] bool send_all(const SOCKET socket, const std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto count = ::send(socket, data.data() + sent,
            static_cast<int>(std::min<std::size_t>(data.size() - sent, INT_MAX)), 0);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

void respond(
    const SOCKET socket,
    const int status,
    const std::string_view type,
    const std::string& body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Error") << "\r\n"
           << "Content-Type: " << type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n";
    static_cast<void>(send_all(socket, header.str()));
    static_cast<void>(send_all(socket, body));
}

[[nodiscard]] std::optional<std::string> receive_request(const SOCKET socket) {
    std::string request;
    std::array<char, 8192> buffer{};
    std::size_t required = std::string::npos;
    while (request.size() < 4U * 1024U * 1024U) {
        const auto count = ::recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count <= 0) {
            return std::nullopt;
        }
        request.append(buffer.data(), static_cast<std::size_t>(count));
        const auto boundary = request.find("\r\n\r\n");
        if (boundary != std::string::npos && required == std::string::npos) {
            required = boundary + 4U;
            std::istringstream headers(request.substr(0U, boundary));
            std::string line;
            while (std::getline(headers, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                const auto lowered = lower_ascii(line);
                if (lowered.starts_with("content-length:")) {
                    required += static_cast<std::size_t>(std::stoull(trim(line.substr(15U))));
                }
            }
        }
        if (required != std::string::npos && request.size() >= required) {
            request.resize(required);
            return request;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::wstring argument_value(
    const int count,
    wchar_t** values,
    const std::wstring_view name) {
    for (int index = 1; index + 1 < count; ++index) {
        if (values[index] == name) {
            return values[index + 1];
        }
    }
    return {};
}

[[nodiscard]] bool has_argument(const int count, wchar_t** values, const std::wstring_view name) {
    return std::any_of(values + 1, values + count, [&](const wchar_t* value) {
        return value == name;
    });
}

void write_ready_file(const std::filesystem::path& path, const unsigned short port) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << "port=" << port << "\nengine=engcoder-native-v1\n";
        if (!stream) {
            throw std::runtime_error("unable to publish native host ready file");
        }
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        throw std::runtime_error("unable to publish native host ready file: " + error.message());
    }
}

[[nodiscard]] int engine_contract_self_test() {
    const std::array matrix_values{1.0F, 2.0F, 3.0F, 4.0F};
    const TensorView matrix{matrix_values.data(), {2U, 2U}};
    const std::array input{2.0F, 3.0F};
    std::array<float, 2U> output{};
    matvec(matrix, input, output);
    if (std::abs(output[0] - 8.0F) > 1.0e-5F ||
        std::abs(output[1] - 18.0F) > 1.0e-5F) {
        throw std::runtime_error("native matvec contract failed");
    }

    const std::array norm_weights{1.0F, 1.0F};
    const TensorView norm_weight{norm_weights.data(), {2U}};
    const std::array norm_input{3.0F, 4.0F};
    std::array<float, 2U> normalized{};
    rms_norm(norm_input, norm_weight, normalized);
    const auto expected_scale = static_cast<float>(1.0 / std::sqrt(12.5 + 1.0e-6));
    if (std::abs(normalized[0] - 3.0F * expected_scale) > 1.0e-5F ||
        std::abs(normalized[1] - 4.0F * expected_scale) > 1.0e-5F) {
        throw std::runtime_error("native RMS-normalization contract failed");
    }

    const std::string valid = "EngCoder \xF0\x9F\xA6\x85";
    const std::string overlong{"\xC0\xAF", 2U};
    const std::string surrogate{"\xED\xA0\x80", 3U};
    if (!valid_utf8(valid) || valid_utf8(overlong) || valid_utf8(surrogate)) {
        throw std::runtime_error("native UTF-8 validation contract failed");
    }
    const auto foundation_root = std::filesystem::temp_directory_path() /
        ("engcoder-native-foundation-contract-" + std::to_string(GetCurrentProcessId()));
    std::error_code cleanup_error;
    std::filesystem::remove_all(foundation_root, cleanup_error);
    std::filesystem::create_directories(foundation_root / "data");
    std::filesystem::create_directories(foundation_root / "memory");
    {
        std::ofstream curriculum(foundation_root / "data" / "bootstrap.jsonl", std::ios::binary);
        curriculum
            << R"({"domain":"english-core","messages":[{"role":"user","content":"Define reliable."},{"role":"assistant","content":"Reliable means consistently dependable or trustworthy."}]})"
            << '\n'
            << R"({"domain":"cpp23","messages":[{"role":"user","content":"What does import do in C++23?"},{"role":"assistant","content":"An import declaration makes a named module's exported declarations available to the translation unit."}]})"
            << '\n';
    }
    {
        std::ofstream episodes(foundation_root / "memory" / "episodes.jsonl", std::ios::binary);
        episodes
            << R"({"prompt":"hello there","response":"Hello there! What would you like to work on?","human_accepted":true,"human_rejected":false,"deterministic_verified":false,"reference_verified":false})"
            << '\n'
            << R"({"prompt":"obsolete answer","response":"Old reply","human_accepted":true,"human_rejected":false,"deterministic_verified":false,"reference_verified":false})"
            << '\n'
            << R"({"prompt":"obsolete answer","response":"Old reply","correction":"","human_accepted":false,"human_rejected":true,"deterministic_verified":false,"reference_verified":false})"
            << '\n';
    }
    NativeModel foundation(foundation_root);
    const auto curated = foundation.complete({{"user", "Define reliable."}}, "fast", 96U, 0.0F);
    const auto approved = foundation.complete({{"user", "hello there"}}, "fast", 96U, 0.0F);
    const auto unknown = foundation.complete({{"user", "zzzxxyy"}}, "fast", 96U, 0.0F);
    const auto suppressed = foundation.complete({{"user", "obsolete answer"}}, "fast", 96U, 0.0F);
    std::filesystem::remove_all(foundation_root, cleanup_error);
    if (foundation.generation() != "foundation" || foundation.weights_bytes() != 0U ||
        !curated.accepted || curated.text != "Reliable means consistently dependable or trustworthy." ||
        !approved.accepted || approved.text != "Hello there! What would you like to work on?" ||
        unknown.accepted || suppressed.accepted ||
        unknown.quality_reason != "the native foundation has not learned this request yet") {
        throw std::runtime_error("clean-install native foundation contract failed");
    }
    std::cout << "EngCoder native inference contract passed\n";
    return 0;
}

} // namespace

int wmain(const int count, wchar_t** values) {
    try {
        if (has_argument(count, values, L"--engine-contract-self-test")) {
            return engine_contract_self_test();
        }
        const auto state_root = std::filesystem::path(argument_value(count, values, L"--state-root"));
        if (state_root.empty()) {
            throw std::runtime_error("--state-root is required");
        }
        NativeModel model(state_root);
        if (has_argument(count, values, L"--self-test")) {
            auto prompt_wide = argument_value(count, values, L"--prompt");
            if (prompt_wide.empty()) {
                prompt_wide = L"hello";
            }
            const auto utf8_bytes = WideCharToMultiByte(
                CP_UTF8, 0, prompt_wide.data(), static_cast<int>(prompt_wide.size()),
                nullptr, 0, nullptr, nullptr);
            std::string prompt(static_cast<std::size_t>(utf8_bytes), '\0');
            static_cast<void>(WideCharToMultiByte(
                CP_UTF8, 0, prompt_wide.data(), static_cast<int>(prompt_wide.size()),
                prompt.data(), utf8_bytes, nullptr, nullptr));
            const auto result = model.complete({{"user", prompt}}, "fast", 96U, 0.0F);
            std::cout << completion_json(result, "threadspire://fast") << '\n';
            return result.accepted ? 0 : 2;
        }

        WSADATA winsock{};
        if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
            throw std::runtime_error("WinSock initialization failed");
        }
        const auto server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == INVALID_SOCKET) {
            WSACleanup();
            throw std::runtime_error("unable to create native host socket");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const auto requested_port = argument_value(count, values, L"--port");
        address.sin_port = htons(requested_port.empty() ? 0U :
            static_cast<unsigned short>(std::stoul(requested_port)));
        if (::bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            ::listen(server, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(server);
            WSACleanup();
            throw std::runtime_error("unable to bind native host to loopback");
        }
        int address_size = sizeof(address);
        if (getsockname(server, reinterpret_cast<sockaddr*>(&address), &address_size) == SOCKET_ERROR) {
            closesocket(server);
            WSACleanup();
            throw std::runtime_error("unable to determine native host port");
        }
        const auto ready_file = std::filesystem::path(argument_value(count, values, L"--ready-file"));
        write_ready_file(ready_file, ntohs(address.sin_port));
        std::cout << "EngCoder native inference ready: generation=" << model.generation()
                  << " weights=" << model.weights_bytes() << " bytes\n" << std::flush;

        bool stopping = false;
        while (!stopping) {
            const auto client = ::accept(server, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                continue;
            }
            try {
                const auto request = receive_request(client);
                if (!request.has_value()) {
                    respond(client, 400, "application/json", "{\"error\":\"invalid request\"}");
                    closesocket(client);
                    continue;
                }
                const auto first_end = request->find("\r\n");
                const auto first = request->substr(0U, first_end);
                const auto first_space = first.find(' ');
                const auto second_space = first.find(' ', first_space + 1U);
                const auto method = first.substr(0U, first_space);
                const auto path = first.substr(first_space + 1U, second_space - first_space - 1U);
                const auto body_offset = request->find("\r\n\r\n") + 4U;
                if (method == "GET" && path == "/health") {
                    respond(client, 200, "application/json",
                        "{\"status\":\"ok\",\"engine\":\"engcoder-native-v1\",\"generation\":\"" +
                        json::escape(model.generation()) + "\"}");
                } else if (method == "POST" && path == "/shutdown") {
                    respond(client, 200, "application/json", "{\"status\":\"stopping\"}");
                    stopping = true;
                } else if (method == "POST" && path == "/v1/chat/completions") {
                    const auto payload = json::parse(request->substr(body_offset));
                    const auto messages = parse_messages(payload);
                    if (messages.empty()) {
                        throw std::runtime_error("messages must be a non-empty array");
                    }
                    const auto model_name = json::string_or(payload.find("model"), "threadspire://fast");
                    const auto maximum = static_cast<std::size_t>(std::clamp(
                        json::number_or(payload.find("max_tokens"), 256.0), 1.0, 2048.0));
                    const auto temperature = static_cast<float>(std::clamp(
                        json::number_or(payload.find("temperature"), 0.2), 0.0, 2.0));
                    const auto result = model.complete(
                        messages, lane_from_model(model_name), maximum, temperature);
                    if (json::bool_or(payload.find("stream"))) {
                        respond(client, 200, "text/event-stream; charset=utf-8",
                            streaming_json(result, model_name));
                    } else {
                        respond(client, 200, "application/json; charset=utf-8",
                            completion_json(result, model_name));
                    }
                } else {
                    respond(client, 404, "application/json", "{\"error\":\"not found\"}");
                }
            } catch (const std::exception& exception) {
                respond(client, 500, "application/json",
                    "{\"error\":{\"message\":\"" + json::escape(exception.what()) + "\"}}");
            }
            ::shutdown(client, SD_BOTH);
            closesocket(client);
        }
        closesocket(server);
        WSACleanup();
        std::error_code error;
        std::filesystem::remove(ready_file, error);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "EngCoder native host failed: " << exception.what() << '\n';
        return 1;
    }
}
