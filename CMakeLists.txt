cmake_minimum_required(VERSION 3.16)
project(marine_chart_viewer_qt CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network Sql WebSockets)

# Prefer GDAL's own CMake config; fall back to pkg-config (e.g. MSYS2).
find_package(GDAL QUIET)
if(GDAL_FOUND)
    set(GDAL_LINK GDAL::GDAL)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GDAL REQUIRED IMPORTED_TARGET gdal)
    set(GDAL_LINK PkgConfig::GDAL)
endif()

# ---- Symbol atlas build-time tool ------------------------------------------
#
# gen_symbols parses data/chartsymbols.xml and emits symbols.bin: a flat binary
# lookup table mapping S-57 object-class names → symbol indices and symbol
# names → atlas-PNG rectangles + pivot offsets.
#
# The tool is a plain console executable (not a Qt GUI app).  It only needs
# Qt6::Core (QXmlStreamReader lives there, not in Qt6::Xml).
#
# NOTE: In a cross-compilation scenario the host (build-machine) tool must be
#       built separately.  For the common native-Windows build this is a
#       non-issue.

add_executable(gen_symbols tools/gen_symbols.cpp)
set_target_properties(gen_symbols PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(gen_symbols PRIVATE Qt6::Core)

# Run gen_symbols at build time to produce symbols.bin in the binary directory.
# The command re-runs whenever the tool exe or the XML source changes.
#
# gen_symbols links Qt6::Core dynamically, so Qt6Core.dll must be locatable when
# the tool runs during the build.  On Windows the Qt DLLs live in the Qt bin/
# directory, which is not normally on the build environment's PATH — without
# this the tool aborts with STATUS_DLL_NOT_FOUND (0xC0000135).  Prepend the Qt
# bin directory (the folder containing the Qt6::Core import library's runtime)
# to PATH for just this invocation.  `cmake -E env --modify` (CMake >= 3.25)
# handles the list separator correctly and leaves the rest of PATH intact.
add_custom_command(
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/symbols.bin"
    COMMAND ${CMAKE_COMMAND} -E env
            --modify "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            "$<TARGET_FILE:gen_symbols>"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/chartsymbols.xml"
            "${CMAKE_CURRENT_BINARY_DIR}/symbols.bin"
    DEPENDS gen_symbols
            "${CMAKE_CURRENT_SOURCE_DIR}/data/chartsymbols.xml"
    COMMENT "Generating symbol lookup table (symbols.bin) from chartsymbols.xml"
    VERBATIM
)
add_custom_target(symbols_bin ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/symbols.bin"
)

qt_add_executable(chartviewer
    src/main.cpp
    src/main_window.cpp
    src/side_menu.cpp
    src/settings.cpp
    src/chart_sets_dialog.cpp
    src/units_dialog.cpp
    src/navigation_options_dialog.cpp
    src/touch_spin_box.cpp
    src/stale_thresholds_dialog.cpp
    src/ownship_prediction_dialog.cpp
    src/nmea0183_debug_window.cpp
    src/nmea0183_plugin.cpp
    src/nmea0183_nav_sender.cpp
    src/nmea2000_client.cpp
    src/nmea2000_plugin.cpp
    src/n2k_decoder.cpp
    src/actisense_ascii_parser.cpp
    src/nav_data_browser_window.cpp
    src/data_priority_dialog.cpp
    src/chart_detail_dialog.cpp
    src/chart_symbol_size_dialog.cpp
    src/ship_size_dialog.cpp
    src/ownship_mmsi_dialog.cpp
    src/heading_source_dialog.cpp
    src/dangerous_ships_dialog.cpp
    src/core_api.cpp
    src/plugin_manager.cpp
    src/chart_view.cpp
    src/chart_catalog.cpp
    src/chart_loader.cpp
    src/mbtiles_reader.cpp
    src/mbtiles_service.cpp
    src/nav_data_store.cpp
    src/ais_target_store.cpp
    src/cpa_calculator.cpp
    src/ais_decoder.cpp
    src/ais_overlay.cpp
    src/ais_target_info_window.cpp
    src/ais_target_list_dialog.cpp
    src/ais_quick_info_window.cpp
    src/route_store.cpp
    src/route_navigator.cpp
    src/nav_display_window.cpp
    src/route_overlay.cpp
    src/route_list_dialog.cpp
    src/waypoint_list_dialog.cpp
    src/route_properties_dialog.cpp
    src/waypoint_properties_dialog.cpp
    src/route_quick_info_window.cpp
    src/name_dialog.cpp
    src/vessel_symbol.cpp
    src/sym_atlas.cpp
    src/theme.cpp
    src/simulator.cpp
    src/nmea0183_client.cpp
)

target_link_libraries(chartviewer PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Sql ${GDAL_LINK})
add_dependencies(chartviewer symbols_bin)

# Dev convenience: lets the basemap resolver fall back to the in-tree gshhg-shp
# folder during development. On an installed build this path won't exist, so it
# is skipped and the standard data locations are used instead.
target_compile_definitions(chartviewer PRIVATE
    CHARTPLOTTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    # Flips HOST_EXPORT to dllexport in the host's translation units; plugins
    # don't define this, so they see dllimport for the same types.
    CHARTPLOTTER_BUILD_HOST=1)

set_target_properties(chartviewer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    # Expose the host's symbols so dynamically loaded plugins can link against
    # them. On Windows ENABLE_EXPORTS produces an import library (chartviewer.lib)
    # and WINDOWS_EXPORT_ALL_SYMBOLS auto-generates a .def listing every global
    # symbol from the exe's object files (including MOC-emitted metaobjects and
    # signal stubs). On Linux/macOS ENABLE_EXPORTS adds -rdynamic so the
    # dynamic linker honours dlopen lookups against the exe.
    ENABLE_EXPORTS TRUE
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)

# ---- Qt DLL deployment (Windows only) --------------------------------------
#
# After each build, run windeployqt to copy the Qt DLLs, platform plugins,
# and QML/styles needed to run the exe in-place.  windeployqt lives next to
# the Qt6::Core DLL, so we derive its path from the Core import location
# rather than hard-coding the Qt install prefix.
#
# --no-compiler-runtime  skip the MSVC redistributable (already present on dev
#                        machines; add an installer step for distribution).
# --no-translations      skip translation catalogues (saves ~30 MB in the
#                        output folder; re-enable if you add i18n support).
if(WIN32)
    get_target_property(_qt_core_loc Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _qt_core_loc)
        get_target_property(_qt_core_loc Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin_dir}" NO_DEFAULT_PATH)

    if(WINDEPLOYQT)
        message(STATUS "windeployqt found: ${WINDEPLOYQT}")
        add_custom_command(TARGET chartviewer POST_BUILD
            COMMAND "${WINDEPLOYQT}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-compiler-runtime
                --no-translations
                "$<TARGET_FILE:chartviewer>"
            COMMENT "Running windeployqt to bundle Qt DLLs"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found — Qt DLLs will not be copied automatically.")
    endif()
endif()

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

# ---- Symbol atlas data files ------------------------------------------------
#
# Copy the prebaked symbols.bin and the atlas PNG(s) next to the executable
# so ChartView can find them at runtime via applicationDirPath().
#
# Place the source files in data/ under the project root:
#   data/chartsymbols.xml       (input to gen_symbols — build time only)
#   data/rastersymbols-day.png  (OpenCPN day-mode sprite sheet)
#   data/rastersymbols-dusk.png (optional — dusk variant)
#   data/rastersymbols-dark.png (optional — night variant)

# symbols.bin is generated in the binary directory by the custom command above.
add_custom_command(TARGET chartviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/symbols.bin"
        "$<TARGET_FILE_DIR:chartviewer>/symbols.bin"
    COMMENT "Installing symbols.bin next to executable"
    VERBATIM
)

# Atlas PNGs: copy each one that is present in data/.  Missing variants are
# silently skipped so a partial data/ directory doesn't break the build.
foreach(_atlas_png
        rastersymbols-day.png
        rastersymbols-dusk.png
        rastersymbols-dark.png)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/data/${_atlas_png}")
    if(EXISTS "${_src}")
        add_custom_command(TARGET chartviewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_src}"
                "$<TARGET_FILE_DIR:chartviewer>/${_atlas_png}"
            COMMENT "Installing ${_atlas_png} next to executable"
            VERBATIM
        )
    else()
        message(STATUS
            "Symbol atlas PNG not found (optional): ${_src}\n"
            "  Copy rastersymbols-*.png files into data/ to enable atlas symbols.")
    endif()
endforeach()

# ---- Dynamic plugins --------------------------------------------------------
#
# Each plugin lives in its own subdirectory under plugins/, builds to its own
# shared library (a separate target / VS project), and post-build copies itself
# into plugins/ next to the host exe so QPluginLoader picks it up at runtime.
add_subdirectory(plugins/test_plugin)
add_subdirectory(plugins/signalk_plugin)
add_subdirectory(plugins/gpx_plugin)

