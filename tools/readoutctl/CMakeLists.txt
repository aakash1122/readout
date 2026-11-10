add_executable(readoutctl readoutctl.cpp)
set_target_properties(readoutctl PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(readoutctl PRIVATE
    READOUT_BUS_NAME="${READOUT_BUS_NAME}"
    READOUT_OBJECT_PATH="${READOUT_OBJECT_PATH}")
target_compile_options(readoutctl PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${READOUT_WERROR}>:-Werror>)
target_link_libraries(readoutctl PRIVATE readout_json)
install(TARGETS readoutctl DESTINATION ${CMAKE_INSTALL_BINDIR})
