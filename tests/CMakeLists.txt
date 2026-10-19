find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(penelope_tests regressiontest.cpp)
target_link_libraries(penelope_tests PRIVATE PenelopeCore Qt6::Test)
target_include_directories(penelope_tests PRIVATE ${HARFBUZZ_INCLUDE_DIRS})
add_test(NAME penelope_regressions COMMAND penelope_tests)
set_tests_properties(penelope_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

qt_add_executable(penelope_renderbench renderbench.cpp)
target_link_libraries(penelope_renderbench PRIVATE PenelopeCore ${CMAKE_DL_LIBS})
target_include_directories(penelope_renderbench PRIVATE ${HARFBUZZ_INCLUDE_DIRS})

add_test(NAME penelope_renderbench_smoke
         COMMAND penelope_renderbench --sizes 25 --iterations 1)
set_tests_properties(penelope_renderbench_smoke
                     PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
# A size curve is a manual performance experiment, not a correctness gate.
# Keep the smoke case cheap for normal CTest runs and release packaging.
option(PENELOPE_PERFORMANCE_TESTS "Register the long render benchmark curve" OFF)
if(PENELOPE_PERFORMANCE_TESTS)
    add_test(NAME penelope_renderbench_curve
             COMMAND penelope_renderbench --sizes 100,400,1600,6400 --iterations 3)
    set_tests_properties(penelope_renderbench_curve PROPERTIES
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 900 LABELS performance)
endif()

# The real menu layout, so the suite can check what the application shows.
qt_add_resources(penelope_tests "xmlgui"
    PREFIX "/kxmlgui5/penelope"
    BASE ${CMAKE_SOURCE_DIR}/src/app
    FILES ${CMAKE_SOURCE_DIR}/src/app/penelopeui.rc
)
