cmake_minimum_required(VERSION 3.15)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Silence raylib's macOS OpenGL-deprecation AUTHOR_WARNING (and other dev warnings)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "")

add_subdirectory(deps/raylib)

project(raylib_demo C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(.)
include_directories(deps)

add_executable(${PROJECT_NAME}
    src/main.c
    # deps/deps.c
)

target_link_libraries(${PROJECT_NAME} raylib)

