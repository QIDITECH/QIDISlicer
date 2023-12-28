set(LibBGCode_SOURCE_DIR "" CACHE PATH "Optionally specify local LibBGCode source directory")

set(_source_dir_line
    URL https://github.com/prusa3d/libbgcode/archive/bc390aab4427589a6402b4c7f65cf4d0a8f987ec.zip
    URL_HASH SHA256=0c86cb67232089728233014f937e2a07d133a61e31dd8811a9c905e563a49f24
)

if (LibBGCode_SOURCE_DIR)
    set(_source_dir_line "SOURCE_DIR;${LibBGCode_SOURCE_DIR};BUILD_ALWAYS;ON")
endif ()

# add_cmake_project(LibBGCode_deps
#     ${_source_dir_line}
#     SOURCE_SUBDIR deps
#     BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/
#     CMAKE_ARGS
#         -DLibBGCode_Deps_DEP_DOWNLOAD_DIR:PATH=${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}
#         -DDEP_CMAKE_OPTS:STRING=-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
#         -DLibBGCode_Deps_SELECT_ALL:BOOL=OFF
#         -DLibBGCode_Deps_SELECT_heatshrink:BOOL=ON
#         -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
#         -DLibBGCode_Deps_DEP_INSTALL_PREFIX=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
# )

add_cmake_project(LibBGCode
    ${_source_dir_line}
    CMAKE_ARGS
        -DLibBGCode_BUILD_TESTS:BOOL=OFF
        -DLibBGCode_BUILD_CMD_TOOL:BOOL=OFF
)

# set(DEP_LibBGCode_deps_DEPENDS ZLIB Boost)
# set(DEP_LibBGCode_DEPENDS LibBGCode_deps)
set(DEP_LibBGCode_DEPENDS ZLIB Boost heatshrink)
