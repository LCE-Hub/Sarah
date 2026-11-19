#pragma once
#include "../Util.hpp"
#include <string>
#include <vector>
namespace sarah {
std::vector<std::string> generate_post_build_commands(const fs::path& script,
                                                      const fs::path& project_dir,
                                                      const fs::path& output_dir);
}
