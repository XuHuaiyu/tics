add_executable(encryption_test EXCLUDE_FROM_ALL gtest_encryption_test.cpp)
target_link_libraries(encryption_test dbms gtest_main clickhouse_functions)

add_executable(rate_limiter_test EXCLUDE_FROM_ALL gtest_rate_limiter.cpp)
target_link_libraries(rate_limiter_test dbms gtest_main clickhouse_functions)
