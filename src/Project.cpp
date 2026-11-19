#include "Project.hpp"
#include "Condition.hpp"
#include <algorithm>
#include <cctype>
#include <set>
namespace sarah {
namespace {
bool condition_ok(const XmlNode& node, const Variables& variables) {
    const std::string* condition = node.attribute("Condition");
    if (condition == nullptr) {
        return true;
    }
    //neo: if sarah return false (or actually true rn ngl)
    return evaluate_condition(*condition, variables);
}

bool metadata_token(const std::string& value) {
    return value.find("%(") != std::string::npos;
}

void add_unique(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

std::vector<std::string> list_values(const std::string& value, bool keep_metadata = false) {
    std::vector<std::string> result;
    for (const auto& token : split(value, ';')) {
        std::string item = trim(token);
        if (item.empty()) {
            continue;
        }
        if (!keep_metadata && metadata_token(item)) {
            continue;
        }
        result.push_back(item);
    }
    return result;
}

std::map<std::string, std::string> collect_properties(const XmlNode& root, const Variables& variables) { //neo: haha xbox ref
    std::map<std::string, std::string> properties;
    for (const auto& group : root.children_named("PropertyGroup")) {
        if (!condition_ok(group, variables)) {
            continue;
        }
        for (const auto& child : group.children) {
            if (child.name == "Import") {
                continue;
            }
            properties[child.name] = trim(child.text);
        }
    }
    return properties;
}

void resolve_properties(std::map<std::string, std::string>& properties, Variables& variables) {
    for (int pass = 0; pass < 32; ++pass) {
        bool changed = false;
        for (auto& entry : properties) {
            Variables local = variables;
            local.erase(entry.first);
            std::string value = expand_variables(entry.second, local);
            if (value != entry.second) {
                entry.second = value;
                changed = true;
            }
        }
        for (const auto& entry : properties) {
            variables[entry.first] = entry.second;
        }
        if (!changed) {
            break;
        }
    }
}

std::map<std::string, std::string> collect_section(const XmlNode& root,
                                                   const std::string& section,
                                                   const Variables& variables) {
    std::map<std::string, std::string> settings;
    for (const auto& group : root.children_named("ItemDefinitionGroup")) {
        if (!condition_ok(group, variables)) {
            continue;
        }
        for (const auto& node : group.children_named(section)) {
            for (const auto& child : node.children) {
                settings[child.name] = trim(child.text);
            }
        }
    }
    return settings;
}

void resolve_section(std::map<std::string, std::string>& settings, const Variables& variables) {
    for (auto& entry : settings) {
        entry.second = expand_variables(entry.second, variables);
    }
}

std::map<std::string, std::string> collect_metadata(const XmlNode& item, const Variables& variables) {
    std::map<std::string, std::string> metadata;
    for (const auto& child : item.children) {
        if (child.name == "ExcludedFromBuild") {
            continue;
        }
        if (!condition_ok(child, variables)) {
            continue;
        }
        metadata[child.name] = trim(child.text);
    }
    return metadata;
}

bool item_excluded(const XmlNode& item, const Variables& variables) {
    bool excluded = false;
    for (const auto& child : item.children_named("ExcludedFromBuild")) {
        if (!condition_ok(child, variables)) {
            continue;
        }
        excluded = to_lower(trim(child.text)) == "true";
    }
    return excluded;
}

fs::path resolve_existing_path(const fs::path& base, const std::string& value, const Variables& variables) {
    std::string expanded = trim(expand_variables(value, variables));
    if (expanded.empty() || expanded.find("$(") != std::string::npos) {
        return {};
    }
    fs::path path = resolve_path(base, expanded);
    fs::path found = find_case_insensitive(path);
    if (!found.empty()) {
        return found;
    }
    return path;
}

fs::path existing_regular_file(const fs::path& path) {
    fs::path found = find_case_insensitive(path);
    std::error_code error;
    if (!found.empty() && fs::is_regular_file(found, error)) {
        return found;
    }
    return {};
}

fs::path file_argument(const std::string& command) {
    std::size_t flag = command.find("-File");
    if (flag == std::string::npos) {
        return {};
    }
    std::size_t start = flag + 5;
    while (start < command.size() && std::isspace(static_cast<unsigned char>(command[start]))) {
        ++start;
    }
    if (start >= command.size()) {
        return {};
    }
    char quote = command[start];
    if (quote == '"' || quote == '\'') {
        std::size_t end = command.find(quote, start + 1);
        if (end == std::string::npos) {
            return {};
        }
        return normalize_slashes(command.substr(start + 1, end - start - 1));
    }
    std::size_t end = start;
    while (end < command.size() && !std::isspace(static_cast<unsigned char>(command[end]))) {
        ++end;
    }
    return normalize_slashes(command.substr(start, end - start));
}

fs::path collect_post_build_script(const XmlNode& root, //neo: i'd have saved myself if i just added powershell itself but NAH i gotta write a shitty powershell parser because im sarah
                                   const Variables& variables,
                                   const fs::path& project_dir,
                                   const std::string& platform) {
    for (const auto& group : root.children_named("ItemDefinitionGroup")) {
        if (!condition_ok(group, variables)) {
            continue;
        }
        for (const auto& event : group.children_named("PostBuildEvent")) {
            const XmlNode* command = event.first_child("Command");
            if (command == nullptr) {
                continue;
            }
            std::string expanded = expand_variables(trim(command->text), variables);
            fs::path argument = file_argument(expanded);
            if (!argument.empty()) {
                fs::path script = existing_regular_file(resolve_path(project_dir, argument.string()));
                if (!script.empty()) {
                    return script;
                }
            }
        }
    }
    fs::path script = existing_regular_file(project_dir / "Common" / "Buildsteps" /
                                            (platform + "-postbuild.ps1"));
    if (!script.empty()) {
        return script;
    }
    return existing_regular_file(project_dir / "postbuild.ps1");
}

std::string setting(const std::map<std::string, std::string>& settings, const std::string& name) {
    auto found = settings.find(name);
    if (found == settings.end()) {
        return {};
    }
    return trim(found->second);
}

bool flag_enabled(const std::string& value) {
    std::string lowered = to_lower(trim(value));
    return lowered == "true" || lowered == "1";
}

std::vector<std::string> compile_flags(const std::map<std::string, std::string>& settings) {
    std::vector<std::string> flags;
    std::string optimization = setting(settings, "Optimization");
    if (optimization == "Disabled") {
        flags.push_back("/Od");
    } else if (optimization == "Full" || optimization == "MaxSpeed") {
        flags.push_back("/O2");
    } else if (optimization == "MinSpace") {
        flags.push_back("/O1");
    }

    std::string runtime = setting(settings, "RuntimeLibrary");
    if (runtime == "MultiThreaded") {
        flags.push_back("/MT");
    } else if (runtime == "MultiThreadedDebug") {
        flags.push_back("/MTd");
    } else if (runtime == "MultiThreadedDLL") {
        flags.push_back("/MD");
    } else if (runtime == "MultiThreadedDebugDLL") {
        flags.push_back("/MDd");
    }

    std::string warning = setting(settings, "WarningLevel");
    if (warning == "TurnOffAllWarnings") {
        flags.push_back("/W0");
    } else if (warning == "Level1") {
        flags.push_back("/W1");
    } else if (warning == "Level2") {
        flags.push_back("/W2");
    } else if (warning == "Level3") {
        flags.push_back("/W3");
    } else if (warning == "Level4") {
        flags.push_back("/W4");
    }

    std::string exceptions = setting(settings, "ExceptionHandling");
    if (exceptions == "Sync") {
        flags.push_back("/EHsc");
    } else if (exceptions == "Async") {
        flags.push_back("/EHa");
    }

    std::string rtti = setting(settings, "RuntimeTypeInfo");
    if (!rtti.empty()) {
        flags.push_back(flag_enabled(rtti) ? "/GR" : "/GR-");
    }

    std::string security = setting(settings, "BufferSecurityCheck");
    if (!security.empty()) {
        flags.push_back(flag_enabled(security) ? "/GS" : "/GS-");
    }

    std::string function_linking = setting(settings, "FunctionLevelLinking");
    if (!function_linking.empty()) {
        flags.push_back(flag_enabled(function_linking) ? "/Gy" : "/Gy-");
    }

    std::string string_pooling = setting(settings, "StringPooling");
    if (!string_pooling.empty()) {
        flags.push_back(flag_enabled(string_pooling) ? "/GF" : "/GF-");
    }

    std::string intrinsics = setting(settings, "IntrinsicFunctions");
    if (!intrinsics.empty()) {
        flags.push_back(flag_enabled(intrinsics) ? "/Oi" : "/Oi-");
    }

    std::string favor = setting(settings, "FavorSizeOrSpeed");
    if (favor == "Speed") {
        flags.push_back("/Ot");
    } else if (favor == "Size") {
        flags.push_back("/Os");
    }

    std::string debug_format = setting(settings, "DebugInformationFormat");
    if (!debug_format.empty() && debug_format != "None") {
        flags.push_back("/Z7");
    }

    std::string compile_as = setting(settings, "CompileAs");
    if (compile_as == "CompileAsCpp") {
        flags.push_back("/TP");
    } else if (compile_as == "CompileAsC") {
        flags.push_back("/TC");
    }

    std::string language = setting(settings, "LanguageStandard");
    if (language == "stdcpp14") {
        flags.push_back("/std:c++14");
    } else if (language == "stdcpp17") {
        flags.push_back("/std:c++17");
    } else if (language == "stdcpp20") {
        flags.push_back("/std:c++20");
    } else if (language == "stdcpplatest") {
        flags.push_back("/std:c++latest");
    }

    for (const auto& warning_number : list_values(setting(settings, "DisableSpecificWarnings"))) {
        flags.push_back("/wd" + warning_number);
    }

    std::string warnings_as_errors = setting(settings, "TreatWarningAsError");
    if (!warnings_as_errors.empty()) {
        flags.push_back(flag_enabled(warnings_as_errors) ? "/WX" : "/WX-");
    }

    std::string runtime_checks = setting(settings, "BasicRuntimeChecks");
    if (runtime_checks == "EnableFastChecks") {
        flags.push_back("/RTC1");
    }

    for (const auto& token : split(setting(settings, "AdditionalOptions"), ' ')) {
        std::string option = trim(token);
        if (option.empty() || metadata_token(option)) {
            continue;
        }
        if (starts_with(option, "/") && !starts_with(option, "/F")) {
            flags.push_back(option);
        }
    }

    return flags;
}

std::vector<std::string> link_flags(const std::map<std::string, std::string>& settings) {
    std::vector<std::string> flags;
    std::string subsystem = setting(settings, "SubSystem");
    if (subsystem == "Console") {
        flags.push_back("/SUBSYSTEM:CONSOLE");
    } else if (subsystem == "Windows") {
        flags.push_back("/SUBSYSTEM:WINDOWS");
    }

    std::string debug = setting(settings, "GenerateDebugInformation");
    if (!debug.empty() && flag_enabled(debug)) {
        flags.push_back("/DEBUG");
    }

    std::string optimize_references = setting(settings, "OptimizeReferences");
    if (!optimize_references.empty()) {
        flags.push_back(flag_enabled(optimize_references) ? "/OPT:REF" : "/OPT:NOREF");
    }

    std::string comdat = setting(settings, "EnableCOMDATFolding");
    if (!comdat.empty()) {
        flags.push_back(flag_enabled(comdat) ? "/OPT:ICF" : "/OPT:NOICF");
    }

    std::string checksum = setting(settings, "SetChecksum");
    if (!checksum.empty() && flag_enabled(checksum)) {
        flags.push_back("/RELEASE");
    }

    for (const auto& library : list_values(setting(settings, "IgnoreSpecificDefaultLibraries"))) {
        flags.push_back("/NODEFAULTLIB:" + library);
    }

    for (const auto& token : split(setting(settings, "AdditionalOptions"), ' ')) {
        std::string option = trim(token);
        if (option.empty() || metadata_token(option)) {
            continue;
        }
        if (starts_with(option, "/")) {
            flags.push_back(option);
        }
    }

    return flags;
}

std::string default_target_ext(const std::string& configuration_type) {
    if (configuration_type == "StaticLibrary") {
        return ".lib";
    }
    if (configuration_type == "DynamicLibrary") {
        return ".dll";
    }
    return ".exe";
}
}

bool ProjectBuild::is_application() const {
    return configuration_type == "Application";
}

bool ProjectBuild::is_static_library() const {
    return configuration_type == "StaticLibrary";
}

Project::Project(const fs::path& path)
    : path_(fs::absolute(path).lexically_normal()),
      directory_(path_.parent_path()),
      name_(path_.stem().string()),
      root_(parse_xml(read_file(path_))) {
    for (const auto& group : root_.children_named("ItemGroup")) {
        for (const auto& configuration : group.children_named("ProjectConfiguration")) {
            const std::string* include = configuration.attribute("Include");
            if (include == nullptr) {
                continue;
            }
            std::vector<std::string> parts = split(*include, '|');
            if (parts.size() == 2) {
                configurations_.emplace_back(parts[0], parts[1]);
            }
        }
    }
}

const fs::path& Project::path() const {
    return path_;
}

const fs::path& Project::directory() const {
    return directory_;
}

const std::string& Project::name() const {
    return name_;
}

const std::vector<std::pair<std::string, std::string>>& Project::configurations() const {
    return configurations_;
}

ProjectBuild Project::evaluate(const std::string& configuration,
                               const std::string& platform,
                               const fs::path& solution_directory) const {
    Variables variables;
    variables["Configuration"] = configuration;
    variables["Platform"] = platform;
    variables["ProjectDir"] = directory_.string() + "/";
    variables["ProjectName"] = name_;
    variables["ProjectFileName"] = path_.filename().string();
    variables["ProjectExt"] = path_.extension().string();
    variables["ProjectPath"] = path_.string();
    variables["SolutionDir"] = solution_directory.string() + "/";
    variables["SolutionName"] = solution_directory.filename().string();
    variables["SolutionFileName"] = solution_directory.filename().string() + ".sln";
    variables["SolutionExt"] = ".sln"; //neo: who would've guessed..
    variables["SolutionPath"] = (solution_directory / (solution_directory.filename().string() + ".sln")).string();
    std::map<std::string, std::string> properties = collect_properties(root_, variables);
    resolve_properties(properties, variables);
    std::map<std::string, std::string> compile = collect_section(root_, "ClCompile", variables);
    std::map<std::string, std::string> link = collect_section(root_, "Link", variables);
    resolve_section(compile, variables);
    resolve_section(link, variables);
    fs::path post_build_script = collect_post_build_script(root_, variables, directory_, platform);
    ProjectBuild build;
    build.name = name_;
    build.project_path = path_;
    build.project_dir = directory_;
    build.configuration = configuration;
    build.platform = platform;
    build.post_build_script = post_build_script;
    build.configuration_type = setting(properties, "ConfigurationType");
    build.target_name = setting(properties, "TargetName");
    if (build.target_name.empty()) {
        build.target_name = name_;
    }
    build.target_ext = setting(properties, "TargetExt");
    if (build.target_ext.empty()) {
        build.target_ext = default_target_ext(build.configuration_type);
    }

    fs::path out_dir;
    std::string out_dir_value = setting(properties, "OutDir");
    if (!out_dir_value.empty()) {
        std::string expanded = expand_variables(out_dir_value, variables);
        if (expanded.find("$(") == std::string::npos) {
            out_dir = resolve_path(directory_, expanded);
        }
    }
    if (out_dir.empty()) {
        out_dir = directory_ / (platform + "_" + configuration);
    }

    std::string output_value = setting(properties, "OutputFile");
    if (!output_value.empty()) {
        std::string expanded = expand_variables(output_value, variables);
        if (expanded.find("$(") == std::string::npos) {
            build.output = resolve_path(directory_, expanded);
        }
    }
    if (build.output.empty()) {
        build.output = out_dir / (build.target_name + build.target_ext);
    }

    build.defines = list_values(setting(compile, "PreprocessorDefinitions"));
    build.include_dirs.push_back(directory_.string());
    for (const auto& include : list_values(setting(properties, "IncludePath"))) {
        fs::path path = resolve_existing_path(directory_, include, variables);
        if (!path.empty()) {
            add_unique(build.include_dirs, path.string());
        }
    }
    for (const auto& include : list_values(setting(compile, "AdditionalIncludeDirectories"))) {
        fs::path path = resolve_existing_path(directory_, include, variables);
        if (!path.empty()) {
            add_unique(build.include_dirs, path.string());
        }
    }
    for (const auto& forced : list_values(setting(compile, "ForcedIncludeFiles"))) {
        fs::path path = resolve_existing_path(directory_, forced, variables);
        if (!path.empty()) {
            add_unique(build.forced_includes, path.string());
        }
    }
    build.compile_flags = compile_flags(compile);
    build.link_flags = link_flags(link);
    for (const auto& directory_value : list_values(setting(link, "AdditionalLibraryDirectories"))) {
        fs::path path = resolve_existing_path(directory_, directory_value, variables);
        if (!path.empty()) {
            add_unique(build.library_dirs, path.string());
        }
    }

    std::vector<std::string> libraries = list_values(setting(link, "AdditionalDependencies"));
    for (const auto& library : libraries) {
        bool has_directory = library.find('/') != std::string::npos ||
                             library.find('\\') != std::string::npos;
        if (has_directory) {
            fs::path path = resolve_existing_path(directory_, library, variables);
            if (!path.empty()) {
                add_unique(build.library_paths, path.string());
            }
        } else {
            add_unique(build.library_paths, library);
        }
    }

    std::set<std::string> seen_sources;
    for (const auto& group : root_.children_named("ItemGroup")) {
        if (!condition_ok(group, variables)) {
            continue;
        }
        for (const auto& item : group.children) {
            if (item.name == "Library") {
                if (item_excluded(item, variables)) {
                    continue;
                }
                const std::string* include = item.attribute("Include");
                if (include == nullptr) {
                    continue;
                }
                std::string library_value = trim(*include);
                bool has_directory = library_value.find('/') != std::string::npos ||
                                     library_value.find('\\') != std::string::npos;
                if (has_directory) {
                    fs::path path = resolve_existing_path(directory_, library_value, variables);
                    if (!path.empty()) {
                        add_unique(build.library_paths, path.string());
                    }
                } else {
                    add_unique(build.library_paths, library_value);
                }
                continue;
            }
            if (item.name != "ClCompile") {
                continue;
            }
            if (!condition_ok(item, variables) || item_excluded(item, variables)) {
                continue;
            }
            const std::string* include = item.attribute("Include");
            if (include == nullptr) {
                continue;
            }
            fs::path path = resolve_existing_path(directory_, *include, variables);
            if (path.empty()) {
                continue;
            }
            std::string key = path.string();
            if (!seen_sources.insert(key).second) {
                continue;
            }

            SourceFile source;
            source.path = path;
            fs::path relative = path.lexically_relative(directory_);
            if (relative.empty() || starts_with(relative.string(), "..")) {
                relative = path.filename();
            }
            source.relative = normalize_slashes(relative.string());
            std::map<std::string, std::string> metadata = collect_metadata(item, variables);
            std::map<std::string, std::string> file_settings = compile;
            for (const auto& entry : metadata) {
                file_settings[entry.first] = expand_variables(entry.second, variables);
            }
            source.defines = list_values(setting(file_settings, "PreprocessorDefinitions"));
            for (const auto& include_value : list_values(setting(file_settings, "AdditionalIncludeDirectories"))) {
                fs::path include_path = resolve_existing_path(directory_, include_value, variables);
                if (!include_path.empty()) {
                    add_unique(source.include_dirs, include_path.string());
                }
            }
            for (const auto& forced : list_values(setting(file_settings, "ForcedIncludeFiles"))) {
                fs::path forced_path = resolve_existing_path(directory_, forced, variables);
                if (!forced_path.empty()) {
                    add_unique(source.forced_includes, forced_path.string());
                }
            }
            source.flags = compile_flags(file_settings);
            build.sources.push_back(std::move(source));
        }
    }

    return build;
}
}
