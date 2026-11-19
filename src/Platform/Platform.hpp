#pragma once
#include "../Project.hpp"
#include "../Solution.hpp"
#include "../Util.hpp"
#include <memory>
#include <string>
#include <vector>
namespace sarah {
struct Toolchain {
    std::string compiler;
    std::string archiver;
    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;
    std::vector<std::string> include_dirs;
    std::vector<std::string> library_dirs;
};

class Platform {
public:
    virtual ~Platform() = default;
    virtual std::string name() const = 0;
    virtual std::string project_platform() const = 0;
    virtual std::vector<std::string> configurations(const Solution& solution) const = 0;
    virtual Toolchain toolchain(const fs::path& root) const = 0;
    virtual std::vector<fs::path> extra_solutions(const fs::path& root) const = 0;
    virtual void prepare(const fs::path& root, const std::vector<std::string>& configurations) const = 0;
    virtual std::string link_subsystem(const ProjectBuild& build) const = 0;
};

std::unique_ptr<Platform> make_platform(const std::string& name);
}
