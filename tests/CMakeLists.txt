# Engine tests. Minimal shared helpers in tests/test.h (CHECK/CHECK_APPROX); the
# self-registering test.h harness + CTest + headless eng_core_group land in M1.4.
# ${CMAKE_CURRENT_SOURCE_DIR} on the include path so each suite finds "test.h".

add_executable(mem_tests mem/mem_tests.cpp)
target_include_directories(mem_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mem_tests PRIVATE eng::core eng::platform moba_warnings moba_options)

add_executable(math_tests math/math_tests.cpp)
target_include_directories(math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(math_tests PRIVATE eng::math moba_warnings moba_options)
