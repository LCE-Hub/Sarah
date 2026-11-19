#include "Setup.hpp"
#include "Build/Makefile.hpp"
#include "Platform/Platform.hpp"
#include "Build/PostBuild.hpp"
#include "Project.hpp"
#include "Solution.hpp"
#include "SourceTree.hpp"
#include <algorithm>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
namespace sarah {
namespace {
fs::path find_default_solution(const fs::path& root) {
    fs::path preferred = root / "MinecraftConsoles.sln"; //neo: its for LCE what did you expect
    std::error_code error;
    if (fs::exists(preferred, error)) {
        return preferred;
    }
    if (fs::is_directory(root, error)) {
        for (const auto& entry : fs::directory_iterator(root, error)) {
            if (entry.is_regular_file() && to_lower(entry.path().extension().string()) == ".sln") {
                return entry.path();
            }
        }
    }
    return {};
}

fs::path relative_to_root(const fs::path& root, const fs::path& path) {
    fs::path relative = path.lexically_relative(root);
    if (relative.empty() || starts_with(relative.string(), "..")) {
        return path.filename();
    }
    return relative;
}

void copy_solution_files(const fs::path& root,
                         const fs::path& build_directory,
                         const Solution& solution,
                         std::vector<fs::path>& dependencies) {
    std::vector<fs::path> files;
    files.push_back(solution.path());
    for (const auto& project : solution.projects()) {
        files.push_back(project.absolute_path);
    }
    for (const auto& file : files) {
        std::error_code error;
        if (!fs::exists(file, error)) {
            continue;
        }
        fs::path destination = build_directory / relative_to_root(root, file);
        fs::create_directories(destination.parent_path(), error);
        fs::copy_file(file, destination, fs::copy_options::overwrite_existing, error);
        if (std::find(dependencies.begin(), dependencies.end(), file) == dependencies.end()) {
            dependencies.push_back(file);
        }
    }
}

std::string choose_configuration(const std::vector<std::string>& configurations,
                                 const std::string& requested) {
    if (configurations.empty()) {
        return {};
    }
    if (!requested.empty()) {
        if (std::find(configurations.begin(), configurations.end(), requested) == configurations.end()) {
            return {};
        }
        return requested;
    }
    if (std::find(configurations.begin(), configurations.end(), "Release") != configurations.end()) {
        return "Release";
    }
    return configurations.front();
}

MakefileTarget make_target(const ProjectBuild& build,
                           const fs::path& build_directory,
                           const std::string& platform,
                           const std::string& configuration,
                           const Platform& platform_module) {
    MakefileTarget target;
    target.build = build;
    target.object_directory = build_directory / "obj" / platform / configuration /
                              sanitize_name(build.name);
    std::string subsystem = platform_module.link_subsystem(build);
    if (!subsystem.empty()) {
        target.extra_link_flags.push_back(subsystem);
    }
    if (build.is_application()) {
        target.post_build_commands = generate_post_build_commands(build.post_build_script,
                                                                  build.project_dir,
                                                                  build.output.parent_path());
    }
    return target;
}

std::vector<MakefileTarget> main_targets(const Solution& solution,
                                         const std::string& configuration,
                                         const std::string& platform,
                                         const fs::path& build_directory,
                                         const Platform& platform_module) {
    std::vector<MakefileTarget> targets;
    for (const auto& project : solution.projects()) {
        ProjectMapping mapping = solution.mapping(project.guid, configuration, platform);
        if (!mapping.build || mapping.configuration.empty()) {
            continue;
        }
        Project parsed(project.absolute_path);
        ProjectBuild build = parsed.evaluate(mapping.configuration,
                                             mapping.platform,
                                             solution.directory());
        targets.push_back(make_target(build,
                                      build_directory,
                                      platform,
                                      configuration,
                                      platform_module));
    }
    return targets;
}

std::vector<MakefileTarget> dependency_targets(const std::vector<MakefileTarget>& targets,
                                               const std::vector<Solution>& extra_solutions,
                                               const std::string& platform,
                                               const std::string& configuration,
                                               const fs::path& build_directory,
                                               const Platform& platform_module) {
    std::vector<MakefileTarget> result;
    std::set<std::string> seen;
    for (const auto& target : targets) {
        for (const auto& library : target.build.library_paths) {
            fs::path library_path(library);
            std::string stem = library_path.stem().string();
            if (stem.empty()) {
                continue;
            }
            for (const auto& extra : extra_solutions) {
                for (const auto& project : extra.projects()) {
                    std::string suffix;
                    if (stem == project.name) {
                        suffix = "";
                    } else if (starts_with(stem, project.name + "_")) {
                        suffix = stem.substr(project.name.size());
                    } else {
                        continue;
                    }
                    std::string extra_configuration;
                    if (suffix == "_d") {
                        extra_configuration = "Debug";
                    } else if (suffix == "_r") {
                        extra_configuration = "Release";
                    } else {
                        continue;
                    }
                    ProjectMapping mapping = extra.mapping(project.guid,
                                                           extra_configuration,
                                                           platform);
                    if (!mapping.build || mapping.configuration.empty()) {
                        continue;
                    }
                    Project parsed(project.absolute_path);
                    ProjectBuild build = parsed.evaluate(mapping.configuration,
                                                         mapping.platform,
                                                         extra.directory());
                    if (!seen.insert(build.output.string()).second) {
                        continue;
                    }
                    result.push_back(make_target(build,
                                                 build_directory,
                                                 platform,
                                                 configuration,
                                                 platform_module));
                }
            }
        }
    }
    return result;
}

void collect_include_dirs(const std::vector<MakefileTarget>& targets,
                          std::vector<fs::path>& include_dirs) {
    for (const auto& target : targets) {
        for (const auto& directory : target.build.include_dirs) {
            include_dirs.push_back(directory);
        }
        for (const auto& source : target.build.sources) {
            for (const auto& directory : source.include_dirs) {
                include_dirs.push_back(directory);
            }
        }
    }
}

void map_target(const SourceMirror& mirror, MakefileTarget& target) {
    for (auto& directory : target.build.include_dirs) {
        directory = mirror_path(mirror, directory).string();
    }
    for (auto& include : target.build.forced_includes) {
        include = mirror_path(mirror, include).string();
    }
    for (auto& source : target.build.sources) {
        source.path = mirror_path(mirror, source.path);
        for (auto& directory : source.include_dirs) {
            directory = mirror_path(mirror, directory).string();
        }
        for (auto& include : source.forced_includes) {
            include = mirror_path(mirror, include).string();
        }
    }
}
}

int setup_build(const SetupOptions& options) {
    fs::path root = fs::absolute(options.root).lexically_normal();
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        std::cerr << "sarah: root directory doesnt exist: " << root << "\n";
        return 1;
    }

    fs::path solution_path = options.solution;
    if (solution_path.empty()) {
        solution_path = find_default_solution(root);
    }
    if (solution_path.empty()) {
        std::cerr << "sarah: no solution file found in " << root << "\n";
        return 1;
    }
    solution_path = fs::absolute(solution_path).lexically_normal();
    if (!fs::exists(solution_path, error)) {
        std::cerr << "sarah: solution file does not exist: " << solution_path << "\n";
        return 1;
    }

    fs::path build_directory = options.build_directory;
    if (build_directory.empty()) {
        build_directory = root / "build";
    }
    build_directory = fs::absolute(build_directory).lexically_normal();
    Solution solution(solution_path);
    std::unique_ptr<Platform> platform_module = make_platform(options.platform);
    if (!platform_module) {
        std::cerr << "sarah: unknown platform: " << options.platform << "\n";
        return 1;
    }

    std::vector<std::string> configurations = platform_module->configurations(solution);
    if (configurations.empty()) {
        std::cerr << "sarah: solution has no configurations for " << options.platform << "\n";
        return 1;
    }
    std::string default_configuration = choose_configuration(configurations, options.configuration);
    if (default_configuration.empty()) {
        std::cerr << "sarah: unknown configuration: " << options.configuration << "\n";
        return 1;
    }

    fs::create_directories(build_directory, error);
    std::vector<fs::path> dependencies;
    copy_solution_files(root, build_directory, solution, dependencies);
    std::vector<Solution> extra_solutions;
    for (const auto& extra_path : platform_module->extra_solutions(root)) {
        Solution extra(extra_path);
        copy_solution_files(root, build_directory, extra, dependencies);
        extra_solutions.push_back(std::move(extra));
    }

    platform_module->prepare(root, configurations);
    Toolchain toolchain = platform_module->toolchain(root);
    fs::path compat_header = build_directory / "compat" / "sarah_compat.h";
    write_file(compat_header,
               "#pragma once\n"
               "#ifdef __cplusplus\n"
               "#include <xhash>\n"
               "using stdext::hash_value;\n"
               "namespace std { using ::stdext::hash_value; }\n"
               "#endif\n");
    toolchain.compile_flags.push_back("/FI" + compat_header.string());
    fs::path intrinsics_header = build_directory / "compat" / "xmmintrin.h";
    write_file(intrinsics_header, R"HEADER(#pragma once
#include_next <xmmintrin.h>
__attribute__((always_inline)) inline __m128 s_set4(float a, float b, float c, float d) { __m128 r; r.m128_f32[0] = a; r.m128_f32[1] = b; r.m128_f32[2] = c; r.m128_f32[3] = d; return r; }
__attribute__((always_inline)) inline __m128 _mm_add_ps(__m128 a, __m128 b) { return s_set4(a.m128_f32[0] + b.m128_f32[0], a.m128_f32[1] + b.m128_f32[1], a.m128_f32[2] + b.m128_f32[2], a.m128_f32[3] + b.m128_f32[3]); }
__attribute__((always_inline)) inline __m128 _mm_add_ss(__m128 a, __m128 b) { return s_set4(a.m128_f32[0] + b.m128_f32[0], a.m128_f32[1], a.m128_f32[2], a.m128_f32[3]); }
__attribute__((always_inline)) inline __m128 _mm_sub_ps(__m128 a, __m128 b) { return s_set4(a.m128_f32[0] - b.m128_f32[0], a.m128_f32[1] - b.m128_f32[1], a.m128_f32[2] - b.m128_f32[2], a.m128_f32[3] - b.m128_f32[3]); }
__attribute__((always_inline)) inline __m128 _mm_mul_ps(__m128 a, __m128 b) { return s_set4(a.m128_f32[0] * b.m128_f32[0], a.m128_f32[1] * b.m128_f32[1], a.m128_f32[2] * b.m128_f32[2], a.m128_f32[3] * b.m128_f32[3]); }
__attribute__((always_inline)) inline __m128 _mm_div_ps(__m128 a, __m128 b) { return s_set4(a.m128_f32[0] / b.m128_f32[0], a.m128_f32[1] / b.m128_f32[1], a.m128_f32[2] / b.m128_f32[2], a.m128_f32[3] / b.m128_f32[3]); }
__attribute__((always_inline)) inline __m128 _mm_sqrt_ps(__m128 a) { return s_set4(__builtin_sqrtf(a.m128_f32[0]), __builtin_sqrtf(a.m128_f32[1]), __builtin_sqrtf(a.m128_f32[2]), __builtin_sqrtf(a.m128_f32[3])); }
__attribute__((always_inline)) inline __m128 _mm_and_ps(__m128 a, __m128 b) { __m128 r; for (int i = 0; i < 4; ++i) r.m128_u32[i] = a.m128_u32[i] & b.m128_u32[i]; return r; }
__attribute__((always_inline)) inline __m128 _mm_andnot_ps(__m128 a, __m128 b) { __m128 r; for (int i = 0; i < 4; ++i) r.m128_u32[i] = (~a.m128_u32[i]) & b.m128_u32[i]; return r; }
__attribute__((always_inline)) inline __m128 _mm_or_ps(__m128 a, __m128 b) { __m128 r; for (int i = 0; i < 4; ++i) r.m128_u32[i] = a.m128_u32[i] | b.m128_u32[i]; return r; }
__attribute__((always_inline)) inline __m128 _mm_cmpneq_ps(__m128 a, __m128 b) { __m128 r; for (int i = 0; i < 4; ++i) r.m128_u32[i] = (a.m128_f32[i] != b.m128_f32[i]) ? 0xffffffffu : 0u; return r; }
__attribute__((always_inline)) inline __m128 _mm_shuffle_ps(__m128 a, __m128 b, unsigned int imm) { return s_set4(a.m128_f32[imm & 3], a.m128_f32[(imm >> 2) & 3], b.m128_f32[(imm >> 4) & 3], b.m128_f32[(imm >> 6) & 3]); }
__attribute__((always_inline)) inline __m128 _mm_move_ss(__m128 a, __m128 b) { return s_set4(b.m128_f32[0], a.m128_f32[1], a.m128_f32[2], a.m128_f32[3]); }
__attribute__((always_inline)) inline __m128 _mm_loadu_ps(const float* p) { return s_set4(p[0], p[1], p[2], p[3]); }
__attribute__((always_inline)) inline void _mm_storeu_ps(float* p, __m128 a) { p[0] = a.m128_f32[0]; p[1] = a.m128_f32[1]; p[2] = a.m128_f32[2]; p[3] = a.m128_f32[3]; }
__attribute__((always_inline)) inline __m128 _mm_setzero_ps() { return s_set4(0.0f, 0.0f, 0.0f, 0.0f); }
__attribute__((always_inline)) inline __m128 _mm_set_ps(float e3, float e2, float e1, float e0) { return s_set4(e0, e1, e2, e3); }
__attribute__((always_inline)) inline __m128 _mm_set_ps1(float e) { return s_set4(e, e, e, e); }
)HEADER");
    toolchain.include_dirs.insert(toolchain.include_dirs.begin(),
                                  intrinsics_header.parent_path().string());
    fs::path sarah = executable_path();
    if (sarah.empty()) {
        sarah = "sarah";
    }

    std::vector<std::pair<std::string, std::vector<MakefileTarget>>> configuration_targets;
    std::vector<fs::path> include_dirs;
    for (const auto& configuration : configurations) {
        std::vector<MakefileTarget> targets = main_targets(solution,
                                                           configuration,
                                                           options.platform,
                                                           build_directory,
                                                           *platform_module);
        std::vector<MakefileTarget> dependencies_for_configuration = dependency_targets(targets,
                                                                                         extra_solutions,
                                                                                         options.platform,
                                                                                         configuration,
                                                                                         build_directory,
                                                                                         *platform_module);
        dependencies_for_configuration.insert(dependencies_for_configuration.end(),
                                              targets.begin(),
                                              targets.end());
        collect_include_dirs(dependencies_for_configuration, include_dirs);
        configuration_targets.emplace_back(configuration, std::move(dependencies_for_configuration));
    }

    for (const auto& entry : configuration_targets) {
        for (const auto& target : entry.second) {
            const fs::path& script = target.build.post_build_script;
            if (!script.empty() &&
                std::find(dependencies.begin(), dependencies.end(), script) == dependencies.end()) {
                dependencies.push_back(script);
            }
        }
    }

    std::vector<fs::path> exclude;
    exclude.push_back(build_directory);
    exclude.push_back(root / ".git");
    SourceMirror mirror = create_source_mirror(root,
                                               build_directory / "source",
                                               exclude,
                                               include_dirs);

    for (auto& entry : configuration_targets) {
        for (auto& target : entry.second) {
            map_target(mirror, target);
        }

        MakefileConfig config;
        config.build_directory = build_directory;
        config.root = root;
        config.solution = solution_path;
        config.sarah = sarah;
        config.platform = options.platform;
        config.configuration = entry.first;
        config.toolchain = toolchain;
        config.targets = std::move(entry.second);
        config.dependencies = dependencies;
        write_configuration_makefile(build_directory / options.platform / (entry.first + ".mk"),
                                     config);
    }

    MakefileConfig top;
    top.build_directory = build_directory;
    top.root = root;
    top.solution = solution_path;
    top.sarah = sarah;
    top.platform = options.platform;
    top.configuration = default_configuration;
    top.toolchain = toolchain;
    top.dependencies = dependencies;
    write_top_level_makefile(build_directory / "Makefile", top);
    write_file(build_directory / ".stamp", std::to_string(static_cast<long long>(std::time(nullptr))));
    std::cout << "sarah: setup\n";
    std::cout << "-root: " << root << "\n";
    std::cout << "-solution: " << solution_path << "\n";
    std::cout << "-build: " << build_directory << "\n";
    std::cout << "-platform: " << options.platform << "\n";
    std::cout << "-default config: " << default_configuration << "\n";
    std::cout << "-configs:";
    for (const auto& configuration : configurations) {
        std::cout << " " << configuration;
    }
    std::cout << "\n";
    return 0;
}
}
