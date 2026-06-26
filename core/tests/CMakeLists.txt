add_executable(bsr_core_tests
    test_parse.cpp
)

target_link_libraries(bsr_core_tests PRIVATE bsr_core)

add_test(NAME bsr_core_tests COMMAND bsr_core_tests)
