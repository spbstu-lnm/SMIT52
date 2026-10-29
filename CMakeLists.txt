cmake_minimum_required(VERSION 3.20.0)
project(SMIT52 VERSION 0.1.0 LANGUAGES C CXX)

# used to generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(libzip CONFIG REQUIRED)

add_executable(SMIT52
    src/main.cpp
    src/config.cpp
    src/config.hpp
    src/backup.cpp
    src/backup.hpp
    src/zip_archive.cpp
    src/zip_archive.hpp)

target_compile_definitions(SMIT52 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SMIT52 PRIVATE Advapi32 Netapi32 libzip::zip)

if(MINGW)
    target_link_options(SMIT52 PRIVATE -municode)
endif()

if(MSVC)
    target_compile_options(SMIT52 PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(SMIT52 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
