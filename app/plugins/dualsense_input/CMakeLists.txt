add_labs_plugin(DualSenseInputPlugin
    DualSensePlugin.h
    DualSensePlugin.cpp
)

# Windows HID + SetupAPI for HID enumeration. ViGEmClient.dll is loaded at
# runtime (no link-time dep — same pattern as ViGEmPlugin).
target_link_libraries(DualSenseInputPlugin PRIVATE
    setupapi
    hid
)
