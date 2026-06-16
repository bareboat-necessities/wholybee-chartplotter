# Signal K Plugin — built as a separate Qt MODULE library, loaded by the host
# at runtime via QPluginLoader. Mirrors plugins/test_plugin/CMakeLists.txt, but
# also links Qt6::WebSockets (for the ws:// transport) and Qt6::Network.

add_library(signalk_plugin MODULE
    signalk_plugin.hpp
    signalk_plugin.cpp
    signalk_plugin_factory.cpp
    signalk_client.hpp
    signalk_client.cpp
    signalk_decoder.hpp
    signalk_decoder.cpp
    signalk_plugin.json
)

set_target_properties(signalk_plugin PROPERTIES
    AUTOMOC ON
    # Stable basename so it never collides with the host or a third-party plugin
    # in the same plugins/ drop folder.
    OUTPUT_NAME "chartplotter_signalk_plugin"
    PREFIX ""           # no "lib" prefix on non-Windows MODULE libs
)

# Plugins consume the public chartplotter SDK headers and a small handful of
# core headers; they link against the host exe for those symbols (ENABLE_EXPORTS
# on chartviewer).
target_include_directories(signalk_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(signalk_plugin PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    chartviewer        # host symbols (NavDataStore, theme, TouchSpinBox, ...)
)

# Deploy the built plugin next to the host exe so QPluginLoader finds it via the
# well-known plugins/ subfolder.
add_custom_command(TARGET signalk_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:chartviewer>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:signalk_plugin>"
            "$<TARGET_FILE_DIR:chartviewer>/plugins/"
    COMMENT "Deploying signalk_plugin into plugins/ next to chartviewer"
    VERBATIM
)

# windeployqt is invoked against chartviewer.exe, which doesn't link Qt6Network /
# Qt6WebSockets directly (only this plugin does), so those DLLs are not copied
# automatically. Drop them next to the exe ourselves so QPluginLoader can resolve
# the plugin's imports at runtime.
add_custom_command(TARGET signalk_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::WebSockets>"
            "$<TARGET_FILE_DIR:chartviewer>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Network>"
            "$<TARGET_FILE_DIR:chartviewer>"
    COMMENT "Copying Qt6WebSockets/Qt6Network DLLs next to chartviewer"
    VERBATIM
)
