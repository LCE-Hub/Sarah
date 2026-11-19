#include "SourceTree.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
namespace sarah {
namespace {
bool path_within(const fs::path& path, const fs::path& directory) {
    fs::path relative = path.lexically_relative(directory);
    if (relative.empty()) {
        return path == directory;
    }
    std::string value = relative.string();
    return !starts_with(value, "..");
}

bool excluded_path(const fs::path& path, const std::vector<fs::path>& exclude) {
    for (const auto& entry : exclude) {
        if (path_within(path, entry)) {
            return true;
        }
    }
    return false;
}

bool source_extension(const fs::path& path) {
    std::string extension = to_lower(path.extension().string());
    return extension == ".h" || extension == ".hpp" || extension == ".hxx" ||
           extension == ".inl" || extension == ".inc" || extension == ".cpp" ||
           extension == ".c" || extension == ".cc" || extension == ".cxx";
}

bool utf16_encoding(const std::string& bytes, bool& little_endian) {
    if (bytes.size() < 2) {
        return false;
    }
    unsigned char first = static_cast<unsigned char>(bytes[0]);
    unsigned char second = static_cast<unsigned char>(bytes[1]);
    if (first == 0xff && second == 0xfe) {
        little_endian = true;
        return true;
    }
    if (first == 0xfe && second == 0xff) {
        little_endian = false;
        return true;
    }
    return false;
}

void append_utf8(std::string& output, std::uint32_t code_point) {
    if (code_point < 0x80) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point < 0x10000) {
        output.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
}

std::string decode_utf16(const std::string& bytes, bool little_endian) {
    std::string result;
    for (std::size_t index = 2; index + 1 < bytes.size(); index += 2) {
        unsigned char first = static_cast<unsigned char>(bytes[index]);
        unsigned char second = static_cast<unsigned char>(bytes[index + 1]);
        std::uint32_t code_point = little_endian
            ? (static_cast<std::uint32_t>(first) | (static_cast<std::uint32_t>(second) << 8))
            : ((static_cast<std::uint32_t>(first) << 8) | static_cast<std::uint32_t>(second));
        if (code_point >= 0xd800 && code_point <= 0xdbff && index + 3 < bytes.size()) {
            unsigned char low_first = static_cast<unsigned char>(bytes[index + 2]);
            unsigned char low_second = static_cast<unsigned char>(bytes[index + 3]);
            std::uint32_t low = little_endian
                ? (static_cast<std::uint32_t>(low_first) | (static_cast<std::uint32_t>(low_second) << 8))
                : ((static_cast<std::uint32_t>(low_first) << 8) | static_cast<std::uint32_t>(low_second));
            if (low >= 0xdc00 && low <= 0xdfff) {
                code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
                index += 2;
            }
        }
        append_utf8(result, code_point);
    }
    return result;
}

std::string read_text_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string bytes = buffer.str();
    bool little_endian = true;
    if (utf16_encoding(bytes, little_endian)) {
        return decode_utf16(bytes, little_endian);
    }
    return bytes;
}

std::vector<std::string> read_includes(const fs::path& path) {
    std::vector<std::string> includes;
    std::istringstream stream(read_text_file(path));
    std::string line;
    while (std::getline(stream, line)) {
        std::size_t position = line.find('#');
        if (position == std::string::npos) {
            continue;
        }
        ++position;
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        if (line.compare(position, 7, "include") != 0) {
            continue;
        }
        position += 7;
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        if (position >= line.size() || line[position] != '"') {
            continue;
        }
        std::size_t end = line.find('"', position + 1);
        if (end == std::string::npos) {
            continue;
        }
        includes.push_back(line.substr(position + 1, end - position - 1));
    }
    return includes;
}

bool is_absolute_include(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    if (value[0] == '/' || value[0] == '\\') {
        return true;
    }
    return value.size() > 2 && std::isalpha(static_cast<unsigned char>(value[0])) &&
           value[1] == ':';
}

void ensure_alias(const fs::path& root,
                  const fs::path& mirror_root,
                  const fs::path& requested,
                  const fs::path& canonical) {
    fs::path requested_relative = requested.lexically_relative(root);
    fs::path canonical_relative = canonical.lexically_relative(root);
    if (requested_relative.empty() || canonical_relative.empty()) {
        return;
    }
    if (starts_with(requested_relative.string(), "..") ||
        starts_with(canonical_relative.string(), "..")) {
        return;
    }

    fs::path parent = mirror_root;
    auto requested_iterator = requested_relative.begin();
    auto canonical_iterator = canonical_relative.begin();
    while (requested_iterator != requested_relative.end() &&
           canonical_iterator != canonical_relative.end()) {
        std::string requested_name = requested_iterator->string();
        std::string canonical_name = canonical_iterator->string();
        fs::path canonical_path = parent / canonical_name;
        if (requested_name != canonical_name) {
            fs::path alias = parent / requested_name;
            std::error_code error;
            if (!fs::exists(alias, error)) {
                fs::create_symlink(canonical_path, alias, error);
            }
        }
        parent = canonical_path;
        ++requested_iterator;
        ++canonical_iterator;
    }
}

void add_aliases(const fs::path& root,
                 const fs::path& mirror_root,
                 const fs::path& file,
                 const std::vector<fs::path>& include_dirs,
                 const std::map<std::string, fs::path>& index) {
    for (const auto& include : read_includes(file)) {
        if (is_absolute_include(include)) {
            continue;
        }
        std::string normalized = replace_all(include, "\\", "/");
        fs::path requested = (file.parent_path() / normalized).lexically_normal();
        auto found = index.find(to_lower(requested.string()));
        fs::path canonical;
        if (found != index.end()) {
            canonical = found->second;
        } else {
            for (const auto& include_dir : include_dirs) {
                fs::path candidate = (include_dir / normalized).lexically_normal();
                auto entry = index.find(to_lower(candidate.string()));
                if (entry != index.end()) {
                    requested = candidate;
                    canonical = entry->second;
                    break;
                }
            }
        }
        if (canonical.empty() || !path_within(canonical, root)) {
            continue;
        }
        std::error_code error;
        if (!fs::is_regular_file(canonical, error)) {
            continue;
        }
        if (!path_within(requested, root)) {
            continue;
        }
        ensure_alias(root, mirror_root, requested, canonical);
    }
}

}

SourceMirror create_source_mirror(const fs::path& root,
                                  const fs::path& directory,
                                  const std::vector<fs::path>& exclude,
                                  const std::vector<fs::path>& include_dirs) {
    SourceMirror mirror;
    mirror.root = fs::absolute(root).lexically_normal();
    mirror.directory = fs::absolute(directory).lexically_normal();
    std::error_code error;
    fs::remove_all(mirror.directory, error);
    fs::create_directories(mirror.directory, error);
    std::vector<fs::path> excluded = exclude;
    excluded.push_back(mirror.directory);
    std::map<std::string, fs::path> index;
    fs::recursive_directory_iterator iterator(mirror.root, fs::directory_options::skip_permission_denied, error);
    fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        fs::path path = iterator->path();
        if (excluded_path(path, excluded)) {
            if (iterator->is_directory(error)) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }

        index[to_lower(path.lexically_normal().string())] = path;
        fs::path relative = path.lexically_relative(mirror.root);
        fs::path target = mirror.directory / relative;
        std::error_code status_error;
        if (iterator->is_symlink(status_error)) {
            fs::create_symlink(fs::absolute(path), target, status_error);
        } else if (iterator->is_directory(status_error)) {
            fs::create_directories(target, status_error);
        } else if (iterator->is_regular_file(status_error)) {
            fs::create_directories(target.parent_path(), status_error);
            std::ifstream probe(path, std::ios::binary);
            char header[2] = {};
            std::size_t header_size = 0;
            if (probe) {
                probe.read(header, 2);
                header_size = static_cast<std::size_t>(probe.gcount());
            }
            bool little_endian = true;
            if (utf16_encoding(std::string(header, header_size), little_endian)) {
                std::ofstream output(target, std::ios::binary);
                output << read_text_file(path);
            } else {
                fs::create_symlink(fs::absolute(path), target, status_error);
            }
        }
        iterator.increment(error);
    }

    std::vector<fs::path> resolved_include_dirs;
    for (const auto& include_dir : include_dirs) {
        if (!path_within(include_dir, mirror.root)) {
            continue;
        }
        resolved_include_dirs.push_back(fs::absolute(include_dir).lexically_normal());
    }

    fs::recursive_directory_iterator scan(mirror.root, fs::directory_options::skip_permission_denied, error);
    while (!error && scan != end) {
        fs::path path = scan->path();
        if (excluded_path(path, excluded)) {
            if (scan->is_directory(error)) {
                scan.disable_recursion_pending();
            }
            scan.increment(error);
            continue;
        }
        if (scan->is_regular_file(error) && source_extension(path)) {
            add_aliases(mirror.root, mirror.directory, path, resolved_include_dirs, index);
        }
        scan.increment(error);
    }

    return mirror;
}

fs::path mirror_path(const SourceMirror& mirror, const fs::path& path) {
    if (path.empty()) {
        return path;
    }
    fs::path absolute = path.is_absolute() ? path.lexically_normal()
                                           : fs::absolute(path).lexically_normal();
    if (!path_within(absolute, mirror.root)) {
        return path;
    }
    fs::path relative = absolute.lexically_relative(mirror.root);
    if (relative.empty() || starts_with(relative.string(), "..")) {
        return path;
    }
    return mirror.directory / relative;
}
}
