cmake_minimum_required(VERSION 3.15)

project(base_demo C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-DTLS_AMALGAMATION)

# cdeps update re-fetches deps/tlse/tlse.c from upstream and can reintroduce
# issues that aren't fixed upstream yet. Reapply our local patch(es) whenever
# the marker they fix is detected, so the fix survives a dependency update.
set(TLSE_C_FILE ${CMAKE_SOURCE_DIR}/deps/tlse/tlse.c)
if(EXISTS ${TLSE_C_FILE})
    file(READ ${TLSE_C_FILE} TLSE_C_CONTENT)
    string(FIND "${TLSE_C_CONTENT}" "#142 fix" TLSE_NEEDS_PATCH)
    if(NOT TLSE_NEEDS_PATCH EQUAL -1)
        find_program(PATCH_EXECUTABLE patch REQUIRED)
        message(STATUS "Applying patches/tlse-fix-asn1-comment.patch to deps/tlse/tlse.c")
        execute_process(
            COMMAND ${PATCH_EXECUTABLE} -p1 -i ${CMAKE_SOURCE_DIR}/patches/tlse-fix-asn1-comment.patch
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE TLSE_PATCH_RESULT
        )
        if(NOT TLSE_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply patches/tlse-fix-asn1-comment.patch")
        endif()
    endif()
    unset(TLSE_C_CONTENT)
endif()

add_subdirectory(deps/tlse)

include_directories(.)
include_directories(deps)

add_executable(${PROJECT_NAME}
    src/main.c
    src/deps.c
)

target_link_libraries(${PROJECT_NAME})

