include_directories (${CMAKE_CURRENT_BINARY_DIR})

add_executable (gtest_log_search gtest_log_search.cpp)
target_link_libraries (gtest_log_search gtest_main flash_service clickhouse-server-lib)

add_executable (exchange_perftest exchange_perftest.cpp)
target_link_libraries (exchange_perftest gtest_main dbms flash_service clickhouse-server-lib)