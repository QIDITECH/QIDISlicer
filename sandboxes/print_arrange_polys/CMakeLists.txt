add_executable(print_arrange_polys main.cpp)

target_link_libraries(print_arrange_polys libslic3r admesh)

if (WIN32)
    qidislicer_copy_dlls(print_arrange_polys)
endif()
