cmake_minimum_required(VERSION 3.16)
project(marine_chart_viewer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

# GDAL ships a pkg-config file ("gdal") on most platforms (incl. MSYS2).
# If your install lacks it, see the README for the find_package(GDAL) fallback.
pkg_check_modules(GDAL REQUIRED IMPORTED_TARGET gdal)

add_executable(chartviewer
    src/main.cpp
    src/chart_loader.cpp
    src/chart_widget.cpp
)

target_link_libraries(chartviewer PRIVATE PkgConfig::GTK4 PkgConfig::GDAL)

if(WIN32)
    # Hide the console window for the GUI build (comment out to keep a console
    # for debugging / stderr from GDAL).
    set_target_properties(chartviewer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
