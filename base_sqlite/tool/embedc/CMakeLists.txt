cmake_minimum_required(VERSION 3.15)
project(embedc C)

set(CMAKE_C_STANDARD 11)

# embedc has no external dependencies — it reads a file and emits C source.
add_executable(embedc
    embedc.c
)

# ---------------------------------------------------------------------------
# embedc_generate(INPUT <file> OUTPUT <file> [NAME <symbol>] [TEXT] [DEPENDS <...>])
#
# Runs the built tool to embed INPUT into OUTPUT. TEXT selects text mode (a
# null-terminated C string); the default is binary mode (a byte array). The
# optional NAME overrides the generated symbol prefix (embedc derives it from
# the input filename otherwise). Wires DEPENDS/OUTPUT so the parent build
# regenerates only when inputs change. Used by the consuming project.
# ---------------------------------------------------------------------------
function(embedc_generate)
    cmake_parse_arguments(GEN "TEXT" "INPUT;OUTPUT;NAME" "DEPENDS" ${ARGN})
    set(GEN_MODE "-b")
    if(GEN_TEXT)
        set(GEN_MODE "-t")
    endif()
    add_custom_command(
        OUTPUT ${GEN_OUTPUT}
        COMMAND $<TARGET_FILE:embedc> ${GEN_MODE} ${GEN_INPUT} ${GEN_OUTPUT} ${GEN_NAME}
        DEPENDS embedc ${GEN_INPUT} ${GEN_DEPENDS}
        COMMENT "embedc: embedding ${GEN_INPUT} -> ${GEN_OUTPUT}"
    )
endfunction()
