#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace sarah {
namespace fs = std::filesystem;
std::string read_file(const fs::path& path);
void write_file(const fs::path& path, const std::string& content);
std::string trim(const std::string& value);
std::string to_lower(std::string value);
bool starts_with(const std::string& value, const std::string& prefix);
bool ends_with(const std::string& value, const std::string& suffix);
std::vector<std::string> split(const std::string& value, char separator);
std::string join(const std::vector<std::string>& values, const std::string& separator);
std::string replace_all(std::string value, const std::string& from, const std::string& to);
std::string normalize_slashes(std::string value);
fs::path resolve_path(const fs::path& base, const std::string& value);
fs::path find_case_insensitive(const fs::path& path);
std::string shell_quote(const std::string& value);
int run(const std::vector<std::string>& arguments, const fs::path& working_directory);
std::string sanitize_name(const std::string& value);
std::string executable_path();
}
