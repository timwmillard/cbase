cmake_minimum_required(VERSION 3.15)
project(csql2lua C)

set(CMAKE_C_STANDARD 11)

add_executable(csql2lua csql2lua.c)

# ---------------------------------------------------------------------------
# csql2lua_generate(CONFIG <cfg> WORKDIR <dir> OUTPUTS <...> DEPENDS <...>)
#
# Runs the built tool to turn a sql2c manifest.json into a Lua C module
# source. Used by the consuming project.
# ---------------------------------------------------------------------------
function(csql2lua_generate)
    cmake_parse_arguments(GEN "" "CONFIG;WORKDIR" "OUTPUTS;DEPENDS" ${ARGN})
    add_custom_command(
        OUTPUT ${GEN_OUTPUTS}
        COMMAND $<TARGET_FILE:csql2lua> -config ${GEN_CONFIG}
        DEPENDS csql2lua ${GEN_CONFIG} ${GEN_DEPENDS}
        WORKING_DIRECTORY ${GEN_WORKDIR}
        COMMENT "csql2lua: generating ${GEN_OUTPUTS}"
    )
endfunction()

# ---------------------------------------------------------------------------
# Resolve Lua — the generated module's only build-time dependency besides
# the sqlite3 amalgamation and the sql2c-generated queries.h/.c it wraps.
#
# Override with CSQL2LUA_LUA_INCLUDE / CSQL2LUA_LUA_LIBRARIES if pkg-config
# can't find your Lua install.
# ---------------------------------------------------------------------------
if(NOT CSQL2LUA_LUA_INCLUDE)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        foreach(_csql2lua_pc lua5.4 lua5.3 lua-5.4 lua-5.3 lua)
            if(NOT LUA_FOUND)
                pkg_check_modules(LUA QUIET ${_csql2lua_pc})
            endif()
        endforeach()
    endif()
    if(LUA_FOUND)
        # PARENT_SCOPE: add_subdirectory() gives this file its own variable
        # scope, but csql2lua_module() is typically called from the parent
        # (the project that add_subdirectory'd us) — it needs these visible
        # in *that* scope, not just this one.
        set(CSQL2LUA_LUA_INCLUDE ${LUA_INCLUDE_DIRS} PARENT_SCOPE)
        set(CSQL2LUA_LUA_LIBRARIES ${LUA_LIBRARIES} PARENT_SCOPE)
        set(CSQL2LUA_LUA_LIBRARY_DIRS ${LUA_LIBRARY_DIRS} PARENT_SCOPE)
        # Also keep local copies in case csql2lua_module() is called from
        # this same directory scope directly.
        set(CSQL2LUA_LUA_INCLUDE ${LUA_INCLUDE_DIRS})
        set(CSQL2LUA_LUA_LIBRARIES ${LUA_LIBRARIES})
        set(CSQL2LUA_LUA_LIBRARY_DIRS ${LUA_LIBRARY_DIRS})
    else()
        message(WARNING
            "csql2lua: Lua not found via pkg-config. Set CSQL2LUA_LUA_INCLUDE "
            "and CSQL2LUA_LUA_LIBRARIES, or csql2lua_module() targets will fail to build.")
    endif()
endif()

# ---------------------------------------------------------------------------
# csql2lua_module(<target> MODULE <name> SOURCE <generated.c>
#                  QUERIES_C <queries.c> SQLITE_C <sqlite3.c> SQLITE_INCLUDE <dir>)
#
# Builds the generated Lua module source together with the sql2c-generated
# queries.c and the SQLite amalgamation into a loadable Lua C module
# (<name>.so, no "lib" prefix) that Lua's require() can find directly.
# ---------------------------------------------------------------------------
function(csql2lua_module target)
    cmake_parse_arguments(MOD "" "MODULE;SOURCE;QUERIES_C;SQLITE_C;SQLITE_INCLUDE" "" ${ARGN})
    add_library(${target} MODULE ${MOD_SOURCE} ${MOD_QUERIES_C} ${MOD_SQLITE_C})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME ${MOD_MODULE}
        PREFIX ""
        SUFFIX ".so"
    )
    target_include_directories(${target} PRIVATE ${MOD_SQLITE_INCLUDE} ${CSQL2LUA_LUA_INCLUDE})
    target_link_directories(${target} PRIVATE ${CSQL2LUA_LUA_LIBRARY_DIRS})
    target_link_libraries(${target} PRIVATE ${CSQL2LUA_LUA_LIBRARIES})
    target_compile_definitions(${target} PRIVATE SQLITE_ENABLE_COLUMN_METADATA)
endfunction()
