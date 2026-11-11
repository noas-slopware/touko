# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.21)

# The name lives here and nowhere else: config.h, the desktop file, the
# QML module and the binary all come from these.
set(APP_NAME "Touko")
set(APP_BINARY "touko")
set(APP_ID "codes.noa.Touko")
project(${APP_BINARY} VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Everything it links lives in /usr/lib.
set(CMAKE_SKIP_INSTALL_RPATH ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 DBus)
# Runtime QML import only, but a missing Kirigami should fail here, not at launch.
find_package(KF6Kirigami 6.0 REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)
# libghostty-vt has struct fields named "emit".
add_compile_definitions(QT_NO_KEYWORDS)

# libghostty-vt ships with Arch's ghostty package. Its .pc file carries the
# build root as prefix, so look the files up directly.
find_path(GHOSTTY_VT_INCLUDE_DIR ghostty/vt.h REQUIRED)
find_library(GHOSTTY_VT_LIBRARY NAMES ghostty-vt REQUIRED)
add_library(ghostty_vt UNKNOWN IMPORTED)
set_target_properties(ghostty_vt PROPERTIES
  IMPORTED_LOCATION "${GHOSTTY_VT_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${GHOSTTY_VT_INCLUDE_DIR}")

configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

set(CORE_SOURCES
  src/ptyprocess.cpp src/ptyprocess.h
  src/shellenv.cpp src/shellenv.h
  src/theme.cpp src/theme.h
  src/terminalsession.cpp src/terminalsession.h
)

qt_add_executable(${APP_BINARY}
  src/main.cpp
  ${CORE_SOURCES}
  src/terminalitem.cpp src/terminalitem.h
  src/sessionmodel.cpp src/sessionmodel.h
  src/keymap.cpp src/keymap.h
  src/boxdrawing.cpp src/boxdrawing.h
  src/notifier.cpp src/notifier.h
)
target_include_directories(${APP_BINARY} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} src)

qt_add_qml_module(${APP_BINARY}
  URI codes.noa.touko
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/TerminalPage.qml
    qml/ExtraKeys.qml
    qml/SettingsPage.qml
    qml/ThemePicker.qml
  NO_RESOURCE_TARGET_PATH
  # Shared Qt and Kirigami: nothing to link statically, and the scan only
  # complains that Kirigami's plugins are not static targets.
  NO_IMPORT_SCAN
)

target_link_libraries(${APP_BINARY} PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::DBus ghostty_vt util)

include(GNUInstallDirs)
install(TARGETS ${APP_BINARY} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(data/app.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop @ONLY)
configure_file(data/app.metainfo.xml.in ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.metainfo.xml @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME ${APP_ID}.svg)

option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  qt_add_executable(test_session tests/test_session.cpp ${CORE_SOURCES})
  target_include_directories(test_session PRIVATE ${CMAKE_CURRENT_BINARY_DIR} src)
  target_link_libraries(test_session PRIVATE Qt6::Gui Qt6::Qml Qt6::Test ghostty_vt util)
  set_target_properties(test_session PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
  add_test(NAME session COMMAND test_session)

  qt_add_executable(test_item tests/test_item.cpp ${CORE_SOURCES}
    src/terminalitem.cpp src/terminalitem.h src/keymap.cpp src/keymap.h src/boxdrawing.cpp src/boxdrawing.h)
  target_include_directories(test_item PRIVATE ${CMAKE_CURRENT_BINARY_DIR} src)
  target_link_libraries(test_item PRIVATE Qt6::Quick Qt6::Test ghostty_vt util)
  set_target_properties(test_item PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
  add_test(NAME item COMMAND test_item)
  set_tests_properties(item PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
