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

# Build the bin2c embedder into the build folder
set(BIN2C_BIN ${CMAKE_BINARY_DIR}/bin2c)
add_custom_command(
    OUTPUT ${BIN2C_BIN}
    COMMAND ${CMAKE_C_COMPILER} -O2 -o ${BIN2C_BIN} ${CMAKE_SOURCE_DIR}/tool/bin2c.c
    DEPENDS ${CMAKE_SOURCE_DIR}/tool/bin2c.c
    COMMENT "Building bin2c embedder"
)

# Embed schema.sql into schema.h
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/src/schema.h
    COMMAND ${BIN2C_BIN} ${CMAKE_SOURCE_DIR}/sql/schema.sql ${CMAKE_SOURCE_DIR}/src/schema.h schema
    DEPENDS ${BIN2C_BIN} ${CMAKE_SOURCE_DIR}/sql/schema.sql
    COMMENT "Embedding schema.sql into schema.h with bin2c"
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

