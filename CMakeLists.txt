cmake_minimum_required(VERSION 3.16)
project(marine_chart_viewer_qt CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

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
    src/side_menu.cpp
    src/settings.cpp
    src/chart_sets_dialog.cpp
    src/units_dialog.cpp
    src/touch_spin_box.cpp
    src/stale_thresholds_dialog.cpp
    src/ownship_prediction_dialog.cpp
    src/nmea0183_debug_window.cpp
    src/nmea0183_plugin.cpp
    src/nav_data_browser_window.cpp
    src/data_priority_dialog.cpp
    src/core_api.cpp
    src/plugin_manager.cpp
    src/test_plugin.cpp
    src/chart_view.cpp
    src/chart_catalog.cpp
    src/chart_loader.cpp
    src/nav_data_store.cpp
    src/ais_target_store.cpp
    src/ais_decoder.cpp
    src/ais_overlay.cpp
    src/vessel_symbol.cpp
    src/simulator.cpp
    src/nmea0183_client.cpp
)

target_link_libraries(chartviewer PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network ${GDAL_LINK})

# Dev convenience: lets the basemap resolver fall back to the in-tree gshhg-shp
# folder during development. On an installed build this path won't exist, so it
# is skipped and the standard data locations are used instead.
target_compile_definitions(chartviewer PRIVATE
    CHARTPLOTTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set_target_properties(chartviewer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# ---- Bundle GDAL data files -------------------------------------------------
# S-57 charts need s57objectclasses.csv (and a few companion CSVs) to map the
# raw OBJL integer codes in each ENC file to named object classes like DEPARE,
# LNDARE, COALNE.  Without them GDAL reads geometry fine but returns empty/raw
# layer names — the chart loader's classify() function can't match anything, so
# every feature renders without fill or colour (grey outlines only).
#
# Strategy: find the data dir relative to the GDAL library that CMake resolved,
# then copy it into gdal-data/ next to the exe at build time. main.cpp reads
# applicationDirPath()/gdal-data and passes it to chart::init(), which calls
# CPLSetConfigOption("GDAL_DATA", ...) before GDALAllRegister().

find_path(GDAL_DATA_DIR
    NAMES s57objectclasses.csv
    HINTS "$ENV{GDAL_DATA}"
    NO_DEFAULT_PATH
)

if(NOT GDAL_DATA_DIR AND TARGET GDAL::GDAL)
    # Derive from the GDAL library location: lib → prefix → share/gdal.
    foreach(_prop
            IMPORTED_LOCATION_RELEASE
            IMPORTED_LOCATION_RELWITHDEBINFO
            IMPORTED_LOCATION_MINSIZEREL
            IMPORTED_LOCATION_DEBUG
            IMPORTED_LOCATION)
        if(NOT _gdal_lib_path)
            get_target_property(_gdal_lib_path GDAL::GDAL ${_prop})
        endif()
    endforeach()
    if(_gdal_lib_path)
        get_filename_component(_gdal_lib_dir "${_gdal_lib_path}" DIRECTORY)
        find_path(GDAL_DATA_DIR s57objectclasses.csv
            HINTS
                "${_gdal_lib_dir}/../share/gdal"
                "${_gdal_lib_dir}/../../share/gdal"
                "${_gdal_lib_dir}/gdal"
                "${_gdal_lib_dir}/gdal-data"
            NO_DEFAULT_PATH
        )
    endif()
endif()

if(GDAL_DATA_DIR)
    message(STATUS "GDAL data directory found: ${GDAL_DATA_DIR}")
    add_custom_command(TARGET chartviewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GDAL_DATA_DIR}"
            "$<TARGET_FILE_DIR:chartviewer>/gdal-data"
        COMMENT "Bundling GDAL data files into gdal-data/"
        VERBATIM
    )
else()
    message(WARNING
        "GDAL data directory not found.\n"
        "S-57 charts will render without colours on machines without GDAL installed.\n"
        "Set GDAL_DATA_DIR=<path containing s57objectclasses.csv> and re-run CMake.")
endif()
