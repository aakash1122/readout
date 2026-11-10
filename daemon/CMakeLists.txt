set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET GLOBAL libsystemd>=249)

set(READOUT_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wnull-dereference
    -Wformat=2 -Wimplicit-fallthrough)
if(READOUT_WERROR)
    list(APPEND READOUT_WARNINGS -Werror)
endif()

# JSON conversion of D-Bus messages, for readoutctl and the tests.
add_library(readout_json STATIC src/bus/json_dump.cpp)
target_include_directories(readout_json PUBLIC src)
target_compile_options(readout_json PRIVATE ${READOUT_WARNINGS})
target_link_libraries(readout_json PUBLIC PkgConfig::SYSTEMD)

# Everything except main.cpp, shared with the tests.
add_library(readout_core STATIC
    src/bus/bus_util.cpp
    src/bus/dict_writer.cpp
    src/bus/service.cpp
    src/common/log.cpp
    src/common/text.cpp
    src/io/held_file.cpp
    src/io/sysfs.cpp
    src/modules/battery.cpp
    src/modules/bluetooth.cpp
    src/modules/cpu.cpp
    src/modules/disk.cpp
    src/modules/gpu.cpp
    src/modules/memory.cpp
    src/modules/network.cpp
    src/modules/process_table.cpp
    src/modules/sensors.cpp
    src/parsers/kernel_text.cpp
)
target_include_directories(readout_core PUBLIC src)
target_compile_definitions(readout_core PUBLIC
    READOUT_VERSION="${PROJECT_VERSION}"
    READOUT_BUS_NAME="${READOUT_BUS_NAME}"
    READOUT_OBJECT_PATH="${READOUT_OBJECT_PATH}"
    _GNU_SOURCE
)
target_compile_options(readout_core PRIVATE ${READOUT_WARNINGS} -fno-exceptions -fno-rtti
    -ffunction-sections -fdata-sections)
target_link_libraries(readout_core PUBLIC PkgConfig::SYSTEMD ${CMAKE_DL_LIBS})

add_executable(readoutd src/main.cpp)
target_compile_options(readoutd PRIVATE ${READOUT_WARNINGS} -fno-exceptions -fno-rtti)
target_link_libraries(readoutd PRIVATE readout_core)
target_link_options(readoutd PRIVATE -Wl,--gc-sections -Wl,--as-needed -Wl,-O1 -Wl,-z,relro,-z,now)

# readoutd uses a tiny part of libstdc++. Linking it statically (with --gc-sections) keeps only that part
# and avoids mapping the whole shared library (about 1.6 MiB of RSS, plus libm). Packagers whose policy
# forbids static libstdc++ can turn this off.
option(READOUT_STATIC_LIBSTDCXX "Link libstdc++ statically into readoutd when the static library exists" ON)
if(READOUT_STATIC_LIBSTDCXX)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-static-libstdc++" READOUT_HAVE_STATIC_LIBSTDCXX)
    if(READOUT_HAVE_STATIC_LIBSTDCXX)
        target_link_options(readoutd PRIVATE -static-libstdc++ -static-libgcc)
    else()
        message(STATUS "Static libstdc++ not found (Fedora: libstdc++-static); readoutd links it dynamically")
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT READOUT_IPO_SUPPORTED OUTPUT READOUT_IPO_MESSAGE LANGUAGES CXX)
if(READOUT_IPO_SUPPORTED AND NOT READOUT_SANITIZE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_property(TARGET readout_core readoutd PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

install(TARGETS readoutd DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

# D-Bus activation and the systemd user unit.
set(READOUT_DAEMON_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/readoutd")
configure_file(data/readoutd.service.in ${CMAKE_CURRENT_BINARY_DIR}/readoutd.service @ONLY)
configure_file(data/dbus.service.in ${CMAKE_CURRENT_BINARY_DIR}/${READOUT_BUS_NAME}.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/readoutd.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${READOUT_BUS_NAME}.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
# The Readout app icon (color and symbolic), shared by both frontends and notifications.
install(FILES data/icons/io.github.aakash1122.readout.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/icons/io.github.aakash1122.readout-symbolic.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/symbolic/apps)
install(FILES data/${READOUT_BUS_NAME}.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/interfaces)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
