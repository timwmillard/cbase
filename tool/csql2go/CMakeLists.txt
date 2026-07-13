cmake_minimum_required(VERSION 3.15)
project(csql2go C)

set(CMAKE_C_STANDARD 11)

add_executable(csql2go csql2go.c)

# ---------------------------------------------------------------------------
# csql2go_generate(CONFIG <cfg> WORKDIR <dir> OUTPUTS <...> DEPENDS <...>)
#
# Runs the built tool to turn a sql2c manifest.json into a cgo-based Go
# package source. Used by the consuming project.
#
# Unlike csql2lua_module(), there is no equivalent "build the result" helper
# here: the generated .go file builds with plain `go build`/`go test` via
# its own inline "#cgo CFLAGS"/"#cgo LDFLAGS" pragmas. It does expect
# queries.h and queries.c (sql2c's own output) to be present in the same
# directory as the generated .go file — cgo auto-compiles any .c file it
# finds there — so consumers typically place or symlink them alongside it.
# ---------------------------------------------------------------------------
function(csql2go_generate)
    cmake_parse_arguments(GEN "" "CONFIG;WORKDIR" "OUTPUTS;DEPENDS" ${ARGN})
    add_custom_command(
        OUTPUT ${GEN_OUTPUTS}
        COMMAND $<TARGET_FILE:csql2go> -config ${GEN_CONFIG}
        DEPENDS csql2go ${GEN_CONFIG} ${GEN_DEPENDS}
        WORKING_DIRECTORY ${GEN_WORKDIR}
        COMMENT "csql2go: generating ${GEN_OUTPUTS}"
    )
endfunction()
