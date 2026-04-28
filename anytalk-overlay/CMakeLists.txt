cmake_minimum_required(VERSION 3.16)

# Building this directory standalone is supported. When pulled in via
# add_subdirectory() from the root project, the parent's project() applies
# and we just register the target.
if(NOT PROJECT_NAME)
    project(anytalk-overlay LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent)

# LayerShellQt is optional — required for proper centering on Wayland
# (KDE/Sway/wlroots). Falls back to standard toplevel when missing.
find_package(LayerShellQt QUIET)

add_executable(anytalk-overlay
    src/main.cpp
    src/Theme.h
    src/Config.h
    src/Config.cpp
    src/AsrController.h
    src/AsrController.cpp
    src/OverlayService.h
    src/OverlayService.cpp
    src/OverlayWindow.h
    src/OverlayWindow.cpp
    src/AuroraBars.h
    src/AuroraBars.cpp
    src/StatusDot.h
    src/StatusDot.cpp
)

# Pull libanytalk.so (Zig ASR engine) into the overlay.
set(_zig_lib "${CMAKE_SOURCE_DIR}/anytalk-lib/zig-out/lib/libanytalk.so")
target_include_directories(anytalk-overlay PRIVATE
    ${CMAKE_SOURCE_DIR}/anytalk-lib/include)
target_link_libraries(anytalk-overlay PRIVATE ${_zig_lib})
set_target_properties(anytalk-overlay PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
    BUILD_WITH_INSTALL_RPATH TRUE)

target_link_libraries(anytalk-overlay PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Concurrent
)

if(LayerShellQt_FOUND)
    target_link_libraries(anytalk-overlay PRIVATE LayerShellQtInterface)
    message(STATUS "anytalk-overlay: LayerShellQt found — Wayland native centering enabled")
else()
    message(STATUS "anytalk-overlay: LayerShellQt not found — Wayland will use compositor placement")
endif()

include(GNUInstallDirs)
install(TARGETS anytalk-overlay DESTINATION ${CMAKE_INSTALL_BINDIR})
