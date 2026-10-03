find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(penelope_tests regressiontest.cpp)
target_link_libraries(penelope_tests PRIVATE PenelopeCore Qt6::Test)
target_include_directories(penelope_tests PRIVATE ${HARFBUZZ_INCLUDE_DIRS})
add_test(NAME penelope_regressions COMMAND penelope_tests)
set_tests_properties(penelope_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

qt_add_executable(penelope_renderbench renderbench.cpp)
target_link_libraries(penelope_renderbench PRIVATE PenelopeCore)
target_include_directories(penelope_renderbench PRIVATE ${HARFBUZZ_INCLUDE_DIRS})

add_test(NAME penelope_renderbench_smoke
         COMMAND penelope_renderbench --sizes 25 --iterations 1)
add_test(NAME penelope_renderbench_curve
         COMMAND penelope_renderbench --sizes 100,400,1600,6400 --iterations 3)
set_tests_properties(penelope_renderbench_smoke penelope_renderbench_curve
                     PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 900)
