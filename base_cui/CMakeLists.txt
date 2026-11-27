cmake_minimum_required(VERSION 3.15)

project(app C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CUI build options (https://yappologistic.github.io/CUI/#/guide/getting-started)
# if(APPLE)
#     set(CUI_METAL ON CACHE BOOL "macOS: add the Metal renderer (cui_gpu_*)" FORCE)
# endif()
add_subdirectory(deps/CUI)

add_executable(${PROJECT_NAME}
    src/main.c
    src/deps.c
)
target_include_directories(${PROJECT_NAME} PRIVATE deps)
target_link_libraries(${PROJECT_NAME} PRIVATE cui::cui)
cui_app_target(${PROJECT_NAME})
