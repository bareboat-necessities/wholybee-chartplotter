cmake_minimum_required(VERSION 3.16)
project(marine_chart_viewer_qt CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Prefer GDAL's own CMake config; fall back to pkg-config (e.g. MSYS2).
find_package(GDAL QUIET)
if(GDAL_FOUND)
    set(GDAL_LINK GDAL::GDAL)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GDAL REQUIRED IMPORTED_TARGET gdal)
    set(GDAL_LINK PkgConfig::GDAL)
endif()

qt_add_executable(chartviewer
    src/main.cpp
    src/main_window.cpp
    src/chart_view.cpp
    src/chart_loader.cpp
)

target_link_libraries(chartviewer PRIVATE Qt6::Widgets ${GDAL_LINK})

set_target_properties(chartviewer PROPERTIES
    WIN32_EXECUTABLE TRUE   # no console window on Windows
    MACOSX_BUNDLE TRUE
)
