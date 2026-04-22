add_library(LabsCore SHARED
    LabsCore.h
    FrameTypes.h
    InputTypes.h
    IPlugin.h
    PluginHost.h
    PluginHost.cpp
    SettingsManager.h
    SettingsManager.cpp
)

target_compile_definitions(LabsCore PRIVATE LABSCORE_EXPORTS)

target_include_directories(LabsCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(LabsCore PUBLIC
    Qt6::Core
)

set_target_properties(LabsCore PROPERTIES
    OUTPUT_NAME "LabsCore"
)
