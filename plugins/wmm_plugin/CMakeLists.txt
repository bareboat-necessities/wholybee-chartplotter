# WMM Plugin — built as a separate Qt MODULE library, loaded by the host at
# runtime via QPluginLoader. Mirrors plugins/gpx_plugin/CMakeLists.txt. Needs no
# extra Qt modules beyond Widgets/Core (the public SDK header pulls Qt6::Gui),
# all already deployed alongside the host exe, so there are no extra runtime DLLs.

add_library(wmm_plugin MODULE
    wmm_plugin.hpp
    wmm_plugin.cpp
    wmm_plugin_factory.cpp
    wmm.hpp
    wmm.cpp
    wmm_plugin.json
)

set_target_properties(wmm_plugin PROPERTIES
    AUTOMOC ON
    OUTPUT_NAME "chartplotter_wmm_plugin"
    PREFIX ""           # no "lib" prefix on non-Windows MODULE libs
)

target_include_directories(wmm_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(wmm_plugin PRIVATE
    Qt6::Widgets
    Qt6::Core
    chartviewer        # host symbols (NavDataStore, INavDataPublisher, ...)
)

# Deploy next to the host exe so QPluginLoader finds it in the plugins/ subfolder.
add_custom_command(TARGET wmm_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:chartviewer>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:wmm_plugin>"
            "$<TARGET_FILE_DIR:chartviewer>/plugins/"
    COMMENT "Deploying wmm_plugin into plugins/ next to chartviewer"
    VERBATIM
)
