cmake_minimum_required(VERSION 3.20)
project(sarah LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(sarah
    src/Main.cpp
    src/Util.cpp
    src/Xml.cpp
    src/Condition.cpp
    src/Solution.cpp
    src/Project.cpp
    src/SourceTree.cpp
    src/Platform/PlatformWindows64.cpp
    src/Build/Makefile.cpp
    src/Build/PostBuild.cpp
    src/Setup.cpp
)

target_compile_options(sarah PRIVATE -Wall -Wextra -Wpedantic)
