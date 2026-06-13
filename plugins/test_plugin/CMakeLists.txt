# Test Plugin — built as a separate Qt MODULE library, loaded by the host at
# runtime via QPluginLoader. Proves out the dynamic plugin loading path; built
# alongside the main exe (in the same Visual Studio solution, as its own
# project) so it stays in step with API changes.

add_library(test_plugin MODULE
    test_plugin.hpp
    test_plugin.cpp
    test_plugin_factory.cpp
    test_plugin.json
)

set_target_properties(test_plugin PROPERTIES
    AUTOMOC ON
    # Use a stable basename: chartplotter_test_plugin.dll keeps third-party
    # plugins from clashing with the host's "test_plugin" target name when
    # they're all dropped in the same plugins/ folder.
    OUTPUT_NAME "chartplotter_test_plugin"
    PREFIX ""           # no "lib" prefix on non-Windows MODULE libs
)

# Plugins consume the public chartplotter SDK headers (plugin_api.hpp,
# plugin_factory.hpp) plus the small handful of core headers they reference
# (nav_data_store.hpp, theme.hpp, touch_spin_box.hpp). They link against the
# host exe for those symbols — see ENABLE_EXPORTS on chartviewer.
target_include_directories(test_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_plugin PRIVATE
    Qt6::Widgets
    Qt6::Core
    chartviewer        # host symbols (NavDataStore, theme, TouchSpinBox, ...)
)

# Deploy the built plugin next to the host exe so QPluginLoader finds it via
# the well-known plugins/ subfolder. Done as a post-build copy so the plugin's
# default output dir stays out of the way of windeployqt.
add_custom_command(TARGET test_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:chartviewer>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:test_plugin>"
            "$<TARGET_FILE_DIR:chartviewer>/plugins/"
    COMMENT "Deploying test_plugin into plugins/ next to chartviewer"
    VERBATIM
)
