# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# The QML each program carries. Listed once and used twice: the resources bake
# it into the binaries, and the translation catalogue is refreshed from the
# same list. lupdate is fed a project built from the targets' sources, and QML
# reaches a binary as a resource rather than as a source, so without naming it
# here every qsTr() in the interface is invisible to it.
set(BINDPEEK_OVERLAY_QML
  Overlay.qml
  PanelBody.qml
  Theme.qml
)
# Theme.qml and PanelBody.qml are in both on purpose: the preview in the
# settings window is the overlay panel itself, drawn from the overlay tokens,
# not a second copy of either.
set(BINDPEEK_EDITOR_QML
  editor/Editor.qml
  editor/AboutDialog.qml
  editor/AlignIcon.qml
  editor/ConfirmDialog.qml
  editor/EditorTheme.qml
  editor/ThemedToolTip.qml
  PanelBody.qml
  Theme.qml
)

# The union, absolute, for the translations call in the parent directory.
set(_bindpeek_qml ${BINDPEEK_OVERLAY_QML} ${BINDPEEK_EDITOR_QML})
list(REMOVE_DUPLICATES _bindpeek_qml)
list(TRANSFORM _bindpeek_qml PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(BINDPEEK_QML_SOURCES ${_bindpeek_qml} PARENT_SCOPE)

# The C++ half of the same errand, exported at the end of this file once the
# three lists exist. Handed to the catalogue rather than left to be found: the
# refresh is driven from one target, and without this it would see that
# target's sources alone and drop every string of the other as gone.

# The backends live in a library of their own, so the tests can exercise them
# without a window and without a running session.
# Named once and used twice, like the QML above: the library is built from this
# list, and the translation catalogue is refreshed from it.
set(BINDPEEK_LIB_SOURCES
  Compositor.cpp
  Settings.cpp
  Source.cpp
  SourceMango.cpp
  SourceHyprland.cpp
)

add_library(bindpeek_sources STATIC ${BINDPEEK_LIB_SOURCES})

# The KDE backend and the library it needs, together or not at all. Anything
# that asks whether it is there asks this one definition, so a build without it
# cannot end up with the backend named somewhere and missing elsewhere.
if(BINDPEEK_WITH_KDE)
  list(APPEND BINDPEEK_LIB_SOURCES SourceKde.cpp)
  target_sources(bindpeek_sources PRIVATE SourceKde.cpp)
  target_link_libraries(bindpeek_sources PRIVATE KF6::ConfigCore)
  target_compile_definitions(bindpeek_sources PUBLIC BINDPEEK_WITH_KDE)
endif()

target_include_directories(bindpeek_sources PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bindpeek_sources
  PUBLIC Qt6::Core
  # QLocalSocket: the compositor check connects to the Wayland socket, and the
  # Hyprland backend asks the running instance over its IPC socket.
  PRIVATE Qt6::Network
)

set(BINDPEEK_PANEL_SOURCES
  main.cpp
  # For the one sentence that describes the program: --help and the about
  # dialog have to say the same thing.
  AppInfo.cpp
  Appearance.cpp
  KeyboardWatch.cpp
  LayerPlacement.cpp
  SystemScheme.cpp
  OverlayController.cpp
)

add_executable(bindpeek ${BINDPEEK_PANEL_SOURCES})

target_link_libraries(bindpeek PRIVATE
  bindpeek_sources
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  # The desktop portal is asked over D-Bus for the light/dark setting.
  Qt6::DBus
  # Turns the window into a wlr-layer-shell surface. Without it there is no
  # exclusive keyboard focus, and without that no modifier filter.
  LayerShellQt::Interface
  # Reads the keyboards under /dev/input without grabbing them.
  PkgConfig::Libevdev
)

# The QML lives in the binary, so the overlay needs no files at runtime.
qt_add_resources(bindpeek "qml"
  PREFIX "/qml"
  FILES ${BINDPEEK_OVERLAY_QML}
)

install(TARGETS bindpeek RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# --- Editor -----------------------------------------------------------------
#
# A second program rather than a mode of the first. It has to keep running when
# the overlay is off, because its tray icon is how the overlay gets switched
# back on.
set(BINDPEEK_EDITOR_SOURCES
  editor/main.cpp
  editor/SettingsModel.cpp
  editor/OverlayProcess.cpp
  editor/TrayWait.cpp
  # Appearance is shared with the overlay so the preview and the real panel are
  # driven by the same code.
  Appearance.cpp
  AppInfo.cpp
  SystemScheme.cpp
)

add_executable(bindpeek-editor ${BINDPEEK_EDITOR_SOURCES})

target_include_directories(bindpeek-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor)
target_link_libraries(bindpeek-editor PRIVATE
  bindpeek_sources
  Qt6::Gui
  # QLocalServer: a second start of the settings window is handed over to the
  # one already running rather than opening a second of everything.
  Qt6::Network
  Qt6::Qml
  Qt6::Quick
  # The desktop portal is asked over D-Bus for the light/dark setting.
  Qt6::DBus
  # QSystemTrayIcon lives in Widgets; the windows themselves stay Qt Quick.
  Qt6::Widgets
)


# No BASE here: the aliases then follow the paths as written, which gives
# /qml/editor/Editor.qml and /qml/Theme.qml. Editor.qml reaches its shared
# neighbour with an explicit import of the parent directory.
#
# Theme.qml comes from the parent directory on purpose: the preview renders
# with the overlay's own tokens, not with a copy of them.
qt_add_resources(bindpeek-editor "editor_qml"
  PREFIX "/qml"
  FILES ${BINDPEEK_EDITOR_QML}
)


# The mark is shown in the about dialog as well, so the icons resource is
# reachable from QML through the same prefix.
qt_add_resources(bindpeek-editor "editor_icons"
  PREFIX "/icons"
  BASE ${CMAKE_SOURCE_DIR}/assets
  FILES
    ${CMAKE_SOURCE_DIR}/assets/logo-mark-black.svg
    ${CMAKE_SOURCE_DIR}/assets/logo-mark-white.svg
)

install(TARGETS bindpeek-editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Desktop entry and icon for the editor. Without them the tray program can only
# be started from a shell, and no desktop can offer it for autostart: the XDG
# autostart mechanism works by copying exactly this file.
#
# The icon is the mark without the wordmark, which is the only variant that
# stays readable at menu size. It carries no background of its own, so a very
# dark panel shows it faintly; the tray icon solves that by switching between
# the two variants at runtime, which a static desktop icon cannot do.
install(FILES ${BINDPEEK_DESKTOP_SOURCE}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        RENAME ${BINDPEEK_DESKTOP_ID}.desktop)
install(FILES ${CMAKE_SOURCE_DIR}/assets/logo-mark-black.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME ${BINDPEEK_ICON_NAME}.svg)

# The C++ sources of both programs and of the library they share, absolute, for
# the translations call in the parent directory. Assembled from the very lists
# the targets are built from, so a file added to a program is a file the
# catalogue sees.
set(_bindpeek_cxx
  ${BINDPEEK_LIB_SOURCES}
  ${BINDPEEK_PANEL_SOURCES}
  ${BINDPEEK_EDITOR_SOURCES}
)
list(REMOVE_DUPLICATES _bindpeek_cxx)
list(TRANSFORM _bindpeek_cxx PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(BINDPEEK_CXX_SOURCES ${_bindpeek_cxx} PARENT_SCOPE)
