cmake_minimum_required(VERSION 3.10)

project(wxGL_Test)

add_executable(wxgltest WIN32 main.cpp )

set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk${SLIC3R_GTK}")
find_package(wxWidgets 3.1 REQUIRED COMPONENTS core base gl html)
find_package(OpenGL REQUIRED)
find_package(NanoSVG REQUIRED)
include(${wxWidgets_USE_FILE})

target_include_directories(wxgltest PRIVATE ${wxWidgets_INCLUDE_DIRS})
target_compile_definitions(wxgltest PRIVATE ${wxWidgets_DEFINITIONS})
target_compile_definitions(wxgltest PUBLIC -DwxDEBUG_LEVEL=0)

target_link_libraries(wxgltest ${wxWidgets_LIBRARIES}
    OpenGL::GL
    OpenGL::EGL
    NanoSVG::nanosvgrast
#    png16
#    X11
    X11 wayland-client wayland-egl
)
