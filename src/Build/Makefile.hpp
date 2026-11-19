#pragma once
#include "../Platform/Platform.hpp"
#include "../Project.hpp"
#include "../Util.hpp"
#include <string>
#include <vector>
namespace sarah {
struct MakefileTarget {
    ProjectBuild build;
    fs::path object_directory;
    std::vector<std::string> extra_link_flags;
    std::vector<std::string> post_build_commands;
};

struct MakefileConfig {
    fs::path build_directory;
    fs::path root;
    fs::path solution;
    fs::path sarah;
    std::string platform;
    std::string configuration;
    Toolchain toolchain;
    std::vector<MakefileTarget> targets;
    std::vector<fs::path> dependencies;
};

void write_configuration_makefile(const fs::path& path, const MakefileConfig& config);
void write_top_level_makefile(const fs::path& path, const MakefileConfig& config);
}
