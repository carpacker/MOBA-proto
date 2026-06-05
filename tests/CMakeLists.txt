# Engine tests. Minimal shared helpers in tests/test.h (CHECK/CHECK_APPROX); the
# self-registering test.h harness + CTest + headless eng_core_group land in M1.4.
# ${CMAKE_CURRENT_SOURCE_DIR} on the include path so each suite finds "test.h".

add_executable(mem_tests mem/mem_tests.cpp)
target_include_directories(mem_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mem_tests PRIVATE eng::core eng::platform moba_warnings moba_options)

add_executable(math_tests math/math_tests.cpp)
target_include_directories(math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(math_tests PRIVATE eng::math moba_warnings moba_options)

add_executable(container_tests containers/container_tests.cpp)
target_include_directories(container_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(container_tests PRIVATE eng::core eng::platform moba_warnings moba_options)

# Determinism suite, built TWICE with different float models. The golden hash is
# integer-only, so both must produce the identical value (ADR-0002 / M1.2 DoD).
add_executable(det_tests det/det_tests.cpp)
target_include_directories(det_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(det_tests PRIVATE eng::math moba_warnings moba_options)
target_compile_options(det_tests PRIVATE /fp:precise)

add_executable(det_tests_fastfp det/det_tests.cpp)
target_include_directories(det_tests_fastfp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(det_tests_fastfp PRIVATE eng::math moba_warnings moba_options)
target_compile_options(det_tests_fastfp PRIVATE /fp:fast)
