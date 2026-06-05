# Engine tests. M1.0: a minimal mem_tests console exe (links core + platform for the
# real page-commit check). M1.4 introduces the test.h harness, CTest, and the headless
# eng_core_group aggregate.
add_executable(mem_tests mem/mem_tests.cpp)
target_link_libraries(mem_tests PRIVATE eng::core eng::platform moba_warnings moba_options)
