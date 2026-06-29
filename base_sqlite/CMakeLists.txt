cmake_minimum_required(VERSION 3.15)

project(sql_demo C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build the sql2c code generator into the build folder
set(SQL2C_BIN ${CMAKE_BINARY_DIR}/sql2c)
add_custom_command(
    OUTPUT ${SQL2C_BIN}
    COMMAND go build -o ${SQL2C_BIN} .
    DEPENDS
        ${CMAKE_SOURCE_DIR}/tool/sql2c/main.go
        ${CMAKE_SOURCE_DIR}/tool/sql2c/go.mod
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tool/sql2c
    COMMENT "Building sql2c code generator"
)

# Generate SQL queries with sql2c
add_custom_command(
    OUTPUT
        ${CMAKE_SOURCE_DIR}/src/queries.h
        ${CMAKE_SOURCE_DIR}/src/queries.c
        ${CMAKE_SOURCE_DIR}/src/models.h
    COMMAND ${SQL2C_BIN} -config config.yaml
    DEPENDS
        ${SQL2C_BIN}
        ${CMAKE_SOURCE_DIR}/sql/schema.sql
        ${CMAKE_SOURCE_DIR}/sql/queries.sql
        ${CMAKE_SOURCE_DIR}/sql/config.yaml
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/sql
    COMMENT "Generating queries.h, queries.c, models.h with sql2c"
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

