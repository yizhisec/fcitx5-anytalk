cmake_minimum_required(VERSION 3.10)
project(fcitx5-anytalk VERSION 0.2.0)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer system prefix and multiarch libdir when not explicitly set by user.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Default install prefix" FORCE)
endif()
if(CMAKE_INSTALL_LIBDIR STREQUAL "lib" AND DEFINED CMAKE_LIBRARY_ARCHITECTURE)
    set(CMAKE_INSTALL_LIBDIR "lib/${CMAKE_LIBRARY_ARCHITECTURE}" CACHE STRING "Default libdir" FORCE)
endif()

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)

# Zig build
find_program(ZIG_EXECUTABLE zig REQUIRED)

set(ZIG_LIB_DIR "${CMAKE_SOURCE_DIR}/anytalk-lib")
set(ZIG_OUTPUT_DIR "${ZIG_LIB_DIR}/zig-out/lib")
set(ZIG_LIB_FILE "${ZIG_OUTPUT_DIR}/libanytalk.so")

add_custom_command(
    OUTPUT ${ZIG_LIB_FILE}
    COMMAND ${ZIG_EXECUTABLE} build -Doptimize=ReleaseFast
    WORKING_DIRECTORY ${ZIG_LIB_DIR}
    COMMENT "Building Zig library (libanytalk.so)"
    DEPENDS
        ${ZIG_LIB_DIR}/src/api.zig
        ${ZIG_LIB_DIR}/src/context.zig
        ${ZIG_LIB_DIR}/src/audio.zig
        ${ZIG_LIB_DIR}/src/websocket.zig
        ${ZIG_LIB_DIR}/src/tls.zig
        ${ZIG_LIB_DIR}/src/protocol.zig
        ${ZIG_LIB_DIR}/src/asr.zig
        ${ZIG_LIB_DIR}/src/json.zig
        ${ZIG_LIB_DIR}/src/uuid.zig
        ${ZIG_LIB_DIR}/build.zig
)

add_custom_target(anytalk-zig-build ALL DEPENDS ${ZIG_LIB_FILE})

add_library(anytalk MODULE
    src/addon.cpp
)

add_dependencies(anytalk anytalk-zig-build)

target_include_directories(anytalk PRIVATE
    ${Fcitx5Core_INCLUDE_DIRS}
    ${Fcitx5Utils_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/anytalk-lib/include
)

target_link_libraries(anytalk
    Fcitx5::Core
    Fcitx5::Utils
    ${ZIG_LIB_FILE}
)

set_target_properties(anytalk PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/fcitx5"
    INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# Explicit list of icon files (no GLOB)
set(ICON_FILES
    ${CMAKE_SOURCE_DIR}/data/anytalk.png
    ${CMAKE_SOURCE_DIR}/data/anytalk-rec.png
)

# Install rules using GNUInstallDirs
install(TARGETS anytalk DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5)
install(FILES data/anytalk.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod)
install(FILES data/anytalk-addon.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon RENAME anytalk.conf)
install(FILES ${ICON_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/icons)
install(FILES ${ICON_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps)
install(FILES ${ZIG_LIB_FILE} DESTINATION ${CMAKE_INSTALL_LIBDIR})
