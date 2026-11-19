#include "Solution.hpp"
#include "Condition.hpp"
#include <algorithm>
#include <regex>
#include <set>
namespace sarah {
namespace {
std::string mapping_key(const std::string& guid,
                        const std::string& configuration,
                        const std::string& platform) {
    return guid + "|" + configuration + "|" + platform;
}

void add_unique(std::vector<std::string>& values, const std::string& value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}
}

Solution::Solution(const fs::path& path)
    : path_(fs::absolute(path).lexically_normal()),
      directory_(path_.parent_path()),
      name_(path_.stem().string()) {
    std::string content = read_file(path_);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= content.size()) {
        std::size_t end = content.find('\n', start);
        if (end == std::string::npos) {
            end = content.size();
        }
        std::string line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
        if (end == content.size()) {
            break;
        }
        start = end + 1;
    }

    std::regex project_pattern(
        R"re(Project\("\{[^}]*\}"\)\s*=\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"\{([^}]*)\}")re"); //neo: i swear my eyes hurt
    enum class Section {
        None,
        SolutionConfigurations,
        ProjectConfigurations,
    };

    Section section = Section::None;
    for (const auto& line : lines) {
        std::string trimmed = trim(line);
        if (starts_with(trimmed, "Project(")) {
            std::smatch match;
            if (std::regex_match(trimmed, match, project_pattern)) {
                SolutionProject project;
                project.name = match[1].str();
                project.path = match[2].str();
                project.guid = to_lower(match[3].str());
                project.absolute_path = resolve_path(directory_, project.path);
                projects_.push_back(std::move(project));
            }
            continue;
        }
        if (starts_with(trimmed, "GlobalSection(SolutionConfigurationPlatforms)")) {
            section = Section::SolutionConfigurations;
            continue;
        }
        if (starts_with(trimmed, "GlobalSection(ProjectConfigurationPlatforms)")) {
            section = Section::ProjectConfigurations;
            continue;
        }
        if (starts_with(trimmed, "EndGlobalSection")) {
            section = Section::None;
            continue;
        }

        if (section == Section::SolutionConfigurations) {
            std::size_t equal = trimmed.find('=');
            if (equal == std::string::npos) {
                continue;
            }
            std::string key = trim(trimmed.substr(0, equal));
            std::vector<std::string> parts = split(key, '|');
            if (parts.size() != 2) {
                continue;
            }
            add_unique(configurations_, parts[0]);
            add_unique(platforms_, parts[1]);
            add_unique(config_platforms_, parts[0] + "|" + parts[1]);
            continue;
        }

        if (section == Section::ProjectConfigurations) {
            std::size_t equal = trimmed.find('=');
            if (equal == std::string::npos) {
                continue;
            }
            std::string key = trim(trimmed.substr(0, equal));
            std::string value = trim(trimmed.substr(equal + 1));
            std::string suffix;
            if (ends_with(key, ".ActiveCfg")) {
                suffix = "ActiveCfg";
                key.resize(key.size() - 10);
            } else if (ends_with(key, ".Build.0")) {
                suffix = "Build.0";
                key.resize(key.size() - 8);
            } else {
                continue;
            }
            std::size_t first_dot = key.find('.');
            if (first_dot == std::string::npos) {
                continue;
            }
            std::string guid = to_lower(key.substr(0, first_dot));
            if (!guid.empty() && guid.front() == '{') {
                guid.erase(guid.begin());
            }
            if (!guid.empty() && guid.back() == '}') {
                guid.pop_back();
            }
            std::string config_platform = key.substr(first_dot + 1);
            std::vector<std::string> parts = split(config_platform, '|');
            if (parts.size() != 2) {
                continue;
            }
            std::string map_key = mapping_key(guid, parts[0], parts[1]);
            ProjectMapping& mapping = mappings_[map_key];
            if (suffix == "ActiveCfg") {
                std::vector<std::string> target = split(value, '|');
                if (target.size() == 2) {
                    mapping.configuration = target[0];
                    mapping.platform = target[1];
                }
            } else if (suffix == "Build.0") {
                mapping.build = true;
            }
        }
    }
}

const fs::path& Solution::path() const {
    return path_;
}

const fs::path& Solution::directory() const {
    return directory_;
}

const std::string& Solution::name() const {
    return name_;
}

const std::vector<SolutionProject>& Solution::projects() const {
    return projects_;
}

const std::vector<std::string>& Solution::configurations() const {
    return configurations_;
}

const std::vector<std::string>& Solution::platforms() const {
    return platforms_;
}

const SolutionProject* Solution::find_project(std::string_view guid) const {
    std::string wanted = to_lower(std::string(guid));
    for (const auto& project : projects_) {
        if (project.guid == wanted) {
            return &project;
        }
    }
    return nullptr;
}

const SolutionProject* Solution::find_project_by_name(std::string_view name) const {
    std::string wanted = to_lower(std::string(name));
    for (const auto& project : projects_) {
        if (to_lower(project.name) == wanted) {
            return &project;
        }
    }
    return nullptr;
}

bool Solution::has_configuration(const std::string& configuration, const std::string& platform) const {
    return std::find(config_platforms_.begin(),
                     config_platforms_.end(),
                     configuration + "|" + platform) != config_platforms_.end();
}

ProjectMapping Solution::mapping(const std::string& guid,
                                 const std::string& configuration,
                                 const std::string& platform) const {
    auto found = mappings_.find(mapping_key(to_lower(guid), configuration, platform));
    if (found == mappings_.end()) {
        return {};
    }
    return found->second;
}

std::vector<std::string> solution_configurations_for_platform(const Solution& solution,
                                                              const std::string& platform) {
    std::vector<std::string> result;
    for (const auto& configuration : solution.configurations()) {
        if (solution.has_configuration(configuration, platform)) {
            result.push_back(configuration);
        }
    }
    return result;
}
}
