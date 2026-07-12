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
