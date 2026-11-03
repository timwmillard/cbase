cmake_minimum_required(VERSION 3.15)
project(sql2c_basic_example C)

set(CMAKE_C_STANDARD 11)

set(SQL2C_SQLITE_C       ${CMAKE_CURRENT_SOURCE_DIR}/../../sqlite/sqlite3.c)
set(SQL2C_SQLITE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../sqlite)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. sql2c_build)

sql2c_generate(
    CONFIG  ${CMAKE_CURRENT_SOURCE_DIR}/sql/config.txt
    WORKDIR ${CMAKE_CURRENT_SOURCE_DIR}/sql
    OUTPUTS ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.c
            ${CMAKE_CURRENT_SOURCE_DIR}/sql/manifes.json
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql
            ${CMAKE_CURRENT_SOURCE_DIR}/sql/queries.sql
)

add_executable(basic_example
    src/main.c
    src/queries.c
    ${SQL2C_SQLITE_C}
)
target_include_directories(basic_example PRIVATE src ${SQL2C_SQLITE_INCLUDE})
target_compile_definitions(basic_example PRIVATE
    SQLITE_ENABLE_COLUMN_METADATA
    SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql"
)

# ---------------------------------------------------------------------------
# Optional: csql2lua demo — builds basicdb.so, a loadable Lua C module,
# from the manifest.json sql2c emitted above. Skipped if Lua isn't found.
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    foreach(_example_lua_pc lua5.4 lua5.3 lua-5.4 lua-5.3 lua)
        if(NOT EXAMPLE_LUA_FOUND)
            pkg_check_modules(EXAMPLE_LUA QUIET ${_example_lua_pc})
        endif()
    endforeach()
endif()

if(EXAMPLE_LUA_FOUND)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../csql2lua csql2lua_build)

    csql2lua_generate(
        CONFIG  ${CMAKE_CURRENT_SOURCE_DIR}/lua/config.txt
        WORKDIR ${CMAKE_CURRENT_SOURCE_DIR}/lua
        OUTPUTS ${CMAKE_CURRENT_SOURCE_DIR}/lua/basicdb.lua.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sql/manifes.json
                ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.h
    )

    # Lands at <build-dir>/basicdb.so — see lua/test.lua for how to point
    # Lua's package.cpath at it.
    csql2lua_module(basicdb_lua
        MODULE        basicdb
        SOURCE        ${CMAKE_CURRENT_SOURCE_DIR}/lua/basicdb.lua.c
        QUERIES_C     ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.c
        SQLITE_C      ${SQL2C_SQLITE_C}
        SQLITE_INCLUDE ${SQL2C_SQLITE_INCLUDE}
    )
else()
    message(STATUS "basic_example: Lua not found via pkg-config, skipping csql2lua demo (basicdb.so)")
endif()

# ---------------------------------------------------------------------------
# Optional: csql2go demo — generates a cgo-based Go package (go/mydb.go)
# from the manifest.json sql2c emitted above, and builds it. Skipped if a
# `go` toolchain isn't found.
# ---------------------------------------------------------------------------
find_program(GO_EXECUTABLE go)

if(GO_EXECUTABLE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../csql2go csql2go_build)

    csql2go_generate(
        CONFIG  ${CMAKE_CURRENT_SOURCE_DIR}/go/config.txt
        WORKDIR ${CMAKE_CURRENT_SOURCE_DIR}/go
        OUTPUTS ${CMAKE_CURRENT_SOURCE_DIR}/go/mydb.go
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sql/manifes.json
                ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.h
    )

    # The generated .go builds via plain `go build`/`go test` (its cgo
    # preamble carries its own #cgo CFLAGS/LDFLAGS), but cgo only
    # auto-compiles .c files that live in the package's own directory — so
    # mirror sql2c's generated queries.h/.c into go/ alongside it.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/go/queries.h
               ${CMAKE_CURRENT_SOURCE_DIR}/go/queries.c
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.h
                ${CMAKE_CURRENT_SOURCE_DIR}/go/queries.h
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.c
                ${CMAKE_CURRENT_SOURCE_DIR}/go/queries.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/queries.c
        COMMENT "csql2go example: mirroring queries.h/.c into go/"
    )

    add_custom_target(basicdb_go ALL
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/go/mydb.go
                ${CMAKE_CURRENT_SOURCE_DIR}/go/queries.h
                ${CMAKE_CURRENT_SOURCE_DIR}/go/queries.c
        COMMAND ${GO_EXECUTABLE} build ./...
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/go
        COMMENT "csql2go example: go build ./..."
    )
else()
    message(STATUS "basic_example: go not found, skipping csql2go demo")
endif()
