# Development tool, not installed: renders the Plasma widget's QML to PNG files.
add_executable(plasma-preview main.cpp)
target_link_libraries(plasma-preview PRIVATE Qt6::Gui Qt6::Quick Qt6::Qml KF6::I18n KF6::I18nQml io.github.aakash1122.readout)
