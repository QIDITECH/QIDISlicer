get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests ${_TEST_NAME}_tests_main.cpp printer_parts.cpp printer_parts.hpp)

# mold linker for successful linking needs also to link TBB library and link it before libslic3r.
target_link_libraries(${_TEST_NAME}_tests test_common TBB::tbb TBB::tbbmalloc libnest2d )
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
set(_catch_args "exclude:[NotWorking]")
list(APPEND _catch_args "${CATCH_EXTRA_ARGS}")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${_catch_args})
