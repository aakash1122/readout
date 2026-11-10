find_package(GTest REQUIRED)

add_executable(readout_tests
    unit/test_text.cpp
    unit/test_parsers.cpp
    unit/test_modules.cpp
)
target_link_libraries(readout_tests PRIVATE readout_core readout_json GTest::gtest GTest::gtest_main)
target_compile_definitions(readout_tests PRIVATE READOUT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_target_properties(readout_tests PROPERTIES CXX_STANDARD 23)

# Unit tests create D-Bus messages, which needs a bus. dbus-run-session provides a private one.
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME unit COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:readout_tests>)

# End-to-end test against the real daemon on a private bus, including D-Bus activation.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(READOUT_TEST_SERVICES ${CMAKE_CURRENT_BINARY_DIR}/services)
file(GENERATE OUTPUT ${READOUT_TEST_SERVICES}/dbus-1/services/${READOUT_BUS_NAME}.service
    CONTENT "[D-BUS Service]\nName=${READOUT_BUS_NAME}\nExec=$<TARGET_FILE:readoutd> --idle-timeout=2\n")
add_test(NAME integration
    COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/integration_test.py
            --daemon $<TARGET_FILE:readoutd> --ctl $<TARGET_FILE:readoutctl>)
set_tests_properties(integration PROPERTIES
    ENVIRONMENT "XDG_DATA_DIRS=${READOUT_TEST_SERVICES}:/usr/local/share:/usr/share"
    TIMEOUT 60)

if(READOUT_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "READOUT_FUZZ needs Clang")
    endif()
    add_executable(fuzz_parsers fuzz/fuzz_parsers.cpp
        ../src/common/text.cpp ../src/parsers/kernel_text.cpp)
    target_include_directories(fuzz_parsers PRIVATE ../src)
    set_target_properties(fuzz_parsers PROPERTIES CXX_STANDARD 23)
    target_compile_options(fuzz_parsers PRIVATE -fsanitize=fuzzer,address,undefined -g)
    target_link_options(fuzz_parsers PRIVATE -fsanitize=fuzzer,address,undefined)
    add_test(NAME fuzz COMMAND fuzz_parsers -max_total_time=20 -rss_limit_mb=1024
        ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
endif()
