#include "Makefile.hpp"
#include <map>
#include <set>
#include <sstream>
namespace sarah {
namespace {
std::string join_quoted(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) {
            result += " ";
        }
        result += shell_quote(value);
    }
    return result;
}

std::string resolve_library(const std::vector<std::string>& directories,
                            const std::string& library) {
    fs::path path(library);
    if (path.has_parent_path()) {
        return library;
    }
    for (const auto& directory : directories) {
        fs::path found = find_case_insensitive(fs::path(directory) / library);
        std::error_code error;
        if (!found.empty() && fs::is_regular_file(found, error)) {
            return found.string();
        }
    }
    return library;
}

std::string object_path(const MakefileTarget& target, const SourceFile& source) {
    fs::path relative = normalize_slashes(source.relative);
    fs::path path = target.object_directory / relative;
    path += ".obj";
    return path.string();
}

std::string compile_command(const MakefileConfig& config,
                            const MakefileTarget& target,
                            const SourceFile& source) {
    std::vector<std::string> arguments;
    arguments.push_back(config.toolchain.compiler);
    arguments.insert(arguments.end(),
                     config.toolchain.compile_flags.begin(),
                     config.toolchain.compile_flags.end());
    arguments.insert(arguments.end(),
                     target.build.compile_flags.begin(),
                     target.build.compile_flags.end());
    arguments.insert(arguments.end(), source.flags.begin(), source.flags.end());

    for (const auto& include : config.toolchain.include_dirs) {
        arguments.push_back("-I" + include);
    }
    for (const auto& include : target.build.include_dirs) {
        arguments.push_back("-I" + include);
    }
    for (const auto& include : source.include_dirs) {
        arguments.push_back("-I" + include);
    }

    std::vector<std::string> defines = target.build.defines;
    defines.insert(defines.end(), source.defines.begin(), source.defines.end());
    for (const auto& define : defines) {
        arguments.push_back("/D" + define);
    }

    std::vector<std::string> forced = target.build.forced_includes;
    forced.insert(forced.end(), source.forced_includes.begin(), source.forced_includes.end());
    for (const auto& include : forced) {
        arguments.push_back("/FI" + include);
    }

    arguments.push_back("/WX-");
    arguments.push_back("/c");
    arguments.push_back(source.path.string());
    arguments.push_back("-o");
    arguments.push_back(object_path(target, source));
    return join_quoted(arguments);
}

std::string link_command(const MakefileConfig& config,
                         const MakefileTarget& target,
                         const std::vector<std::string>& objects) {
    std::vector<std::string> arguments;
    arguments.push_back(config.toolchain.compiler);
    arguments.push_back("--target=x86_64-pc-windows-msvc");
    arguments.push_back("-fuse-ld=lld");
    arguments.push_back("-Wno-unused-command-line-argument");
    for (const auto& object : objects) {
        arguments.push_back(object);
    }
    arguments.push_back("-o");
    arguments.push_back(target.build.output.string());
    arguments.push_back("/link");
    arguments.insert(arguments.end(),
                     config.toolchain.link_flags.begin(),
                     config.toolchain.link_flags.end());
    arguments.insert(arguments.end(),
                     target.build.link_flags.begin(),
                     target.build.link_flags.end());
    arguments.insert(arguments.end(),
                     target.extra_link_flags.begin(),
                     target.extra_link_flags.end());
    for (const auto& directory : config.toolchain.library_dirs) {
        arguments.push_back("/libpath:" + directory);
    }
    for (const auto& directory : target.build.library_dirs) {
        arguments.push_back("/libpath:" + directory);
    }
    std::vector<std::string> library_dirs = config.toolchain.library_dirs;
    library_dirs.insert(library_dirs.end(),
                        target.build.library_dirs.begin(),
                        target.build.library_dirs.end());
    for (const auto& library : target.build.library_paths) {
        arguments.push_back(resolve_library(library_dirs, library));
    }
    return join_quoted(arguments);
}

std::string library_command(const MakefileConfig& config,
                            const MakefileTarget& target,
                            const std::vector<std::string>& objects) {
    std::vector<std::string> arguments;
    arguments.push_back(config.toolchain.archiver);
    arguments.push_back("/OUT:" + target.build.output.string());
    for (const auto& object : objects) {
        arguments.push_back(object);
    }
    return join_quoted(arguments);
}
}

void write_configuration_makefile(const fs::path& path, const MakefileConfig& config) {
    std::string application_output;
    for (const auto& target : config.targets) {
        if (target.build.is_application()) {
            application_output = target.build.output.string();
            break;
        }
    }

    std::ostringstream output;
    output << "SHELL := /bin/bash\n";
    output << "WINE ?= wine\n\n";
    output << ".PHONY: all clean run\n\n";
    output << "all:";
    for (const auto& target : config.targets) {
        output << " " << target.build.output.string();
    }
    output << "\n\n";
    output << "clean:\n";
    output << "\trm -rf " << shell_quote((config.build_directory / "obj" / config.platform / config.configuration).string()) << "\n\n";
    std::map<std::string, const MakefileTarget*> outputs;
    for (const auto& target : config.targets) {
        outputs[target.build.output.string()] = &target;
    }

    for (const auto& target : config.targets) {
        std::vector<std::string> objects;
        for (const auto& source : target.build.sources) {
            objects.push_back(object_path(target, source));
        }

        std::set<std::string> prerequisites;
        for (const auto& library : target.build.library_paths) {
            if (outputs.find(library) != outputs.end()) {
                prerequisites.insert(library);
            }
        }

        output << target.build.output.string() << ":";
        for (const auto& object : objects) {
            output << " " << object;
        }
        for (const auto& prerequisite : prerequisites) {
            output << " " << prerequisite;
        }
        output << "\n";
        output << "\t@mkdir -p \"$(dir $@)\"\n";
        if (target.build.is_application()) {
            output << "\t" << link_command(config, target, objects) << "\n";
            for (const auto& command : target.post_build_commands) {
                output << "\t" << command << "\n";
            }
            output << "\n";
        } else {
            output << "\t" << library_command(config, target, objects) << "\n\n";
        }

        for (const auto& source : target.build.sources) {
            std::string object = object_path(target, source);
            output << object << ": " << source.path.string() << "\n";
            output << "\t@mkdir -p \"$(dir $@)\"\n";
            output << "\t" << compile_command(config, target, source) << "\n\n";
        }
    }

    if (application_output.empty()) {
        output << "run:\n\t@echo \"no application target\"\n\n";
    } else {
        output << "run: " << application_output << "\n";
        output << "\t@cd \"$(dir $<)\" && $(WINE) './$(notdir $<)'\n\n";
    }

    write_file(path, output.str());
}

void write_top_level_makefile(const fs::path& path, const MakefileConfig& config) {
    std::ostringstream output;
    output << "CONFIG ?= " << config.configuration << "\n";
    output << "PLATFORM ?= " << config.platform << "\n";
    output << "WINE ?= wine\n";
    output << "SARAH ?= " << config.sarah.string() << "\n";
    output << "ROOT := " << config.root.string() << "\n";
    output << "SOLUTION := " << config.solution.string() << "\n";
    output << "BUILD := " << config.build_directory.string() << "\n\n";
    output << ".PHONY: all clean run " << config.platform << "\n\n";
    output << "all: " << config.platform << "\n\n";
    output << config.platform << ": $(BUILD)/.stamp\n";
    output << "\t@test -f \"$(BUILD)/" << config.platform << "/$(CONFIG).mk\" || (echo \"unknown configuration: $(CONFIG)\"; exit 1)\n";
    output << "\t@$(MAKE) -f \"$(BUILD)/" << config.platform << "/$(CONFIG).mk\"\n\n";
    output << "run: $(BUILD)/.stamp\n";
    output << "\t@test -f \"$(BUILD)/" << config.platform << "/$(CONFIG).mk\" || (echo \"unknown configuration: $(CONFIG)\"; exit 1)\n";
    output << "\t@$(MAKE) -f \"$(BUILD)/" << config.platform << "/$(CONFIG).mk\" run\n\n";
    output << "$(BUILD)/.stamp:";
    for (const auto& dependency : config.dependencies) {
        output << " " << dependency.string();
    }
    output << "\n";
    output << "\t@$(SARAH) setup build --root \"$(ROOT)\" --solution \"$(SOLUTION)\"\n\n";
    output << "clean:\n";
    output << "\t@rm -f \"$(BUILD)/.stamp\"\n";
    output << "\t@rm -rf \"$(BUILD)/obj\"\n";
    write_file(path, output.str());
}
}
