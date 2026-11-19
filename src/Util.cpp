#include "Util.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
namespace sarah {
std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to read " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void write_file(const fs::path& path, const std::string& content) {
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to write " + path.string());
    }
    stream << content;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& value, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        std::size_t end = value.find(separator, start);
        if (end == std::string::npos) {
            end = value.size();
        }
        std::string token = trim(value.substr(start, end - start));
        if (!token.empty()) {
            result.push_back(token);
        }
        if (end == value.size()) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::string join(const std::vector<std::string>& values, const std::string& separator) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            result += separator;
        }
        result += values[index];
    }
    return result;
}

std::string replace_all(std::string value, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return value;
    }
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
    return value;
}

std::string normalize_slashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

fs::path resolve_path(const fs::path& base, const std::string& value) {
    std::string normalized = normalize_slashes(trim(value));
    if (normalized.empty()) {
        return base;
    }
    fs::path path(normalized);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (base / path).lexically_normal();
}

fs::path find_case_insensitive(const fs::path& path) {
    std::error_code error;
    if (fs::exists(path, error)) {
        return path;
    }

    fs::path current;
    fs::path remainder = path;
    if (path.is_absolute()) {
        current = path.root_path();
        remainder = path.relative_path();
    } else {
        current = fs::current_path();
    }

    for (const auto& component : remainder) {
        std::string name = component.string();
        if (name == ".") {
            continue;
        }
        if (name == "..") {
            current = current.parent_path();
            continue;
        }

        fs::path candidate = current / name;
        if (fs::exists(candidate, error)) {
            current = candidate;
            continue;
        }

        bool found = false;
        if (fs::is_directory(current, error)) {
            std::string wanted = to_lower(name);
            for (const auto& entry : fs::directory_iterator(current, error)) {
                if (to_lower(entry.path().filename().string()) == wanted) {
                    current = entry.path();
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return {};
        }
    }
    return current;
}

std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (char character : value) {
        if (character == '\'') {
            result += "'\\''";
        } else {
            result += character;
        }
    }
    result += "'";
    return result;
}

int run(const std::vector<std::string>& arguments, const fs::path& working_directory) {
    std::string command;
    if (!working_directory.empty()) {
        command += "cd " + shell_quote(working_directory.string()) + " && ";
    }
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            command += " ";
        }
        command += shell_quote(arguments[index]);
    }
    int status = std::system(command.c_str());
    if (status == -1) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

std::string sanitize_name(const std::string& value) {
    std::string result;
    for (char character : value) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            result += character;
        } else {
            result += '_';
        }
    }
    if (result.empty()) {
        result = "_";
    }
    return result;
}

std::string executable_path() {
    std::error_code error;
    fs::path path = fs::read_symlink("/proc/self/exe", error);
    if (error) {
        return {};
    }
    return path.string();
}
}
