cmake_minimum_required(VERSION 3.21)
project(kern_qt LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

set(BRIDGE_LIBRARY "${CMAKE_BINARY_DIR}/bridge/release/libkern_qt_bridge.a")
add_custom_command(
    OUTPUT "${BRIDGE_LIBRARY}"
    COMMAND cargo build --release --locked
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/bridge/Cargo.toml"
            --target-dir "${CMAKE_BINARY_DIR}/bridge"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bridge/src/lib.rs"
            "${CMAKE_CURRENT_SOURCE_DIR}/bridge/Cargo.toml"
            "${CMAKE_CURRENT_SOURCE_DIR}/bridge/Cargo.lock"
            "${CMAKE_CURRENT_SOURCE_DIR}/../core/src/table.rs"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)
add_custom_target(kern_qt_bridge DEPENDS "${BRIDGE_LIBRARY}")

# Shared by the app and the development checks.
add_library(kern_shared OBJECT controller.cpp controller.h theme.cpp theme.h qml.qrc)
target_link_libraries(kern_shared PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
add_dependencies(kern_shared kern_qt_bridge)

add_executable(kern main.cpp)
target_link_libraries(kern PRIVATE kern_shared "${BRIDGE_LIBRARY}" dl pthread m)

# Development checks (see README). Not installed. The target is excluded from the default
# build; build it with `cmake --build <dir> --target kern-checks`.
add_executable(kern-checks EXCLUDE_FROM_ALL main.cpp tests/checks.cpp tests/checks.h)
target_link_libraries(kern-checks PRIVATE kern_shared "${BRIDGE_LIBRARY}" dl pthread m)
target_compile_definitions(kern-checks PRIVATE KERN_WITH_CHECKS KERN_QT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

install(TARGETS kern RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.hverbruggen.kern.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.hverbruggen.kern.svg"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")
