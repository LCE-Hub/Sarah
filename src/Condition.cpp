#include "Condition.hpp"
#include "Util.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
namespace sarah {
namespace {
std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::size_t find_top_level(const std::string& value, const std::string& needle, std::size_t start = 0) {
    char quote = 0;
    int depth = 0;
    for (std::size_t index = start; index + needle.size() <= value.size(); ++index) {
        char character = value[index];
        if (quote != 0) {
            if (character == quote) {
                quote = 0;
            }
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            continue;
        }
        if (character == '(') {
            ++depth;
            continue;
        }
        if (character == ')') {
            if (depth > 0) {
                --depth;
            }
            continue;
        }
        if (depth == 0 && lower_ascii(value.substr(index, needle.size())) == needle) {
            return index;
        }
    }
    return std::string::npos;
}

std::vector<std::string> split_top_level(const std::string& value, const std::string& separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        std::size_t position = find_top_level(value, separator, start);
        if (position == std::string::npos) {
            result.push_back(trim(value.substr(start)));
            break;
        }
        result.push_back(trim(value.substr(start, position - start)));
        start = position + separator.size();
    }
    return result;
}

bool has_outer_parentheses(const std::string& value) {
    if (value.size() < 2 || value.front() != '(' || value.back() != ')') {
        return false;
    }
    int depth = 0;
    char quote = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        char character = value[index];
        if (quote != 0) {
            if (character == quote) {
                quote = 0;
            }
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            continue;
        }
        if (character == '(') {
            ++depth;
        } else if (character == ')') {
            --depth;
            if (depth == 0 && index + 1 != value.size()) {
                return false;
            }
        }
    }
    return depth == 0;
}

std::string unquote(const std::string& value) {
    std::string result = trim(value);
    if (result.size() >= 2) {
        char first = result.front();
        char last = result.back();
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            return result.substr(1, result.size() - 2);
        }
    }
    return result;
}

bool evaluate_expression(const std::string& expression, const Variables& variables);
bool evaluate_exists(const std::string& argument, const Variables& variables) {
    std::string path = unquote(expand_variables(argument, variables));
    if (path.find("$(") != std::string::npos) {
        return false;
    }
    std::error_code error;
    std::filesystem::path value = normalize_slashes(path);
    return std::filesystem::exists(value, error);
}

bool evaluate_expression(const std::string& expression, const Variables& variables) {
    std::string value = trim(expression);
    if (value.empty()) {
        return true;
    }

    if (has_outer_parentheses(value)) {
        return evaluate_expression(value.substr(1, value.size() - 2), variables);
    }

    auto or_parts = split_top_level(value, " or ");
    if (or_parts.size() > 1) {
        for (const auto& part : or_parts) {
            if (evaluate_expression(part, variables)) {
                return true;
            }
        }
        return false;
    }

    auto and_parts = split_top_level(value, " and ");
    if (and_parts.size() > 1) {
        for (const auto& part : and_parts) {
            if (!evaluate_expression(part, variables)) {
                return false;
            }
        }
        return true;
    }

    if (!value.empty() && value.front() == '!' && (value.size() < 2 || value[1] != '=')) {
        return !evaluate_expression(value.substr(1), variables);
    }

    std::string lowered = lower_ascii(value);
    if (starts_with(lowered, "exists(") && value.back() == ')') {
        return evaluate_exists(value.substr(7, value.size() - 8), variables);
    }

    std::size_t not_equal = find_top_level(value, "!=");
    if (not_equal != std::string::npos) {
        std::string left = expand_variables(unquote(value.substr(0, not_equal)), variables);
        std::string right = expand_variables(unquote(value.substr(not_equal + 2)), variables);
        return left != right;
    }

    std::size_t equal = find_top_level(value, "==");
    if (equal != std::string::npos) {
        std::string left = expand_variables(unquote(value.substr(0, equal)), variables);
        std::string right = expand_variables(unquote(value.substr(equal + 2)), variables);
        return left == right;
    }

    if (lowered == "true") {
        return true;
    }
    if (lowered == "false") {
        return false;
    }

    std::string expanded = trim(expand_variables(unquote(value), variables));
    return !expanded.empty() && expanded != "0";
}
}

std::string expand_variables(const std::string& value, const Variables& variables) {
    std::string result;
    result.reserve(value.size());
    std::size_t index = 0;
    while (index < value.size()) {
        if (value[index] == '$' && index + 1 < value.size() && value[index + 1] == '(') {
            std::size_t end = value.find(')', index + 2);
            if (end == std::string::npos) {
                result += value.substr(index);
                break;
            }
            std::string name = value.substr(index + 2, end - index - 2);
            auto found = variables.find(name);
            if (found != variables.end()) {
                result += found->second;
            } else {
                result += value.substr(index, end - index + 1);
            }
            index = end + 1;
            continue;
        }
        result += value[index];
        ++index;
    }
    return result;
}

bool evaluate_condition(const std::string& condition, const Variables& variables) {
    if (trim(condition).empty()) {
        return true;
    }
    return evaluate_expression(condition, variables);
}
}
