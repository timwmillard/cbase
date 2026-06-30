cmake_minimum_required(VERSION 3.15)
project(sql2c C)

set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# Resolve the SQLite amalgamation — the tool's only dependency.
#
# Priority:
#   1. SQL2C_SQLITE_C / SQL2C_SQLITE_INCLUDE set by the consumer.
#   2. A vendored copy under ./sqlite/ (for truly standalone use).
# ---------------------------------------------------------------------------
if(NOT SQL2C_SQLITE_C)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sqlite/sqlite3.c)
        set(SQL2C_SQLITE_C ${CMAKE_CURRENT_SOURCE_DIR}/sqlite/sqlite3.c)
        set(SQL2C_SQLITE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/sqlite)
    else()
        message(FATAL_ERROR
            "sql2c: SQLite amalgamation not found. Set SQL2C_SQLITE_C and "
            "SQL2C_SQLITE_INCLUDE, or vendor sqlite3.c/.h under tool/sql2c/sqlite/.")
    endif()
endif()

if(NOT SQL2C_SQLITE_INCLUDE)
    get_filename_component(SQL2C_SQLITE_INCLUDE ${SQL2C_SQLITE_C} DIRECTORY)
endif()

add_executable(sql2c
    sql2c.c
    ${SQL2C_SQLITE_C}
)
target_include_directories(sql2c PRIVATE ${SQL2C_SQLITE_INCLUDE})
# Column metadata APIs (sqlite3_column_table_name / _origin_name) need this.
target_compile_definitions(sql2c PRIVATE SQLITE_ENABLE_COLUMN_METADATA)

# ---------------------------------------------------------------------------
# sql2c_generate(CONFIG <cfg> WORKDIR <dir> OUTPUTS <...> DEPENDS <...>)
#
# Runs the built tool to produce generated C; wires DEPENDS/OUTPUT so the parent
# build regenerates only when inputs change. Used by the consuming project.
# ---------------------------------------------------------------------------
function(sql2c_generate)
    cmake_parse_arguments(GEN "" "CONFIG;WORKDIR" "OUTPUTS;DEPENDS" ${ARGN})
    add_custom_command(
        OUTPUT ${GEN_OUTPUTS}
        COMMAND $<TARGET_FILE:sql2c> -config ${GEN_CONFIG}
        DEPENDS sql2c ${GEN_CONFIG} ${GEN_DEPENDS}
        WORKING_DIRECTORY ${GEN_WORKDIR}
        COMMENT "sql2c: generating ${GEN_OUTPUTS}"
    )
endfunction()
