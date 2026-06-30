cmake_minimum_required(VERSION 3.15)

project(sql_demo C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build the sql2c code generator (standalone tool), pointed at our sqlite.
set(SQL2C_SQLITE_C ${CMAKE_SOURCE_DIR}/deps/sqlite3.c CACHE FILEPATH "")
set(SQL2C_SQLITE_INCLUDE ${CMAKE_SOURCE_DIR}/deps CACHE PATH "")
add_subdirectory(tool/sql2c)

# Generate models.h / queries.h / queries.c from the schema + queries.
sql2c_generate(
    CONFIG  ${CMAKE_SOURCE_DIR}/sql/config.txt
    WORKDIR ${CMAKE_SOURCE_DIR}/sql
    OUTPUTS ${CMAKE_SOURCE_DIR}/src/queries.h
            ${CMAKE_SOURCE_DIR}/src/queries.c
            ${CMAKE_SOURCE_DIR}/src/models.h
    DEPENDS ${CMAKE_SOURCE_DIR}/sql/schema.sql
            ${CMAKE_SOURCE_DIR}/sql/queries.sql
)
add_custom_target(generate_queries DEPENDS
    ${CMAKE_SOURCE_DIR}/src/queries.h
    ${CMAKE_SOURCE_DIR}/src/queries.c
    ${CMAKE_SOURCE_DIR}/src/models.h
)

# Build the embedc tool (standalone) and embed schema.sql into schema.h as a
# null-terminated C string (schema_data / schema_len).
add_subdirectory(tool/embedc)

embedc_generate(
    INPUT  ${CMAKE_SOURCE_DIR}/sql/schema.sql
    OUTPUT ${CMAKE_SOURCE_DIR}/src/schema.h
    NAME   schema
    TEXT
)
add_custom_target(generate_schema DEPENDS
    ${CMAKE_SOURCE_DIR}/src/schema.h
)

include_directories(.)
include_directories(deps)


add_executable(${PROJECT_NAME}
    src/main.c
    src/queries.c
    src/deps.c
    deps/sqlite3.c
)

add_dependencies(${PROJECT_NAME}
    generate_queries
    generate_schema
)
target_link_libraries(${PROJECT_NAME})

