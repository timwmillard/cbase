cmake_minimum_required(VERSION 3.15)

project(jobq C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

# Vendored SQLite amalgamation and Lua source (via deps.lua): compiled
# straight into libjobq, so it has zero system libsqlite3/liblua dependency.
set(SQLITE_C ${CMAKE_SOURCE_DIR}/deps/sqlite3.c)

set(LUA_SRC_DIR ${CMAKE_SOURCE_DIR}/deps/lua-5.5.0/src)
file(GLOB LUA_SRC ${LUA_SRC_DIR}/*.c)
list(REMOVE_ITEM LUA_SRC ${LUA_SRC_DIR}/lua.c ${LUA_SRC_DIR}/luac.c)

# Build the embedc tool and embed dkjson.lua / src/runtime.lua into C headers
# (everything the job system needs except jobs.lua, which stays a loose file
# so -l PATH can swap in a different handlers script).
add_subdirectory(tool/embedc)

embedc_generate(
    INPUT  ${CMAKE_SOURCE_DIR}/dkjson.lua
    OUTPUT ${CMAKE_SOURCE_DIR}/src/dkjson_lua.h
    NAME   dkjson_lua
    TEXT
)
embedc_generate(
    INPUT  ${CMAKE_SOURCE_DIR}/src/runtime.lua
    OUTPUT ${CMAKE_SOURCE_DIR}/src/runtime_lua.h
    NAME   runtime_lua
    TEXT
)
add_custom_target(generate_lua_headers DEPENDS
    ${CMAKE_SOURCE_DIR}/src/dkjson_lua.h
    ${CMAKE_SOURCE_DIR}/src/runtime_lua.h
)

include_directories(deps)
include_directories(${LUA_SRC_DIR})

# libjobq: queue + worker pool, with vendored sqlite3/lua compiled straight in.
add_library(jobq_static STATIC
    src/queue.c
    src/pool.c
    ${SQLITE_C}
    ${LUA_SRC}
)
add_dependencies(jobq_static generate_lua_headers)
set_target_properties(jobq_static PROPERTIES OUTPUT_NAME jobq)

add_executable(jobqd src/main.c)
target_link_libraries(jobqd PRIVATE jobq_static Threads::Threads)

install(TARGETS jobqd jobq_static
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)
install(FILES src/jobq.h src/queue.h src/pool.h DESTINATION include/jobq)

# Single-header distribution (stb-style jobq.h): not part of `all` — build it
# explicitly with `cmake --build build --target jobq_single_header`. Depends
# on embedc's generated headers since src/pool.c #includes them; singleh
# inlines that content into the implementation section same as any other
# local #include.
add_subdirectory(tool/singleh)

singleh_generate(
    MANIFEST ${CMAKE_SOURCE_DIR}/jobq.manifest
    OUTPUT   ${CMAKE_SOURCE_DIR}/jobq.h
    WORKDIR  ${CMAKE_SOURCE_DIR}
    DEPENDS
        ${CMAKE_SOURCE_DIR}/src/jobq.h
        ${CMAKE_SOURCE_DIR}/src/queue.h
        ${CMAKE_SOURCE_DIR}/src/queue.c
        ${CMAKE_SOURCE_DIR}/src/pool.h
        ${CMAKE_SOURCE_DIR}/src/pool.c
        ${CMAKE_SOURCE_DIR}/src/dkjson_lua.h
        ${CMAKE_SOURCE_DIR}/src/runtime_lua.h
)
add_custom_target(jobq_single_header DEPENDS ${CMAKE_SOURCE_DIR}/jobq.h)
