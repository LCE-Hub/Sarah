#include "PostBuild.hpp"
#include <regex>
#include <sstream>
namespace sarah {
namespace { //neo: please dont judge me for this :sob:
enum class ActionType {
    MakeDirectory,
    CopyFile,
    CopyDirectory,
    RemoveDirectory,
    DeleteFiles
};

struct Action {
    ActionType type;
    fs::path source;
    fs::path destination;
    std::vector<std::string> patterns;
};

std::string extract_array(const std::string& text, const std::string& name) {
    std::string marker = "$" + name + " = @(";
    std::size_t start = text.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    start += marker.size();
    int depth = 1;
    for (std::size_t index = start; index < text.size(); ++index) {
        if (text[index] == '(') {
            ++depth;
        } else if (text[index] == ')') {
            --depth;
            if (depth == 0) {
                return text.substr(start, index - start);
            }
        }
    }
    return {};
}

std::vector<std::string> quoted_values(const std::string& text) {
    std::vector<std::string> result;
    std::regex pattern(R"re("([^"]+)")re");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    for (auto iterator = begin; iterator != end; ++iterator) {
        result.push_back((*iterator)[1].str());
    }
    return result;
}

std::vector<std::string> hashtable_blocks(const std::string& text) {
    std::vector<std::string> result;
    std::regex pattern(R"re(@\{([^}]*)\})re");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    for (auto iterator = begin; iterator != end; ++iterator) {
        result.push_back((*iterator)[1].str());
    }
    return result;
}

std::string field_value(const std::string& block, const std::string& field) {
    std::regex pattern("\\b" + field + "\\s*=\\s*([^;}]+)");
    std::smatch match;
    if (std::regex_search(block, match, pattern)) {
        return trim(match[1].str());
    }
    return {};
}

fs::path unquote(const std::string& value) {
    std::string trimmed = trim(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
    return normalize_slashes(trimmed);
}

fs::path script_value(const std::string& value,
                      const fs::path& project_dir,
                      const fs::path& script_dir) {
    std::string trimmed = trim(value);
    if (starts_with(trimmed, "Join-Path")) {
        std::regex pattern(R"re(Join-Path\s+\$(\w+)\s+"([^"]+)")re");
        std::smatch match;
        if (std::regex_search(trimmed, match, pattern)) {
            fs::path base = match[1].str() == "PSScriptRoot" ? script_dir : project_dir;
            return resolve_path(base, match[2].str());
        }
    }
    return unquote(trimmed);
}

fs::path source_value(const std::string& value,
                      const fs::path& project_dir,
                      const fs::path& script_dir) {
    fs::path path = script_value(value, project_dir, script_dir);
    if (!path.is_absolute()) {
        path = project_dir / path;
    }
    path = path.lexically_normal();
    fs::path found = find_case_insensitive(path);
    return found.empty() ? path : found;
}

fs::path destination_value(const std::string& value,
                           const fs::path& output_dir,
                           const fs::path& project_dir,
                           const fs::path& script_dir) {
    fs::path path = script_value(value, project_dir, script_dir);
    if (!path.is_absolute()) {
        path = output_dir / path;
    }
    return path.lexically_normal();
}

std::vector<std::string> include_patterns(const std::string& text) {
    std::size_t include = text.find("-Include");
    if (include == std::string::npos) {
        return {};
    }
    std::size_t begin = include + 8;
    std::size_t end = text.find("-File", begin);
    if (end == std::string::npos) {
        end = text.size();
    }
    std::vector<std::string> result;
    for (const auto& token : split(text.substr(begin, end - begin), ',')) {
        std::string value = trim(token);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (!value.empty()) {
            result.push_back(value);
        }
    }
    return result;
}

std::vector<Action> parse_actions(const std::string& text,
                                  const fs::path& project_dir,
                                  const fs::path& script_dir,
                                  const fs::path& output_dir) {
    std::vector<Action> actions;
    for (const auto& value : quoted_values(extract_array(text, "directories"))) {
        actions.push_back({ActionType::MakeDirectory, {}, output_dir / normalize_slashes(value), {}});
    }

    for (const auto& block : hashtable_blocks(extract_array(text, "folderCopies"))) {
        fs::path source = source_value(field_value(block, "Source"), project_dir, script_dir);
        fs::path destination = destination_value(field_value(block, "Dest"), output_dir, project_dir, script_dir);
        actions.push_back({ActionType::CopyDirectory, source, destination, {}});
    }

    for (const auto& block : hashtable_blocks(extract_array(text, "fileCopies"))) {
        fs::path source = source_value(field_value(block, "Source"), project_dir, script_dir);
        fs::path destination = destination_value(field_value(block, "Dest"), output_dir, project_dir, script_dir);
        actions.push_back({ActionType::CopyFile, source, destination, {}});
    }

    for (const auto& value : quoted_values(extract_array(text, "deleteDirs"))) {
        actions.push_back({ActionType::RemoveDirectory, {}, output_dir / normalize_slashes(value), {}});
    }

    std::vector<std::string> patterns = include_patterns(text);
    for (const auto& value : quoted_values(extract_array(text, "delDirs"))) {
        fs::path normalized = normalize_slashes(value);
        std::string normalized_text = normalized.string();
        if (ends_with(normalized_text, "/*")) {
            normalized_text.resize(normalized_text.size() - 2);
        }
        actions.push_back({ActionType::DeleteFiles, {}, output_dir / normalized_text, patterns});
    }

    return actions;
}

std::string command_for(const Action& action) {
    std::ostringstream result;
    switch (action.type) {
    case ActionType::MakeDirectory:
        result << "mkdir -p " << shell_quote(action.destination.string());
        break;
    case ActionType::CopyDirectory: {
        std::string source = action.source.string();
        if (!ends_with(source, "/")) {
            source += "/";
        }
        result << "if [ -d " << shell_quote(action.source.string()) << " ]; then "
               << "mkdir -p " << shell_quote(action.destination.string()) << "; "
               << "cp -a " << shell_quote(source + ".") << " "
               << shell_quote(action.destination.string()) << "/; fi";
        break;
    }
    case ActionType::CopyFile:
        result << "if [ -f " << shell_quote(action.source.string()) << " ]; then "
               << "mkdir -p " << shell_quote(action.destination.parent_path().string()) << "; "
               << "cp -f " << shell_quote(action.source.string()) << " "
               << shell_quote(action.destination.string()) << "; fi";
        break;
    case ActionType::RemoveDirectory:
        result << "rm -rf " << shell_quote(action.destination.string());
        break;
    case ActionType::DeleteFiles:
        if (!action.patterns.empty()) {
            result << "if [ -d " << shell_quote(action.destination.string()) << " ]; then "
                   << "find " << shell_quote(action.destination.string()) << " -type f \\( ";
            for (std::size_t index = 0; index < action.patterns.size(); ++index) {
                if (index != 0) {
                    result << " -o ";
                }
                result << "-name " << shell_quote(action.patterns[index]);
            }
            result << " \\) -delete; fi";
        }
        break;
    }
    return result.str();
}

}

std::vector<std::string> generate_post_build_commands(const fs::path& script,
                                                      const fs::path& project_dir,
                                                      const fs::path& output_dir) {
    std::vector<std::string> commands;
    std::error_code error;
    if (script.empty() || !fs::is_regular_file(script, error)) {
        return commands;
    }
    std::vector<Action> actions = parse_actions(read_file(script),
                                                project_dir,
                                                script.parent_path(),
                                                output_dir);
    for (const auto& action : actions) {
        std::string command = command_for(action);
        if (!command.empty()) {
            commands.push_back(command);
        }
    }
    return commands;
}

}
