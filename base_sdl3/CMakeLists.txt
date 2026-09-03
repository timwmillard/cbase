cmake_minimum_required(VERSION 3.15)

project(base_sdl3 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

add_subdirectory(deps/SDL)

include_directories(.)
include_directories(deps)

add_executable(${PROJECT_NAME}
    src/main.c
    src/deps.c
)

target_link_libraries(${PROJECT_NAME} SDL3::SDL3-static)

