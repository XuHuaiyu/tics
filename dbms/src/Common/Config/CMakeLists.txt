
include(${ClickHouse_SOURCE_DIR}/cmake/dbms_glob_sources.cmake)

add_headers_and_sources(clickhouse_common_config .)

add_library(clickhouse_common_config ${SPLIT_SHARED} ${clickhouse_common_config_headers} ${clickhouse_common_config_sources})

target_link_libraries(clickhouse_common_config clickhouse_common_io string_utils cpptoml)
target_include_directories(clickhouse_common_config PRIVATE ${DBMS_INCLUDE_DIR})
