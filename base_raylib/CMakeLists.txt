cmake_minimum_required(VERSION 3.15)

add_subdirectory(deps/raylib)

project(raylib_demo C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(.)
include_directories(deps)

add_executable(${PROJECT_NAME}
    src/main.c
)

target_link_libraries(${PROJECT_NAME} raylib)

