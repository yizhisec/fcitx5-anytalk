cmake_minimum_required(VERSION 3.10)
project(fcitx5-anytalk VERSION 0.2.0)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version information
set(ANYTALK_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(ANYTALK_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(ANYTALK_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(ANYTALK_VERSION "${PROJECT_VERSION}")

# Build options
option(BUILD_DAEMON "Build Rust daemon" ON)
option(DAEMON_DEBUG "Build daemon in debug mode" OFF)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)

if(BUILD_DAEMON)
    find_program(CARGO_EXECUTABLE cargo)
    if(NOT CARGO_EXECUTABLE)
        message(FATAL_ERROR "cargo not found")
    endif()

    if(DAEMON_DEBUG)
        set(CARGO_BUILD_ARGS "build")
        set(DAEMON_BUILD_DIR "debug")
    else()
        set(CARGO_BUILD_ARGS "build" "--release")
        set(DAEMON_BUILD_DIR "release")
    endif()

    add_custom_target(anytalk-daemon-build ALL
        COMMAND ${CARGO_EXECUTABLE} ${CARGO_BUILD_ARGS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/anytalk-daemon
        COMMENT "Building Rust daemon (${DAEMON_BUILD_DIR})"
    )
endif()

add_library(anytalk MODULE
    src/addon.cpp
    src/ipc_client.cpp
    src/daemon_manager.cpp
)

if(BUILD_DAEMON)
    add_dependencies(anytalk anytalk-daemon-build)
endif()

target_include_directories(anytalk PRIVATE
    ${Fcitx5Core_INCLUDE_DIRS}
    ${Fcitx5Utils_INCLUDE_DIRS}
    ${JSONC_INCLUDE_DIRS}
)

target_link_libraries(anytalk
    Fcitx5::Core
    Fcitx5::Utils
    ${JSONC_LIBRARIES}
)

set_target_properties(anytalk PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/fcitx5"
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

if(BUILD_DAEMON)
    install(PROGRAMS anytalk-daemon/target/${DAEMON_BUILD_DIR}/anytalk-daemon
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
