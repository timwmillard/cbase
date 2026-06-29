cmake_minimum_required(VERSION 3.15)

project(base_demo C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(.)
include_directories(deps)

add_executable(${PROJECT_NAME}
    src/main.c
    src/deps.c
    deps/sqlite3.c
)

target_link_libraries(${PROJECT_NAME})

