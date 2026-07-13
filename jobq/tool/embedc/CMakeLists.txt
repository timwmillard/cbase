cmake_minimum_required(VERSION 3.15)
project(embedc C)

set(CMAKE_C_STANDARD 11)

# embedc has no external dependencies — it reads a file and emits C source.
add_executable(embedc
    embedc.c
)

# ---------------------------------------------------------------------------
# embedc_generate(INPUT <file> OUTPUT <file> [NAME <symbol>] [TEXT] [DEPENDS <...>])
# embedc_generate(INPUTS <file>... OUTPUT <file> [TEXT] [DEPENDS <...>])
#
# Runs the built tool to embed INPUT, or every file in INPUTS, into OUTPUT.
# TEXT selects text mode (a null-terminated C string); the default is binary
# mode (a byte array). NAME is supported for a single INPUT; symbols for INPUTS
# are derived from their filenames. Wires DEPENDS/OUTPUT so the parent build
# regenerates only when inputs change. Used by the consuming project.
# ---------------------------------------------------------------------------
function(embedc_generate)
    cmake_parse_arguments(GEN "TEXT" "INPUT;OUTPUT;NAME" "INPUTS;DEPENDS" ${ARGN})
    set(GEN_MODE "-b")
    if(GEN_TEXT)
        set(GEN_MODE "-t")
    endif()

    if(NOT GEN_OUTPUT)
        message(FATAL_ERROR "embedc_generate requires OUTPUT")
    endif()
    if(GEN_INPUT AND GEN_INPUTS)
        message(FATAL_ERROR "embedc_generate accepts either INPUT or INPUTS, not both")
    elseif(GEN_INPUTS)
        if(GEN_NAME)
            message(FATAL_ERROR "embedc_generate NAME is only valid with a single INPUT")
        endif()
        set(GEN_INPUT_FILES ${GEN_INPUTS})
        set(GEN_COMMAND ${GEN_MODE} -o ${GEN_OUTPUT} ${GEN_INPUTS})
    elseif(GEN_INPUT)
        set(GEN_INPUT_FILES ${GEN_INPUT})
        set(GEN_COMMAND ${GEN_MODE} ${GEN_INPUT} ${GEN_OUTPUT} ${GEN_NAME})
    else()
        message(FATAL_ERROR "embedc_generate requires INPUT or INPUTS")
    endif()

    add_custom_command(
        OUTPUT ${GEN_OUTPUT}
        COMMAND $<TARGET_FILE:embedc> ${GEN_COMMAND}
        DEPENDS embedc ${GEN_INPUT_FILES} ${GEN_DEPENDS}
        COMMENT "embedc: embedding ${GEN_INPUT_FILES} -> ${GEN_OUTPUT}"
    )
endfunction()
