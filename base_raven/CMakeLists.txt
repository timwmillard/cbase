cmake_minimum_required(VERSION 3.15)

project(base_demo C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(RAVEN_BACKEND_SOKOL ON CACHE BOOL "" FORCE)
set(RAVEN_BACKEND_VULKAN OFF CACHE BOOL "" FORCE)

add_subdirectory(deps/Raven)

# Build the embedc tool (standalone) and embed gui.xml into gui_xml.h as a
# null-terminated C string (gui_xml_data / gui_xml_len).
add_subdirectory(tool/embedc)

embedc_generate(
    INPUT  ${CMAKE_SOURCE_DIR}/src/gui.xml
    OUTPUT ${CMAKE_SOURCE_DIR}/src/gui_xml.h
    NAME   gui_xml
    TEXT
)
add_custom_target(generate_gui DEPENDS
    ${CMAKE_SOURCE_DIR}/src/gui_xml.h
)

include_directories(.)
include_directories(deps)

add_executable(${PROJECT_NAME}
    src/main.c
    src/deps.c
)

add_dependencies(${PROJECT_NAME}
    generate_gui
)
target_link_libraries(${PROJECT_NAME} raven::raven)

# Embed glass.xml into glass_xml.h (glass_xml_data / glass_xml_len).
embedc_generate(
    INPUT  ${CMAKE_SOURCE_DIR}/glass/glass.xml
    OUTPUT ${CMAKE_SOURCE_DIR}/glass/glass_xml.h
    NAME   glass_xml
    TEXT
)
add_custom_target(generate_glass DEPENDS
    ${CMAKE_SOURCE_DIR}/glass/glass_xml.h
)

add_executable(glass
    glass/main.c
)

add_dependencies(glass
    generate_glass
)
target_link_libraries(glass raven::raven m)

