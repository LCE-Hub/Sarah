#include "Setup.hpp"
#include "Util.hpp"
#include <exception>
#include <iostream>
#include <string>
#include <vector>
namespace {
void print_usage() {
    std::cout << ">sarah setup build [options]\n";
    std::cout << "\n";
    std::cout << "options:\n";
    std::cout << "  --root <path>       project root containing the solution (default: current directory)\n";
    std::cout << "  --solution <path>   solution file (default: first .sln in root)\n";
    std::cout << "  --build <path>      build output directory (default: <root>/build)\n";
    std::cout << "  --platform <name>   target platform (default: Windows64)\n";
    std::cout << "  --config <name>     default configuration (default: Release)\n";
    std::cout << "  --help              show this message rn\n";
}

bool read_value(const std::vector<std::string>& arguments,
                std::size_t& index,
                std::string& output) {
    if (index + 1 >= arguments.size()) {
        return false;
    }
    ++index;
    output = arguments[index];
    return true;
}

}

int main(int argc, char** argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.empty()) {
        print_usage();
        return 1;
    }

    for (const auto& argument : arguments) {
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
    }

    if (arguments.size() < 2 || arguments[0] != "setup" || arguments[1] != "build") {
        std::cerr << "sarah: unknown command\n";
        print_usage();
        return 1;
    }

    sarah::SetupOptions options;
    options.root = sarah::fs::current_path();
    for (std::size_t index = 2; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        std::string value;
        if (argument == "--root") {
            if (!read_value(arguments, index, value)) {
                std::cerr << "sarah: --root requires a value\n";
                return 1;
            }
            options.root = value;
        } else if (argument == "--solution") {
            if (!read_value(arguments, index, value)) {
                std::cerr << "sarah: --solution requires a value\n";
                return 1;
            }
            options.solution = value;
        } else if (argument == "--build") {
            if (!read_value(arguments, index, value)) {
                std::cerr << "sarah: --build requires a value\n";
                return 1;
            }
            options.build_directory = value;
        } else if (argument == "--platform") {
            if (!read_value(arguments, index, value)) {
                std::cerr << "sarah: --platform requires a value\n";
                return 1;
            }
            options.platform = value;
        } else if (argument == "--config") {
            if (!read_value(arguments, index, value)) {
                std::cerr << "sarah: --config requires a value\n";
                return 1;
            }
            options.configuration = value;
        } else {
            std::cerr << "sarah: unknown option: " << argument << "\n";
            return 1;
        }
    }

    try {
        return sarah::setup_build(options);
    } catch (const std::exception& error) {
        std::cerr << "sarah: " << error.what() << "\n";
        return 1;
    }
}
