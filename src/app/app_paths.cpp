module;
#include "module_prelude.hpp"
#include "resource.h"
#include <ShlObj.h>
#include <Windows.h>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <system_error>

module threadspire.platform;

namespace threadspire::ai::app {
namespace {

[[nodiscard]] std::filesystem::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const auto length = ::GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    buffer.resize(length);
    return std::filesystem::path(std::move(buffer)).lexically_normal();
}

[[nodiscard]] std::filesystem::path local_app_data() {
    PWSTR raw_path = nullptr;
    const auto result = ::SHGetKnownFolderPath(
        FOLDERID_LocalAppData,
        KF_FLAG_CREATE,
        nullptr,
        &raw_path);
    if (FAILED(result) || raw_path == nullptr) {
        throw std::runtime_error("SHGetKnownFolderPath(FOLDERID_LocalAppData) failed");
    }
    const std::filesystem::path path(raw_path);
    ::CoTaskMemFree(raw_path);
    return path;
}

void write_resource(
    const int identifier,
    const std::filesystem::path& destination,
    const bool overwrite) {
    if (!overwrite && std::filesystem::is_regular_file(destination)) {
        return;
    }
    const auto resource = ::FindResourceW(nullptr, MAKEINTRESOURCEW(identifier), RT_RCDATA);
    if (resource == nullptr) {
        throw std::runtime_error("embedded resource was not found");
    }
    const auto resource_data = ::LoadResource(nullptr, resource);
    const auto size = ::SizeofResource(nullptr, resource);
    if (resource_data == nullptr || size == 0) {
        throw std::runtime_error("embedded resource could not be loaded");
    }
    const auto* bytes = static_cast<const std::byte*>(::LockResource(resource_data));
    if (bytes == nullptr) {
        throw std::runtime_error("embedded resource could not be locked");
    }

    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = destination.wstring() + L".tmp";
    {
        std::ofstream stream(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("unable to materialize embedded resource");
        }
        stream.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    }
    std::error_code error;
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        throw std::runtime_error("unable to publish embedded resource: " + error.message());
    }
}


} // namespace

AppPaths make_app_paths() {
    AppPaths paths;
    paths.executable = executable_path();
    paths.root = local_app_data() / "ThreadSpire";
    paths.packs = paths.root / "packs";
    paths.outputs = paths.root / "outputs";
    paths.cache = paths.root / "cache";
    paths.candidates = paths.root / "candidates";
    paths.checkpoints = paths.root / "models" / "internal" / "checkpoints";
    const auto training_pack = paths.packs / "internal-pytorch-training";
    paths.runtime = training_pack / "runtime";
    paths.python = paths.runtime / "Scripts" / "python.exe";
    paths.worker = training_pack / "backend" / "model_worker.py";
    paths.internal_model_module = training_pack / "backend" / "internal_model.py";
    paths.curriculum_guard_module = training_pack / "backend" / "curriculum_guard.py";
    paths.model_registry = paths.root / "config" / "models.ini";
    paths.local_server_registry = paths.root / "config" / "local-servers.ini";
    paths.mcp_registry = paths.root / "config" / "mcp.ini";
    paths.ready_file = paths.root / "state" / "server.ready";
    paths.jobs = paths.root / "jobs";
    paths.logs = paths.root / "logs";
    paths.experts = paths.root / "experts";
    paths.memory = paths.root / "memory";
    paths.data = paths.root / "data";
    paths.bootstrap_dataset = paths.data / "bootstrap.jsonl";
    paths.bootstrap_anchors = paths.data / "bootstrap-anchors.jsonl";
    paths.custom_training_template = paths.data / "custom-training-template.jsonl";
    paths.trajectories = paths.root / "trajectories";
    return paths;
}

void initialize_base_resources(const AppPaths& paths) {
    std::filesystem::create_directories(paths.root);
    std::filesystem::create_directories(paths.packs);
    std::filesystem::create_directories(paths.outputs);
    std::filesystem::create_directories(paths.cache);
    std::filesystem::create_directories(paths.candidates);
    std::filesystem::create_directories(paths.checkpoints);
    std::filesystem::create_directories(paths.jobs);
    std::filesystem::create_directories(paths.logs);
    std::filesystem::create_directories(paths.experts);
    std::filesystem::create_directories(paths.memory);
    std::filesystem::create_directories(paths.data);
    std::filesystem::create_directories(paths.trajectories);
    std::filesystem::create_directories(paths.ready_file.parent_path());
    std::error_code stale_marker_error;
    for (const auto marker : {"inference.server", "inference.priority", "training.active", "training.requested"}) {
        std::filesystem::remove(paths.root / "control" / marker, stale_marker_error);
        stale_marker_error.clear();
    }

    write_resource(IDR_DEFAULT_MODELS, paths.model_registry, false);
    write_resource(IDR_SEED_DATA, paths.data / "seed.jsonl", false);
    write_resource(IDR_ANCHOR_DATA, paths.data / "anchors.jsonl", false);
    // These are versioned bundled sources, not user-authored evidence. Refresh them
    // on upgrade so a resumed bootstrap actually receives the stronger curriculum.
    write_resource(IDR_BOOTSTRAP_DATA, paths.bootstrap_dataset, true);
    write_resource(IDR_BOOTSTRAP_ANCHORS, paths.bootstrap_anchors, true);
    write_resource(IDR_CUSTOM_TRAINING_TEMPLATE, paths.custom_training_template, false);
}

} // namespace threadspire::ai::app
