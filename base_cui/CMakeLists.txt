cmake_minimum_required(VERSION 3.15)

project(app C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(.)
include_directories(deps)

# Platform-specific configuration
if(APPLE)
    set(PLATFORM_LIBS "-framework Cocoa" "-framework QuartzCore" "-framework OpenGL")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ObjC")
elseif(WIN32)
else()
endif()

add_executable(${PROJECT_NAME}
    src/main.c
    src/deps.c
)

target_link_libraries(${PROJECT_NAME} ${PLATFORM_LIBS})

