#pragma once
#include "Util.hpp"
#include "Xml.hpp"
#include <map>
#include <string>
#include <vector>
namespace sarah {
struct SourceFile {
    fs::path path;
    std::string relative;
    std::vector<std::string> defines;
    std::vector<std::string> include_dirs;
    std::vector<std::string> forced_includes;
    std::vector<std::string> flags;
};

struct ProjectBuild {
    std::string name;
    fs::path project_path;
    fs::path project_dir;
    std::string configuration;
    std::string platform;
    std::string configuration_type;
    std::string target_name;
    std::string target_ext;
    fs::path output;
    std::vector<SourceFile> sources;
    std::vector<std::string> defines;
    std::vector<std::string> include_dirs;
    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;
    std::vector<std::string> library_paths;
    std::vector<std::string> library_dirs;
    std::vector<std::string> ignore_libraries;
    std::vector<std::string> forced_includes;
    fs::path post_build_script;
    bool is_application() const;
    bool is_static_library() const;
};

class Project {
public:
    explicit Project(const fs::path& path);
    const fs::path& path() const;
    const fs::path& directory() const;
    const std::string& name() const;
    const std::vector<std::pair<std::string, std::string>>& configurations() const;
    ProjectBuild evaluate(const std::string& configuration,
                          const std::string& platform,
                          const fs::path& solution_directory) const;

private:
    fs::path path_;
    fs::path directory_;
    std::string name_;
    XmlNode root_;
    std::vector<std::pair<std::string, std::string>> configurations_;
};
}
