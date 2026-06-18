# Instruments Plugin — built as a separate Qt MODULE library, loaded by the host
# at runtime via QPluginLoader. Mirrors plugins/gpx_plugin/CMakeLists.txt. Needs
# only Qt6::Widgets/Core (the catalogue XML is parsed with QXmlStreamReader from
# Qt6::Core), all already deployed alongside the host exe, so there are no extra
# runtime DLLs to copy.

add_library(instruments_plugin MODULE
    instruments_plugin.hpp
    instruments_plugin.cpp
    instruments_plugin_factory.cpp
    instrument_config.hpp
    instrument_config.cpp
    instrument_tile.hpp
    instrument_tile.cpp
    instrument_bar.hpp
    instrument_bar.cpp
    instruments_settings_page.hpp
    instruments_settings_page.cpp
    instruments_plugin.json
)

set_target_properties(instruments_plugin PROPERTIES
    AUTOMOC ON
    # Stable basename so it never collides with the host or a third-party plugin
    # in the same plugins/ drop folder.
    OUTPUT_NAME "chartplotter_instruments_plugin"
    PREFIX ""           # no "lib" prefix on non-Windows MODULE libs
)

# Plugins consume the public chartplotter SDK headers and a small handful of
# core headers; they link against the host exe for those symbols (ENABLE_EXPORTS
# on chartviewer).
target_include_directories(instruments_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(instruments_plugin PRIVATE
    Qt6::Widgets
    Qt6::Core
    chartviewer        # host symbols (NavDataStore, TouchSpinBox, theme, ...)
)

# Deploy the built plugin next to the host exe so QPluginLoader finds it via the
# well-known plugins/ subfolder.
add_custom_command(TARGET instruments_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:chartviewer>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:instruments_plugin>"
            "$<TARGET_FILE_DIR:chartviewer>/plugins/"
    # The default instrument catalogue ships next to the exe; the plugin seeds a
    # user-editable copy from it on first run (see InstrumentCatalog::load).
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/instruments.xml"
            "$<TARGET_FILE_DIR:chartviewer>/instruments.xml"
    COMMENT "Deploying instruments_plugin into plugins/ next to chartviewer"
    VERBATIM
)
