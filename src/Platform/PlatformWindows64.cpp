#include "Platform.hpp"
#include "../Condition.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
namespace sarah {
namespace {
fs::path environment_path(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return {};
    }
    return fs::path(value);
}

fs::path home_directory() {
    fs::path home = environment_path("HOME");
    if (home.empty()) {
        home = "/home/neo";
    }
    return home;
}

std::string sdk_version(const fs::path& include_root) {
    std::vector<std::string> versions;
    std::error_code error;
    if (fs::is_directory(include_root, error)) {
        for (const auto& entry : fs::directory_iterator(include_root, error)) {
            if (!entry.is_directory()) {
                continue;
            }
            std::string name = entry.path().filename().string();
            if (starts_with(name, "10.")) {
                versions.push_back(name);
            }
        }
    }
    if (versions.empty()) {
        return "10.0.22621";
    }
    std::sort(versions.begin(), versions.end());
    return versions.back();
}

fs::path find_fxc(const fs::path& root) {
    fs::path configured = environment_path("SARAH_FXC");
    if (!configured.empty()) {
        return configured;
    }
    std::vector<fs::path> candidates;
    candidates.push_back(root / "tools" / "fxc.exe");
    candidates.push_back(root.parent_path() / "tools" / "fxc.exe");
    fs::path executable = executable_path();
    if (!executable.empty()) {
        candidates.push_back(executable.parent_path().parent_path() / "tools" / "fxc.exe");
    }
    candidates.push_back(home_directory() / "tools" / "fxc.exe");
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (fs::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

bool is_windows_executable(const fs::path& path) {
    return to_lower(path.extension().string()) == ".exe";
}

std::string wine_command() {
    fs::path configured = environment_path("SARAH_WINE");
    if (!configured.empty()) {
        return configured.string();
    }
    return "wine";
}

struct ShaderJob {
    std::string output;
    std::string entry;
    std::string profile;
    std::string define;
    std::string source;
    std::string symbol;
};

void write_shader_stub(const fs::path& path, const std::string& symbol) {
    std::string content;
    content += "#pragma once\n";
    content += "static const unsigned char " + symbol + "[] = { 0 };\n";
    write_file(path, content);
}

void generate_shaders(const fs::path& root) {
    fs::path renderer = root / "Minecraft.Client" / "Windows64" / "4JLibs" / "4J_Render" / "Windows64";
    fs::path shaders = renderer / "shaders";
    std::error_code error;
    if (!fs::is_directory(shaders, error)) {
        return;
    }

    const std::vector<ShaderJob> jobs = { //neo: even sarah has jobs and you dont
        {"VS_Compressed.h", "main", "vs_4_0", "COMPRESSED", "main_VS.hlsl", "g_main_VS_Compressed"},
        {"VS_PF3_TF2_CB4_NB4_XW1.h", "main", "vs_4_0", "", "main_VS.hlsl", "g_main_VS_PF3_TF2_CB4_NB4_XW1"},
        {"VS_PF3_TF2_CB4_NB4_XW1_LIGHTING.h", "main", "vs_4_0", "LIGHTING", "main_VS.hlsl", "g_main_VS_PF3_TF2_CB4_NB4_XW1_LIGHTING"},
        {"VS_PF3_TF2_CB4_NB4_XW1_TEXGEN.h", "main", "vs_4_0", "TEXGEN", "main_VS.hlsl", "g_main_VS_PF3_TF2_CB4_NB4_XW1_TEXGEN"},
        {"VS_ScreenSpace.h", "VS_ScreenSpace", "vs_4_0", "", "screen_VS.hlsl", "g_main_VS_ScreenSpace"},
        {"VS_ScreenClear.h", "VS_ScreenClear", "vs_4_0", "", "screen_VS.hlsl", "g_main_VS_ScreenClear"},
        {"PS_Standard.h", "main", "ps_4_0", "", "main_PS.hlsl", "g_main_PS_Standard"},
        {"PS_TextureProjection.h", "main", "ps_4_0", "TEXTURE_PROJECTION", "main_PS.hlsl", "g_main_PS_TextureProjection"},
        {"PS_ForceLOD.h", "main", "ps_4_0", "FORCE_LOD", "main_PS.hlsl", "g_main_PS_ForceLOD"},
        {"PS_ScreenSpace.h", "PS_ScreenSpace", "ps_4_0", "", "screen_PS.hlsl", "g_main_PS_ScreenSpace"},
        {"PS_ScreenClear.h", "PS_ScreenClear", "ps_4_0", "", "screen_PS.hlsl", "g_main_PS_ScreenClear"},
        {"PS_ScreenGamma.h", "PS_ScreenGamma", "ps_4_0", "", "screen_PS.hlsl", "g_main_PS_ScreenGamma"},
    };

    fs::path fxc = find_fxc(root);
    for (const auto& job : jobs) {
        fs::path output = renderer / job.output;
        if (fs::exists(output, error)) {
            continue;
        }
        bool generated = false;
        if (!fxc.empty()) {
            std::vector<std::string> arguments;
            if (is_windows_executable(fxc)) {
                arguments.push_back(wine_command());
            }
            arguments.push_back(fxc.string());
            arguments.push_back("/O3");
            arguments.push_back("/nologo");
            arguments.push_back("/T");
            arguments.push_back(job.profile);
            arguments.push_back("/E");
            arguments.push_back(job.entry);
            if (!job.define.empty()) {
                arguments.push_back("/D");
                arguments.push_back(job.define);
            }
            arguments.push_back("/Fh");
            arguments.push_back("../" + job.output);
            arguments.push_back("/Vn");
            arguments.push_back(job.symbol);
            arguments.push_back(job.source);
            int status = run(arguments, shaders);
            generated = status == 0 && fs::exists(output, error);
        }
        if (!generated) {
            write_shader_stub(output, job.symbol);
        }
    }
}

void copy_4jlibs_headers(const fs::path& root) {
    fs::path base = root / "Minecraft.Client" / "Windows64" / "4JLibs";
    fs::path source = base / "Headers" / "Windows64";
    fs::path destination = base / "inc";
    std::error_code error;
    if (!fs::is_directory(source, error)) {
        return;
    }
    fs::create_directories(destination, error);
    for (const auto& entry : fs::recursive_directory_iterator(source, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        fs::path relative = entry.path().lexically_relative(source);
        fs::path target = destination / relative;
        fs::create_directories(target.parent_path(), error);
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, error);
    }
    fs::create_directories(base / "libs", error);
}

class Windows64Platform final : public Platform {
public:
    std::string name() const override {
        return "Windows64";
    }

    std::string project_platform() const override {
        return "x64";
    }

    std::vector<std::string> configurations(const Solution& solution) const override {
        return solution_configurations_for_platform(solution, name());
    }

    Toolchain toolchain(const fs::path& root) const override {
        (void)root;
        Toolchain toolchain;
        toolchain.compiler = "clang-cl";
        fs::path compiler = environment_path("SARAH_CLANG_CL");
        if (!compiler.empty()) {
            toolchain.compiler = compiler.string();
        }
        toolchain.archiver = "llvm-lib";
        fs::path archiver = environment_path("SARAH_LLVM_LIB");
        if (!archiver.empty()) {
            toolchain.archiver = archiver.string();
        }

        fs::path xwin = environment_path("SARAH_XWIN");
        if (xwin.empty()) {
            xwin = home_directory() / ".xwin";
        }
        std::string version = sdk_version(xwin / "sdk" / "include");
        toolchain.include_dirs.push_back((xwin / "crt" / "include").string());
        toolchain.include_dirs.push_back((xwin / "sdk" / "include" / version / "ucrt").string());
        toolchain.include_dirs.push_back((xwin / "sdk" / "include" / version / "shared").string());
        toolchain.include_dirs.push_back((xwin / "sdk" / "include" / version / "um").string());
        toolchain.include_dirs.push_back((xwin / "sdk" / "include" / version / "winrt").string());
        toolchain.include_dirs.push_back((xwin / "sdk" / "include" / version / "cppwinrt").string());
        toolchain.library_dirs.push_back((xwin / "crt" / "lib" / "x86_64").string());
        toolchain.library_dirs.push_back((xwin / "sdk" / "lib" / "um" / "x86_64").string());
        toolchain.library_dirs.push_back((xwin / "sdk" / "lib" / "ucrt" / "x86_64").string());
        const std::vector<std::string> suppressions = {
            "-Wno-unused-command-line-argument",
            "-Wno-nonportable-include-path",
            "-Wno-ignored-pragma-intrinsic",
            "-Wno-ignored-attributes",
            "-Wno-unknown-pragmas",
            "-Wno-pragma-pack",
            "-Wno-microsoft-anon-tag",
            "-Wno-microsoft-cast",
            "-Wno-microsoft-enum-forward-reference",
            "-Wno-microsoft-unqualified-friend",
            "-Wno-microsoft-template-shadow",
            "-Wno-incompatible-ms-struct",
            "-Wno-ignored-optimization-argument",
            "-Wno-ignored-pragma-optimize",
            "-Wno-deprecated-declarations",
            "-Wno-parentheses-equality",
            "-Wno-switch",
            "-Wno-tautological-compare",
            "-Wno-undefined-inline",
            "-Wno-return-type",
            "-Wno-unused-value",
            "-Wno-unused-variable",
            "-Wno-unused-function",
            "-Wno-unused-but-set-variable",
            "-Wno-unused-private-field",
            "-Wno-unused-local-typedef",
            "-Wno-unused-lambda-capture",
            "-Wno-missing-braces",
            "-Wno-format",
            "-Wno-writable-strings",
            "-Wno-c++11-narrowing",
            "-Wno-register",
            "-Wno-dynamic-exception-spec",
            "-Wno-extra-semi",
            "-Wno-char-subscripts",
            "-Wno-multichar",
            "-Wno-string-plus-int",
            "-Wno-constant-conversion",
            "-Wno-implicit-const-int-float-conversion",
            "-Wno-float-conversion",
            "-Wno-enum-conversion",
            "-Wno-sign-compare",
            "-Wno-unknown-warning-option",
            "-Wno-unsupported-floating-point-opt",
            "-Wno-unknown-argument",
            "-Wno-ignored-qualifiers",
            "-Wno-c++11-extensions",
            "-Wno-c++14-extensions",
            "-Wno-c++17-extensions",
            "-Wno-c++20-extensions",
            "-Wno-gnu-anonymous-struct",
            "-Wno-nested-anon-types",
            "-Wno-keyword-macro",
            "-Wno-undefined-var-template",
            "-Wno-microsoft-include",
            "-Wno-msvc-not-found",
            "-Wno-unused-const-variable",
            "-Wno-unused-member-function",
            "-Wno-unused-template",
            "-Wno-unused-label",
        };

        toolchain.compile_flags.push_back("--target=x86_64-pc-windows-msvc");
        toolchain.compile_flags.push_back("-fms-extensions");
        toolchain.compile_flags.push_back("-fdelayed-template-parsing");
        toolchain.compile_flags.push_back("/nologo");
        toolchain.compile_flags.push_back("/utf-8");
        toolchain.compile_flags.push_back("/bigobj");
        toolchain.compile_flags.push_back("/WX-");
        toolchain.compile_flags.push_back("/EHsc");
        toolchain.compile_flags.push_back("/std:c++14");
        toolchain.compile_flags.push_back("/D_CRT_NONSTDC_NO_WARNINGS");
        for (const auto& suppression : suppressions) {
            toolchain.compile_flags.push_back(suppression);
        }

        toolchain.link_flags.push_back("/NOLOGO");
        toolchain.link_flags.push_back("/INCREMENTAL:NO");
        toolchain.link_flags.push_back("/FORCE:MULTIPLE");
        toolchain.link_flags.push_back("/IGNORE:4099");
        toolchain.link_flags.push_back("/MANIFEST:NO");
        toolchain.link_flags.push_back("/MACHINE:X64");
        const std::vector<std::string> system_libraries = {
            "kernel32", "user32", "gdi32", "winspool", "comdlg32", "advapi32",
            "shell32", "ole32", "oleaut32", "uuid", "odbc32", "odbccp32"
        };
        for (const auto& library : system_libraries) {
            fs::path found = find_case_insensitive(xwin / "sdk" / "lib" / "um" / "x86_64" /
                                                   (library + ".lib"));
            if (!found.empty()) {
                toolchain.link_flags.push_back(found.string());
            }
        }

        return toolchain;
    }

    std::vector<fs::path> extra_solutions(const fs::path& root) const override {
        std::vector<fs::path> result;
        fs::path solution = root / "Minecraft.Client" / "Windows64" / "4JLibs" / "4JLibs.sln";
        std::error_code error;
        if (fs::exists(solution, error)) {
            result.push_back(solution);
        }
        return result;
    }

    void prepare(const fs::path& root, const std::vector<std::string>& configurations) const override {
        (void)configurations;
        copy_4jlibs_headers(root);
        generate_shaders(root);
    }

    std::string link_subsystem(const ProjectBuild& build) const override {
        if (!build.is_application()) {
            return {};
        }
        for (const auto& flag : build.link_flags) {
            if (starts_with(flag, "/SUBSYSTEM:")) {
                return {};
            }
        }
        return "/SUBSYSTEM:WINDOWS";
    }
};

}

std::unique_ptr<Platform> make_platform(const std::string& name) {
    if (name == "Windows64") {
        return std::make_unique<Windows64Platform>();
    }
    return {};
}
}
