cmake_minimum_required(VERSION 3.20.0)
project(SMIT52 VERSION 0.1.0 LANGUAGES C CXX)

# used to generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SMIT52 src/main.cpp
    src/main.hpp)
