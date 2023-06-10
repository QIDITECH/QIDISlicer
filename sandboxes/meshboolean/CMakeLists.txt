add_executable(meshboolean MeshBoolean.cpp)

target_link_libraries(meshboolean libslic3r)

if (WIN32)
    qidislicer_copy_dlls(meshboolean)
endif()
