#include "engcoder/core/config.hpp"
#include "engcoder/runtime/runtime_host.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void apply_arguments(engcoder::core::Config& config, int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--workspace" && index + 1 < argc) {
            config.workspace = argv[++index];
        } else if (argument == "--web-root" && index + 1 < argc) {
            config.web_root = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            config.server_port = static_cast<std::uint16_t>(std::stoul(argv[++index]));
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "EngCoderServer options:\n"
                << "  --workspace <path>  Workspace exposed to EngCoder tools\n"
                << "  --web-root <path>   Static EngCoder web-client directory\n"
                << "  --port <number>     Loopback HTTP port (default 14321)\n";
            std::exit(0);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto config = engcoder::core::Config::from_environment();
        apply_arguments(config, argc, argv);
        engcoder::runtime::RuntimeHost host(std::move(config));
        return host.serve_forever() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
