cmake_minimum_required(VERSION 3.16)
project(fcitx5-anytalk VERSION 0.5.0)

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

# Optional Qt6 voice-activation overlay (independent process).
option(BUILD_OVERLAY "Build the Qt6 Aurora voice overlay" ON)
if(BUILD_OVERLAY)
    find_package(Qt6 6.4 QUIET COMPONENTS Core Gui Widgets DBus Concurrent WebSockets)
    if(NOT Qt6_FOUND)
        message(WARNING "Qt6 (Core/Gui/Widgets/DBus/Concurrent/WebSockets) not found "
                        "— overlay will NOT be built. Install qt6-base / qt6-websockets "
                        "or pass -DBUILD_OVERLAY=OFF to silence.")
        set(BUILD_OVERLAY OFF)
    endif()
endif()

# fcitx5 addon — thin IM integration layer (hotkey, preedit, commit). All
# heavy lifting (audio capture, ASR, TLS) lives in the overlay process.
add_library(anytalk MODULE src/addon.cpp)

target_include_directories(anytalk PRIVATE
    ${Fcitx5Core_INCLUDE_DIRS}
    ${Fcitx5Utils_INCLUDE_DIRS}
)

target_link_libraries(anytalk PRIVATE Fcitx5::Core Fcitx5::Utils)

set_target_properties(anytalk PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/fcitx5"
)

# Legacy icons (kept for compatibility with anything that hard-codes the old names).
set(LEGACY_ICONS
    ${CMAKE_SOURCE_DIR}/data/anytalk.png
    ${CMAKE_SOURCE_DIR}/data/anytalk-rec.png
)

# Icon C state set: idle / ready / connecting / recording.
set(ICON_STATES idle ready connecting recording)
set(ICON_PNG_SIZES 16 22 32 48)

# Install rules using GNUInstallDirs
install(TARGETS anytalk DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5)
install(FILES data/anytalk-addon.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon RENAME anytalk.conf)

# Legacy locations.
install(FILES ${LEGACY_ICONS} DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/icons)
install(FILES ${LEGACY_ICONS} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps)

# Multi-size PNGs into hicolor theme: <size>x<size>/apps/anytalk-<state>.png
foreach(size IN LISTS ICON_PNG_SIZES)
    set(_size_dir ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    foreach(state IN LISTS ICON_STATES)
        install(
            FILES ${CMAKE_SOURCE_DIR}/data/anytalk-${state}-${size}.png
            DESTINATION ${_size_dir}
            RENAME anytalk-${state}.png
        )
    endforeach()
endforeach()

# Scalable SVGs into hicolor/scalable/apps and into fcitx5/icons (used by status icon).
set(STATE_SVGS
    ${CMAKE_SOURCE_DIR}/data/anytalk-idle.svg
    ${CMAKE_SOURCE_DIR}/data/anytalk-ready.svg
    ${CMAKE_SOURCE_DIR}/data/anytalk-connecting.svg
    ${CMAKE_SOURCE_DIR}/data/anytalk-recording.svg
)
install(FILES ${STATE_SVGS} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${STATE_SVGS} DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/icons)

# Sample Waybar styling for the recording-state "breathing" effect.
install(FILES data/waybar-anytalk.css DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5-anytalk)

# Optional overlay process. Launched on demand via D-Bus session-bus activation,
# so no systemd unit / autostart entry is needed — installing the .service file
# below is enough.
if(BUILD_OVERLAY)
    add_subdirectory(anytalk-overlay)
    install(FILES data/org.fcitx.Fcitx5.AnyTalk.Overlay.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
endif()
