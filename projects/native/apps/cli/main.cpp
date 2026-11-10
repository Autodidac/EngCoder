#include "engcoder/core/config.hpp"
#include "engcoder/model/lm_studio_provider.hpp"
#include "engcoder/process/process.hpp"
#include "engcoder/runtime/runtime_host.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using json = nlohmann::json;

void print_doctor(const engcoder::core::Config& config, const json& discovery) {
    std::cout << "EngCoder local model server doctor\n"
              << "  Model server URL:  " << config.lm_studio_url << '\n'
              << "  Backend:           " << discovery.value("backend", std::string{"unknown"}) << '\n'
              << "  Native v1:         " << (discovery.value("native_v1", false) ? "yes" : "no") << '\n'
              << "  OpenAI compatible: " << (discovery.value("openai_compatible", false) ? "yes" : "no") << '\n'
              << "  Legacy v0:         " << (discovery.value("legacy_v0", false) ? "yes" : "no") << '\n'
              << "  Agent transport:   " << discovery.value("preferred_agent_transport", std::string{"unknown"}) << '\n'
              << "  Selected model:    " << discovery.value("selected_model", std::string{"auto"}) << '\n';

    if (discovery.contains("models") && discovery["models"].is_array()) {
        std::cout << "\nAvailable LLMs:\n";
        for (const auto& model : discovery["models"]) {
            std::cout << "  - " << model.value("id", std::string{"<unknown>"});
            if (model.value("loaded", false)) std::cout << " [loaded]";
            if (model.value("tool_use", false)) std::cout << " [tool-use]";
            std::cout << '\n';
        }
    }
}

int run_doctor(bool json_output) {
    auto config = engcoder::core::Config::from_environment();
    engcoder::model::LmStudioProvider provider(config);
    const auto discovery = provider.discover();
    if (!discovery) {
        const auto& message = discovery.error().message;
        const bool authentication_required =
            message.find("HTTP 401") != std::string::npos ||
            message.find("invalid_api_key") != std::string::npos;

        if (authentication_required) {
            std::cerr << "EngCoder reached the local model server, but authentication failed.\n\n"
                      << message << "\n\n"
                      << "Set the token configured by Bionic/LM Studio:\n"
                      << "  $env:LM_API_TOKEN='your-token'\n";
        } else {
            std::cerr << "EngCoder could not connect to the local model API server.\n\n"
                      << message << "\n";
        }
        return 2;
    }

    if (json_output) std::cout << discovery->dump(2) << '\n';
    else print_doctor(config, *discovery);
    return 0;
}

int run_boundary_lab(int argc, char** argv) {
    const auto config = engcoder::core::Config::from_environment();
    const auto worker = std::filesystem::absolute(config.training_backend).parent_path() / "boundary_lab.py";
    if (!std::filesystem::is_regular_file(worker)) {
        std::cerr << "C++ boundary lab worker was not found at " << worker.string() << '\n';
        return 2;
    }
    engcoder::process::Request request;
    request.executable = config.python_executable;
    request.arguments.push_back(worker.string());
    for (int i = 2; i < argc; ++i) request.arguments.emplace_back(argv[i]);
    request.working_directory = std::filesystem::current_path();
    request.timeout = std::chrono::minutes{20};
    const auto result = engcoder::process::run(request);
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 2;
    }
    std::cout << result->stdout_text;
    std::cerr << result->stderr_text;
    return result->timed_out || result->cancelled ? 2 : result->exit_code;
}

int run_prompt(
    engcoder::runtime::RuntimeHost& runtime,
    std::string message,
    std::string& session_id) {
    bool started_output = false;
    const auto outcome = runtime.run_task(
        "cli",
        session_id,
        message,
        {},
        [&](const engcoder::core::Event& event) {
            if (event.type == "model.text_delta") {
                if (!started_output) {
                    std::cout << "assistant> " << std::flush;
                    started_output = true;
                }
                std::cout << event.data.value("delta", std::string{}) << std::flush;
            } else if (event.type == "tool.requested") {
                if (started_output) std::cout << '\n';
                std::cout << "[tool] " << event.data.value("name", std::string{"unknown"})
                          << " " << event.data.value("arguments", json::object()).dump() << '\n';
                started_output = false;
            } else if (event.type == "tool.completed") {
                const auto probabilities = event.data.value("review_probabilities", json::object());
                std::cout << "[review] " << event.data.value("reviewer", std::string{"policy"})
                          << " confidence=" << event.data.value("review_confidence", 0.0);
                if (!probabilities.empty()) {
                    std::cout << " allow=" << probabilities.value("allow", 0.0)
                              << " approval=" << probabilities.value("approval", 0.0)
                              << " deny=" << probabilities.value("deny", 0.0);
                }
                std::cout << '\n';
            }
        });

    if (!outcome) {
        std::cerr << "EngCoder task failed to run: " << outcome.error().message << '\n';
        return 4;
    }

    session_id = outcome->session_id;
    switch (outcome->terminal) {
        case engcoder::runtime::RunTerminal::completed:
            if (!started_output) {
                std::cout << "assistant> " << outcome->final_text;
            }
            std::cout << '\n';
            return 0;
        case engcoder::runtime::RunTerminal::failed:
            std::cerr << "\n[task failed] "
                      << outcome->terminal_data.value("error", outcome->terminal_data.dump()) << '\n';
            return 5;
        case engcoder::runtime::RunTerminal::awaiting_approval:
            std::cerr << "\n[approval required] " << outcome->terminal_data.dump(2) << '\n';
            return 6;
        case engcoder::runtime::RunTerminal::cancelled:
            std::cerr << "\n[task cancelled]\n";
            return 7;
    }
    return 5;
}

std::string join_arguments(int argc, char** argv, int start) {
    std::ostringstream out;
    for (int index = start; index < argc; ++index) {
        if (index != start) out << ' ';
        out << argv[index];
    }
    return out.str();
}

void print_help() {
    std::cout
        << "EngCoder CLI\n\n"
        << "The CLI embeds the native EngCoder runtime; no HTTP server is required.\n\n"
        << "Usage:\n"
        << "  EngCoderCLI                 Interactive agent session\n"
        << "  EngCoderCLI <prompt...>     Run a one-shot EngCoder task\n"
        << "  EngCoderCLI --doctor        Diagnose Bionic/LM Studio backend\n"
        << "  EngCoderCLI --doctor --json Machine-readable backend discovery\n"
        << "  EngCoderCLI --boundary-lab --help  C++ repair research, training, and evaluation\n"
        << "  EngCoderCLI --help          Show this help\n\n"
        << "Interactive commands: /new, /doctor, /help, /quit\n";
}

int interactive_chat() {
    engcoder::runtime::RuntimeHost runtime;
    auto session_id = engcoder::runtime::make_session_id("cli");

    std::cout << "EngCoder interactive CLI\n"
              << "Runtime: embedded native\n"
              << "Decision layer: " << runtime.decision_mode() << "\n"
              << "Type /new, /doctor, /help, or /quit.\n\n";

    for (;;) {
        std::cout << "you> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << '\n';
            return 0;
        }
        if (line.empty()) continue;
        if (line == "/quit" || line == "/exit") return 0;
        if (line == "/help") { print_help(); continue; }
        if (line == "/doctor") { (void)run_doctor(false); continue; }
        if (line == "/new") {
            session_id = engcoder::runtime::make_session_id("cli");
            std::cout << "New session: " << session_id << '\n';
            continue;
        }

        const auto status = run_prompt(runtime, std::move(line), session_id);
        if (status != 0) return status;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string_view first{argv[1]};
        if (first == "--help" || first == "-h") {
            print_help();
            return 0;
        }
        if (first == "--doctor") {
            const bool json_output = argc > 2 && std::string_view{argv[2]} == "--json";
            return run_doctor(json_output);
        }
        if (first == "--boundary-lab") return run_boundary_lab(argc, argv);

        engcoder::runtime::RuntimeHost runtime;
        auto session_id = engcoder::runtime::make_session_id("cli-oneshot");
        return run_prompt(runtime, join_arguments(argc, argv, 1), session_id);
    }

    return interactive_chat();
}
