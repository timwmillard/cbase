cmake_minimum_required(VERSION 3.15)
project(singleh C)

set(CMAKE_C_STANDARD 11)

# singleh has no external dependencies — it reads a manifest, some C/header
# files, and some data files, and emits one amalgamated header.
add_executable(singleh
    singleh.c
)

# ---------------------------------------------------------------------------
# singleh_generate(MANIFEST <file> OUTPUT <file> [WORKDIR <dir>] [DEPENDS <...>])
#
# Runs the built tool against MANIFEST to produce OUTPUT. WORKDIR sets the
# directory manifest paths are resolved relative to (defaults to
# CMAKE_CURRENT_SOURCE_DIR). Wires DEPENDS/OUTPUT so the parent build
# regenerates only when inputs change. Used by the consuming project.
# ---------------------------------------------------------------------------
function(singleh_generate)
    cmake_parse_arguments(GEN "" "MANIFEST;OUTPUT;WORKDIR" "DEPENDS" ${ARGN})
    if(NOT GEN_WORKDIR)
        set(GEN_WORKDIR ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    add_custom_command(
        OUTPUT ${GEN_OUTPUT}
        COMMAND $<TARGET_FILE:singleh> -manifest ${GEN_MANIFEST}
        DEPENDS singleh ${GEN_MANIFEST} ${GEN_DEPENDS}
        WORKING_DIRECTORY ${GEN_WORKDIR}
        COMMENT "singleh: generating ${GEN_OUTPUT}"
    )
endfunction()
