cmake_minimum_required(VERSION 3.15)

add_subdirectory(deps/ecewo-static)
add_subdirectory(deps/ecewo-fs)
add_subdirectory(deps/ecewo)

project(ecewo_demo C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(.)
include_directories(deps)

add_executable(${PROJECT_NAME}
    src/main.c
    deps/deps.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ecewo::ecewo
    ecewo::static
    ecewo::fs
)

# ecewo::static transitively pulls in ecewo::fs and ecewo::ecewo, which both
# also depend on ecewo::ecewo. The new Apple linker warns about the resulting
# duplicate static archives on the link line; silence the harmless warning.
if(APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

