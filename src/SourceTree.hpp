#pragma once
#include "Util.hpp"
#include <vector>
namespace sarah {
struct SourceMirror {
    fs::path root;
    fs::path directory;
};

SourceMirror create_source_mirror(const fs::path& root,
                                  const fs::path& directory,
                                  const std::vector<fs::path>& exclude,
                                  const std::vector<fs::path>& include_dirs);
fs::path mirror_path(const SourceMirror& mirror, const fs::path& path);

}
