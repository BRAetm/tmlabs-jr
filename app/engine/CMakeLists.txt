add_executable(LabsEngine WIN32
    main.cpp
    LabsMainWindow.h
    LabsMainWindow.cpp
    LabsBackgroundWidget.h
    LabsLogoWidget.h
    LabsTheme.h
    LabsTheme.cpp
    LabsThemeDialog.h
    LabsThemeDialog.cpp
    ControllerMonitorWidget.h
    ControllerMonitorWidget.cpp
    resources/labs.qrc
    resources/labs.rc
)

target_link_libraries(LabsEngine PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    LabsCore
)

set_target_properties(LabsEngine PROPERTIES
    OUTPUT_NAME "LabsEngine"
    WIN32_EXECUTABLE TRUE
)
