module;
#include "module_prelude.hpp"

module threadspire.scheduler;

namespace threadspire::ai::runtime {
namespace {

constexpr std::uintmax_t mib = 1024ULL * 1024ULL;
constexpr std::uintmax_t gib = 1024ULL * mib;

[[nodiscard]] bool safe_pack_id(const std::string_view id) noexcept {
    return !id.empty() && id != "." && id != ".." &&
        std::ranges::all_of(id, [](const unsigned char character) {
            return std::isalnum(character) != 0 || character == '-' || character == '_';
        });
}

[[nodiscard]] std::uintmax_t tree_size(const std::filesystem::path& root) noexcept {
    std::error_code error;
    std::uintmax_t total = 0U;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const auto status = iterator->symlink_status(error);
        if (!error && std::filesystem::is_symlink(status)) {
            iterator.disable_recursion_pending();
        } else if (!error && std::filesystem::is_regular_file(status)) {
            total += iterator->file_size(error);
        }
        error.clear();
        iterator.increment(error);
        error.clear();
    }
    return total;
}


enum class AuditBucket {
    model_weight,
    runtime,
    program,
    metadata,
    other,
};

struct FileClassification final {
    AuditBucket bucket{AuditBucket::other};
    std::string purpose;
};

struct PackageIndex final {
    CapabilityPackDependencyAudit audit;
    std::filesystem::path site_root;
    std::filesystem::path metadata_root;
};

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::string trim_ascii(std::string value) {
    const auto whitespace = [](const unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto first = std::ranges::find_if_not(value, whitespace);
    const auto last = std::ranges::find_if_not(value | std::views::reverse, whitespace).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

[[nodiscard]] std::string path_key(const std::filesystem::path& path) {
    return lower_ascii(path.lexically_normal().generic_string());
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to read " + path.string());
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string metadata_field(
    const std::string_view text,
    const std::string_view requested) {
    std::istringstream stream{std::string(text)};
    std::string line;
    const auto requested_lower = lower_ascii(std::string(requested));
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos ||
            lower_ascii(line.substr(0, separator)) != requested_lower) {
            continue;
        }
        return trim_ascii(line.substr(separator + 1U));
    }
    return {};
}

[[nodiscard]] std::vector<std::string> parse_csv_row(const std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (character == '"') {
            if (quoted && index + 1U < line.size() && line[index + 1U] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field.push_back(character);
        }
    }
    fields.push_back(std::move(field));
    return fields;
}

[[nodiscard]] FileClassification classify_file(const std::filesystem::path& relative) {
    const auto key = path_key(relative);
    const auto filename = lower_ascii(relative.filename().string());
    const auto extension = lower_ascii(relative.extension().string());
    const auto named_weight_binary = extension == ".bin" &&
        (filename.find("model") != std::string::npos ||
         filename.find("weight") != std::string::npos ||
         filename.find("adapter") != std::string::npos ||
         filename.find("pytorch") != std::string::npos);
    const auto named_flax_weights = extension == ".msgpack" &&
        (filename.find("model") != std::string::npos ||
         filename.find("weight") != std::string::npos);
    if (extension == ".safetensors" || extension == ".gguf" || extension == ".ckpt" ||
        extension == ".pt" || extension == ".pth" || extension == ".onnx" ||
        extension == ".tflite" || extension == ".h5" || extension == ".hdf5" ||
        named_weight_binary || named_flax_weights) {
        return {AuditBucket::model_weight, "model weights"};
    }
    if (key.find(".dist-info/") != std::string::npos ||
        key.find(".egg-info/") != std::string::npos || filename == "pack.json" ||
        filename == "metadata" || filename == "record" || filename == "wheel" ||
        filename == "installer" || filename.starts_with("license") ||
        filename.starts_with("copying") || filename.starts_with("notice")) {
        return {AuditBucket::metadata, "package metadata or license evidence"};
    }
    if (extension == ".py") {
        return {AuditBucket::program, "Python program code"};
    }
    if (extension == ".pyc" || extension == ".pyo") {
        return {AuditBucket::program, "cached Python bytecode"};
    }
    if (extension == ".exe" || extension == ".dll" || extension == ".pyd" ||
        extension == ".lib" || extension == ".so" || extension == ".dylib") {
        return {AuditBucket::runtime, "native executable or runtime library"};
    }
    if (key.find("runtime/") != std::string::npos) {
        return {AuditBucket::runtime, "language runtime or runtime support data"};
    }
    if (extension == ".json" || extension == ".toml" || extension == ".ini" ||
        extension == ".cfg" || extension == ".txt" || extension == ".yaml" ||
        extension == ".yml" || filename == "pack.ready") {
        return {AuditBucket::other, "configuration or package data"};
    }
    return {AuditBucket::other, "package data"};
}

[[nodiscard]] bool files_identical(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    const std::stop_token stop_token) {
    std::error_code left_error;
    std::error_code right_error;
    const auto left_size = std::filesystem::file_size(left, left_error);
    const auto right_size = std::filesystem::file_size(right, right_error);
    if (left_error || right_error || left_size != right_size) {
        return false;
    }
    std::ifstream left_stream(left, std::ios::binary);
    std::ifstream right_stream(right, std::ios::binary);
    if (!left_stream || !right_stream) {
        return false;
    }
    std::vector<char> left_buffer(1024U * 1024U);
    std::vector<char> right_buffer(1024U * 1024U);
    while (left_stream && right_stream) {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("capability-pack audit cancelled");
        }
        left_stream.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        right_stream.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        const auto left_count = left_stream.gcount();
        const auto right_count = right_stream.gcount();
        if (left_count != right_count ||
            !std::equal(left_buffer.begin(), left_buffer.begin() + left_count, right_buffer.begin())) {
            return false;
        }
    }
    return left_stream.gcount() == right_stream.gcount();
}

[[nodiscard]] std::optional<CapabilityPackDescriptor> catalog_descriptor(
    const std::string_view id) {
    auto catalog = CapabilityPackManager::catalog();
    const auto found = std::ranges::find(catalog, id, &CapabilityPackDescriptor::id);
    if (found == catalog.end()) {
        return std::nullopt;
    }
    return *found;
}

[[nodiscard]] std::string manifest_string(
    const core::json::Value& manifest,
    const std::string_view key,
    const std::string_view fallback = {}) {
    return core::json::string_or(manifest.find(key), fallback);
}
} // namespace

CapabilityPackManager::CapabilityPackManager(std::filesystem::path packs_root)
    : packs_root_(std::move(packs_root)) {
}

std::vector<CapabilityPackDescriptor> CapabilityPackManager::catalog() {
    return {
        {
            .id = "compact-native-inference",
            .name = "Compact Native Inference",
            .version = "2",
            .kind = CapabilityPackKind::compact_native_inference,
            .projected_download_bytes = 8U * mib,
            .projected_install_bytes = 16U * mib,
            .required_files = {"bin/engcoder-native-host.exe"},
            .purpose = "Serves built-in text and code answers without Python or PyTorch.",
            .origin = "Verified EngCoder release payload.",
            .license = "MIT for EngCoder; bundled Microsoft runtime files keep their redistributable terms.",
            .loading_condition = "Loaded only when built-in EngCoder is selected or automatic routing chooses it.",
            .safe_removal_rule = "Remove only as one complete pack while its answer host is stopped; the verified release can restore it automatically.",
        },
        {
            .id = "internal-pytorch-training",
            .name = "Internal PyTorch Training",
            .version = "1",
            .kind = CapabilityPackKind::internal_pytorch_training,
            .projected_download_bytes = 2700U * mib,
            .projected_install_bytes = 4300U * mib,
            .required_files = {"runtime/Scripts/python.exe", "backend/model_worker.py", "backend/internal_model.py", "backend/curriculum_guard.py"},
            .purpose = "Trains and evaluates separate shadow candidates for EngCoder internal weights.",
            .origin = "Prepared local package; installed Python and PyTorch origins are read from their package metadata.",
            .license = "Mixed dependencies; exact installed licenses are reported from package metadata.",
            .loading_condition = "Loaded only for a bounded internal weight-training or evaluation job, never for normal native answers.",
            .safe_removal_rule = "Remove only as one complete pack while no internal training/evaluation job runs and no installed add-on requires it.",
        },
        {
            .id = "transformers-peft-finetuning",
            .name = "Advanced Fine-tuning (Transformers/PEFT)",
            .version = "1",
            .kind = CapabilityPackKind::transformers_peft_finetuning,
            .projected_download_bytes = 900U * mib,
            .projected_install_bytes = 1500U * mib,
            .required_files = {"pack.ready"},
            .dependencies = {"internal-pytorch-training"},
            .purpose = "Fine-tunes an explicitly selected compatible external base with Transformers and PEFT methods such as LoRA.",
            .origin = "Prepared local package; installed Transformers/PEFT origins are read from package metadata.",
            .license = "Mixed dependencies; exact installed licenses are reported from package metadata.",
            .loading_condition = "Loaded only for an explicitly requested external-base fine-tuning job; never for built-in answers or automatic memory.",
            .safe_removal_rule = "Remove only as one complete pack while no external fine-tuning job runs; trained model artifacts are separate.",
        },
        {
            .id = "image-engine",
            .name = "Image Generation",
            .version = "1",
            .kind = CapabilityPackKind::image_engine,
            .projected_download_bytes = 3500U * mib,
            .projected_install_bytes = 7U * gib,
            .required_files = {"pack.ready"},
            .purpose = "Runs an independently versioned specialist that creates or edits images.",
            .origin = "Prepared local image-engine package; component origins are read from installed metadata.",
            .license = "Engine-specific; exact installed licenses must be present in package metadata.",
            .loading_condition = "Loaded only for an image request routed to this specialist.",
            .safe_removal_rule = "Remove only as one complete pack while no image task is active; generated outputs are separate.",
        },
        {
            .id = "audio-engine",
            .name = "Audio Generation",
            .version = "1",
            .kind = CapabilityPackKind::audio_engine,
            .projected_download_bytes = 1200U * mib,
            .projected_install_bytes = 2500U * mib,
            .required_files = {"pack.ready"},
            .purpose = "Runs an independently versioned specialist that creates speech, music, or other audio.",
            .origin = "Prepared local audio-engine package; component origins are read from installed metadata.",
            .license = "Engine-specific; exact installed licenses must be present in package metadata.",
            .loading_condition = "Loaded only for an audio or music request routed to this specialist.",
            .safe_removal_rule = "Remove only as one complete pack while no audio task is active; generated outputs are separate.",
        },
    };
}

std::filesystem::path CapabilityPackManager::pack_path(const std::string_view id) const {
    if (!safe_pack_id(id)) {
        throw std::invalid_argument("invalid capability-pack id");
    }
    return packs_root_ / std::string(id);
}

bool CapabilityPackManager::installed(const std::string_view id) const noexcept {
    try {
        std::error_code error;
        return std::filesystem::is_regular_file(pack_path(id) / "pack.json", error) && !error;
    } catch (...) {
        return false;
    }
}

std::vector<CapabilityPackRecord> CapabilityPackManager::inspect() const {
    auto descriptors = catalog();
    std::vector<CapabilityPackRecord> records;
    records.reserve(descriptors.size());
    for (auto& descriptor : descriptors) {
        const auto path = pack_path(descriptor.id);
        const auto is_installed = installed(descriptor.id);
        records.push_back({
            .descriptor = std::move(descriptor),
            .state = is_installed ? CapabilityPackState::installed : CapabilityPackState::available,
            .install_path = path,
            .installed_bytes = is_installed ? tree_size(path) : 0U,
        });
    }
    return records;
}

CapabilityPackAudit CapabilityPackManager::audit(
    const std::string_view id,
    const std::stop_token stop_token) const {
    CapabilityPackAudit result;
    result.descriptor = catalog_descriptor(id).value_or(CapabilityPackDescriptor{
        .id = std::string(id),
        .name = std::string(id),
        .version = "unknown",
    });
    try {
        result.install_path = pack_path(id);
        const auto manifest_path = result.install_path / "pack.json";
        std::error_code error;
        const auto root_status = std::filesystem::symlink_status(result.install_path, error);
        if (error || !std::filesystem::exists(root_status)) {
            result.state = CapabilityPackState::available;
            result.error = "not installed; no installed files exist to audit";
            return result;
        }
        if (std::filesystem::is_symlink(root_status) || !std::filesystem::is_directory(root_status)) {
            result.state = CapabilityPackState::failed;
            result.error = "installed pack root is not a real directory";
            return result;
        }
        const auto manifest_text = read_text_file(manifest_path);
        std::string parse_error;
        const auto manifest = core::json::try_parse(manifest_text, parse_error);
        if (!manifest.has_value() || !manifest->is_object() ||
            manifest_string(*manifest, "id") != id) {
            result.state = CapabilityPackState::failed;
            result.error = parse_error.empty()
                ? "installed pack manifest is missing or identifies another pack"
                : "installed pack manifest is invalid: " + parse_error;
        } else {
            result.state = CapabilityPackState::installed;
            result.descriptor.name = manifest_string(*manifest, "name", result.descriptor.name);
            result.descriptor.version = manifest_string(*manifest, "version", result.descriptor.version);
            result.descriptor.purpose = manifest_string(
                *manifest, "purpose", result.descriptor.purpose);
            result.descriptor.origin = manifest_string(
                *manifest, "origin", result.descriptor.origin);
            result.descriptor.license = manifest_string(
                *manifest, "license", result.descriptor.license);
            result.descriptor.loading_condition = manifest_string(
                *manifest, "loading_condition", result.descriptor.loading_condition);
            result.descriptor.safe_removal_rule = manifest_string(
                *manifest, "safe_removal_rule", result.descriptor.safe_removal_rule);
            if (const auto* required = manifest->find("required_files"); required != nullptr) {
                if (const auto* values = required->as_array(); values != nullptr) {
                    result.descriptor.required_files.clear();
                    for (const auto& value : *values) {
                        const auto file = std::string(value.as_string());
                        if (!file.empty()) {
                            result.descriptor.required_files.push_back(file);
                        }
                    }
                }
            }
            if (const auto* dependencies = manifest->find("dependencies"); dependencies != nullptr) {
                if (const auto* values = dependencies->as_array(); values != nullptr) {
                    result.descriptor.dependencies.clear();
                    for (const auto& value : *values) {
                        const auto dependency = std::string(value.as_string());
                        if (safe_pack_id(dependency)) {
                            result.descriptor.dependencies.push_back(dependency);
                        }
                    }
                }
            }
        }

        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> installed_files;
        std::filesystem::recursive_directory_iterator iterator(
            result.install_path,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;
        while (iterator != end) {
            if (stop_token.stop_requested()) {
                throw std::runtime_error("capability-pack audit cancelled");
            }
            const auto status = iterator->symlink_status(error);
            if (error) {
                throw std::runtime_error("unable to inspect installed pack: " + error.message());
            }
            if (std::filesystem::is_symlink(status)) {
                iterator.disable_recursion_pending();
                result.state = CapabilityPackState::failed;
                if (!result.error.empty()) {
                    result.error += "; ";
                }
                result.error += "symbolic link excluded from audit: " +
                    iterator->path().lexically_relative(result.install_path).generic_string();
            } else if (std::filesystem::is_directory(status)) {
                ++result.directory_count;
            } else if (std::filesystem::is_regular_file(status)) {
                installed_files.emplace_back(
                    iterator->path().lexically_relative(result.install_path),
                    iterator->path());
            }
            iterator.increment(error);
            if (error) {
                throw std::runtime_error("unable to enumerate installed pack: " + error.message());
            }
        }
        std::ranges::sort(installed_files, {}, [](const auto& item) {
            return path_key(item.first);
        });

        std::vector<PackageIndex> package_indexes;
        std::unordered_map<std::string, std::size_t> owner_by_file;
        for (const auto& [relative, absolute] : installed_files) {
            if (lower_ascii(relative.filename().string()) != "metadata" ||
                !lower_ascii(relative.parent_path().filename().string()).ends_with(".dist-info")) {
                continue;
            }
            PackageIndex package;
            package.site_root = absolute.parent_path().parent_path();
            package.metadata_root = absolute.parent_path();
            try {
                const auto metadata = read_text_file(absolute);
                package.audit.name = metadata_field(metadata, "Name");
                package.audit.version = metadata_field(metadata, "Version");
                package.audit.license = metadata_field(metadata, "License-Expression");
                if (package.audit.license.empty()) {
                    package.audit.license = metadata_field(metadata, "License");
                }
                package.audit.origin = metadata_field(metadata, "Home-page");
                if (package.audit.origin.empty()) {
                    package.audit.origin = metadata_field(metadata, "Project-URL");
                }
            } catch (...) {
            }
            if (package.audit.name.empty()) {
                auto fallback = relative.parent_path().filename().string();
                fallback.resize(fallback.size() - std::string(".dist-info").size());
                package.audit.name = std::move(fallback);
            }
            if (package.audit.version.empty()) {
                package.audit.version = "not declared";
            }
            if (package.audit.license.empty() || lower_ascii(package.audit.license) == "unknown") {
                package.audit.license = "not declared in installed package metadata";
            }
            if (package.audit.origin.empty() || lower_ascii(package.audit.origin) == "unknown") {
                package.audit.origin = "not declared in installed package metadata";
            }
            const auto package_index = package_indexes.size();
            package_indexes.push_back(std::move(package));
            const auto record = absolute.parent_path() / "RECORD";
            std::ifstream record_stream(record, std::ios::binary);
            std::string line;
            while (std::getline(record_stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                const auto fields = parse_csv_row(line);
                if (fields.empty() || fields.front().empty()) {
                    continue;
                }
                const std::filesystem::path record_relative(fields.front());
                if (record_relative.is_absolute() ||
                    std::ranges::find(record_relative, "..") != record_relative.end()) {
                    continue;
                }
                const auto owned = (package_indexes[package_index].site_root /
                    record_relative).lexically_normal().lexically_relative(result.install_path);
                if (!owned.empty() && !owned.is_absolute() &&
                    std::ranges::find(owned, "..") == owned.end()) {
                    owner_by_file.try_emplace(path_key(owned), package_index);
                }
            }
        }
        for (std::size_t index = 0; index < package_indexes.size(); ++index) {
            const auto metadata_relative = package_indexes[index].metadata_root.lexically_relative(
                result.install_path);
            const auto metadata_prefix = path_key(metadata_relative) + "/";
            for (const auto& [relative, absolute] : installed_files) {
                static_cast<void>(absolute);
                if (path_key(relative).starts_with(metadata_prefix)) {
                    owner_by_file.try_emplace(path_key(relative), index);
                }
            }
        }

        struct DependencyFile final {
            std::filesystem::path path;
            std::uintmax_t bytes{0};
            std::string pack_id;
        };
        std::unordered_map<std::string, DependencyFile> dependency_files;
        for (const auto& dependency : result.descriptor.dependencies) {
            if (!installed(dependency)) {
                continue;
            }
            const auto dependency_root = pack_path(dependency);
            std::filesystem::recursive_directory_iterator dependency_iterator(
                dependency_root,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            while (dependency_iterator != end) {
                if (stop_token.stop_requested()) {
                    throw std::runtime_error("capability-pack audit cancelled");
                }
                const auto status = dependency_iterator->symlink_status(error);
                if (error) {
                    throw std::runtime_error("unable to inspect dependency pack: " + error.message());
                }
                if (std::filesystem::is_symlink(status)) {
                    dependency_iterator.disable_recursion_pending();
                } else if (std::filesystem::is_regular_file(status)) {
                    const auto relative = dependency_iterator->path().lexically_relative(dependency_root);
                    dependency_files.try_emplace(path_key(relative), DependencyFile{
                        .path = dependency_iterator->path(),
                        .bytes = dependency_iterator->file_size(error),
                        .pack_id = dependency,
                    });
                    if (error) {
                        throw std::runtime_error("unable to measure dependency file: " + error.message());
                    }
                }
                dependency_iterator.increment(error);
                if (error) {
                    throw std::runtime_error("unable to enumerate dependency pack: " + error.message());
                }
            }
        }
        for (const auto& descriptor : catalog()) {
            if (descriptor.id == id || !installed(descriptor.id) ||
                std::ranges::find(descriptor.dependencies, id) == descriptor.dependencies.end()) {
                continue;
            }
            result.required_by.push_back(descriptor.id);
        }

        result.files.reserve(installed_files.size());
        for (const auto& [relative, absolute] : installed_files) {
            if (stop_token.stop_requested()) {
                throw std::runtime_error("capability-pack audit cancelled");
            }
            const auto bytes = std::filesystem::file_size(absolute, error);
            if (error) {
                throw std::runtime_error("unable to measure installed file: " + error.message());
            }
            const auto classification = classify_file(relative);
            CapabilityPackFileAudit file{
                .relative_path = relative,
                .bytes = bytes,
                .purpose = classification.purpose,
                .owner = "pack wrapper or unclaimed data",
            };
            const auto owner = owner_by_file.find(path_key(relative));
            if (owner != owner_by_file.end()) {
                auto& package = package_indexes[owner->second].audit;
                file.owner = package.name + " " + package.version;
                ++package.file_count;
                package.bytes += bytes;
            }
            const auto dependency = dependency_files.find(path_key(relative));
            if (dependency != dependency_files.end() && dependency->second.bytes == bytes &&
                files_identical(absolute, dependency->second.path, stop_token)) {
                file.duplicate_of = dependency->second.pack_id + "/" + relative.generic_string();
            }
            result.installed_bytes += bytes;
            switch (classification.bucket) {
            case AuditBucket::model_weight:
                ++result.model_weight_files;
                result.model_weight_bytes += bytes;
                break;
            case AuditBucket::runtime:
                result.runtime_bytes += bytes;
                break;
            case AuditBucket::program:
                result.program_bytes += bytes;
                break;
            case AuditBucket::metadata:
                result.metadata_bytes += bytes;
                break;
            case AuditBucket::other:
                result.other_bytes += bytes;
                break;
            }
            result.files.push_back(std::move(file));
        }
        result.file_count = result.files.size();
        result.packages.reserve(package_indexes.size());
        for (auto& package : package_indexes) {
            result.packages.push_back(std::move(package.audit));
        }
        std::ranges::sort(result.packages, {}, [](const auto& package) {
            return lower_ascii(package.name);
        });
        return result;
    } catch (const std::exception& exception) {
        result.state = CapabilityPackState::failed;
        result.error = exception.what();
        return result;
    }
}
std::string_view to_string(const CapabilityPackKind kind) noexcept {
    switch (kind) {
    case CapabilityPackKind::compact_native_inference: return "compact-native-inference";
    case CapabilityPackKind::internal_pytorch_training: return "internal-pytorch-training";
    case CapabilityPackKind::transformers_peft_finetuning: return "transformers-peft-finetuning";
    case CapabilityPackKind::image_engine: return "image-engine";
    case CapabilityPackKind::audio_engine: return "audio-engine";
    }
    return "compact-native-inference";
}

std::string_view to_string(const CapabilityPackState state) noexcept {
    switch (state) {
    case CapabilityPackState::available: return "available";
    case CapabilityPackState::installing: return "installing";
    case CapabilityPackState::installed: return "installed";
    case CapabilityPackState::failed: return "failed";
    }
    return "available";
}

} // namespace threadspire::ai::runtime
