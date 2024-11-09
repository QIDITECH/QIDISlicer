
add_cmake_project(OpenCSG
    EXCLUDE_FROM_ALL ON # No need to build this lib by default. Only used in experiment in sandboxes/opencsg
    URL https://github.com/floriankirsch/OpenCSG/archive/refs/tags/opencsg-1-4-2-release.zip
    URL_HASH SHA256=51afe0db79af8386e2027d56d685177135581e0ee82ade9d7f2caff8deab5ec5
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in ./CMakeLists.txt
)

set(DEP_OpenCSG_DEPENDS GLEW ZLIB)
