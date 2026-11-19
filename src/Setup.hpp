#pragma once
#include "Util.hpp"
#include <string>
namespace sarah {
struct SetupOptions {
    fs::path root;
    fs::path solution;
    fs::path build_directory;
    std::string platform = "Windows64"; //neo: default because meow
    std::string configuration;
};

int setup_build(const SetupOptions& options);
}
