# KDE Plasma 6 widget.
find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

set(KDE_COMPILERSETTINGS_LEVEL 6.0)
include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
# KDECMakeSettings is not used: it fetches translations from l10n.kde.org, which does not apply here.
set(CMAKE_AUTOMOC ON)
# The applet lands in build/bin/plasma/applets, so QT_PLUGIN_PATH=build/bin runs it uninstalled.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick DBus)
find_package(KF6 6.0 REQUIRED COMPONENTS I18n CoreAddons Config)
# 6.4 added plasma_add_applet, which builds the widget below.
find_package(Plasma 6.4 REQUIRED)

# Optional at build time; the widget imports them from QML at run time.
find_package(KF6KirigamiPlatform 6.0)
find_package(KF6QuickCharts 6.0)
find_package(KF6Notifications 6.0)
set_package_properties(KF6QuickCharts PROPERTIES TYPE RUNTIME PURPOSE "Charts in the panel and popups")
set_package_properties(KF6Notifications PROPERTIES TYPE RUNTIME PURPOSE "Threshold alerts")

set(READOUT_APPLET_ID io.github.aakash1122.readout)

# Every config page must declare every key of main.xml (Plasma passes all of them to each page),
# so the shared base page is generated from main.xml instead of being kept in sync by hand.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/package/main.xml READOUT_CONFIG_XML)
string(REGEX MATCHALL "<entry name=\"[A-Za-z0-9]+\" type=\"[A-Za-z]+\"" READOUT_CONFIG_ENTRIES "${READOUT_CONFIG_XML}")
set(READOUT_CONFIG_PROPERTIES "")
foreach(entry IN LISTS READOUT_CONFIG_ENTRIES)
    string(REGEX REPLACE "<entry name=\"([A-Za-z0-9]+)\" type=\"([A-Za-z]+)\"" "\\1;\\2" parts "${entry}")
    list(GET parts 0 key)
    list(GET parts 1 kind)
    if(kind STREQUAL "Bool")
        set(qml_type bool)
    elseif(kind STREQUAL "Int")
        set(qml_type int)
    elseif(kind STREQUAL "String")
        set(qml_type string)
    else()
        set(qml_type var)
    endif()
    string(APPEND READOUT_CONFIG_PROPERTIES "    property ${qml_type} cfg_${key}\n    property ${qml_type} cfg_${key}Default\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/main.xml)
configure_file(package/ConfigPage.qml.in ${CMAKE_CURRENT_BINARY_DIR}/ConfigPage.qml @ONLY)

plasma_add_applet(${READOUT_APPLET_ID}
    QML_SOURCES
        package/main.qml
        package/AppletContext.qml
        package/Logic.js
        package/Summary.qml
        package/Alerts.qml
        package/CompactRepresentation.qml
        package/ModuleWidget.qml
        package/ModuleState.qml
        package/PanelText.qml
        package/TextWidget.qml
        package/MiniWidget.qml
        package/LineWidget.qml
        package/BarsWidget.qml
        package/PieWidget.qml
        package/TachometerWidget.qml
        package/SpeedWidget.qml
        package/DualLineWidget.qml
        package/StateWidget.qml
        package/StackWidget.qml
        package/BatteryWidget.qml
        package/MemoryWidget.qml
        package/VerticalLabel.qml
        package/FullRepresentation.qml
        package/Appearance.qml
        package/ModulePage.qml
        package/ModuleSummary.qml
        package/PageLayout.qml
        package/Dashboard.qml
        package/StatsText.qml
        package/Separator.qml
        package/InfoRow.qml
        package/EmptyMessage.qml
        package/Well.qml
        package/Card.qml
        package/LineHistory.qml
        package/MirroredChart.qml
        package/Ring.qml
        package/PressureGauge.qml
        package/LedBars.qml
        package/CapacityBar.qml
        package/ConnectivityGrid.qml
        package/BatteryGlyph.qml
        package/AnalogClock.qml
        package/CalendarCard.qml
        package/ProcessList.qml
        package/CpuPage.qml
        package/MemoryPage.qml
        package/DiskPage.qml
        package/NetworkPage.qml
        package/GpuGauge.qml
        package/GpuPage.qml
        package/SensorsPage.qml
        package/BatteryPage.qml
        package/BluetoothPage.qml
        package/ClockPage.qml
        package/config.qml
        ${CMAKE_CURRENT_BINARY_DIR}/ConfigPage.qml
        package/ConfigGeneral.qml
        package/ConfigAlerts.qml
        package/AlertRow.qml
    RESOURCES
        package/main.xml
    CPP_SOURCES
        src/readoutclient.cpp
        src/readoutsource.cpp
        src/formatter.cpp
        src/worldclock.cpp
        src/settings.cpp
        src/systemmonitor.cpp
        src/historyseries.cpp
    GENERATE_APPLET_CLASS
)

target_include_directories(${READOUT_APPLET_ID} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(${READOUT_APPLET_ID} PRIVATE
    READOUT_BUS_NAME="${READOUT_BUS_NAME}"
    READOUT_OBJECT_PATH="${READOUT_OBJECT_PATH}"
    TRANSLATION_DOMAIN="plasma_applet_${READOUT_APPLET_ID}"
)
target_compile_options(${READOUT_APPLET_ID} PRIVATE $<$<BOOL:${READOUT_WERROR}>:-Werror>)
target_link_libraries(${READOUT_APPLET_ID} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    KF6::I18n
    KF6::CoreAddons
    KF6::ConfigCore
)

install(FILES readout.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME plasma_qmllint COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/lint-qml.py ${CMAKE_BINARY_DIR})
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/tools/plasma-preview ${CMAKE_CURRENT_BINARY_DIR}/plasma-preview)
