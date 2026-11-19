#pragma once
#include "Util.hpp"
#include <map>
#include <string>
#include <vector>
namespace sarah {
struct SolutionProject {
    std::string name;
    std::string path;
    std::string guid;
    fs::path absolute_path;
};

struct ProjectMapping {
    std::string configuration;
    std::string platform;
    bool build = false;
};

class Solution {
public:
    explicit Solution(const fs::path& path);
    const fs::path& path() const;
    const fs::path& directory() const;
    const std::string& name() const;
    const std::vector<SolutionProject>& projects() const;
    const std::vector<std::string>& configurations() const;
    const std::vector<std::string>& platforms() const;
    const SolutionProject* find_project(std::string_view guid) const;
    const SolutionProject* find_project_by_name(std::string_view name) const;
    bool has_configuration(const std::string& configuration, const std::string& platform) const;
    ProjectMapping mapping(const std::string& guid,
                           const std::string& configuration,
                           const std::string& platform) const;

private:
    fs::path path_;
    fs::path directory_;
    std::string name_;
    std::vector<SolutionProject> projects_;
    std::vector<std::string> configurations_;
    std::vector<std::string> platforms_;
    std::vector<std::string> config_platforms_;
    std::map<std::string, ProjectMapping> mappings_;
};

std::vector<std::string> solution_configurations_for_platform(const Solution& solution,
                                                              const std::string& platform);
}
