#include "engcoder/core/learning_policy_lock.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        std::cout << "starting" << std::endl;
        const engcoder::core::LearningPolicyLock lock(argv[1]);
        std::cout << "locked" << std::endl;
        if (std::string{argv[2]} == "hold") {
            std::string release;
            std::getline(std::cin, release);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
