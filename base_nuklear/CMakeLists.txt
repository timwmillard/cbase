cmake_minimum_required(VERSION 3.15)

project(nuklear_demo)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(.)
include_directories(deps)

# Platform-specific configuration
if(APPLE)
    set(BACKEND -DSOKOL_METAL)
    set(PLATFORM_LIBS "-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework MetalKit")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ObjC")
elseif(WIN32)
    set(BACKEND -DSOKOL_D3D11)
    set(PLATFORM_LIBS gdi32 ole32 d3d11 dxgi)
    add_definitions(-D_WIN32_WINNT=0x0601)
else()
    # Linux
    set(BACKEND -DSOKOL_GLCORE)
    set(PLATFORM_LIBS GL m pthread dl)
    add_definitions(-D_GNU_SOURCE)
endif()

add_definitions(${BACKEND})

add_executable(${PROJECT_NAME}
    src/main.c
    src/deps.c
)

target_link_libraries(${PROJECT_NAME} ${PLATFORM_LIBS})

