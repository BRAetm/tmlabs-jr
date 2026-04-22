add_labs_plugin(XInputPlugin
    XInputPlugin.h
    XInputPlugin.cpp
)

target_link_libraries(XInputPlugin PRIVATE
    Xinput9_1_0
)
