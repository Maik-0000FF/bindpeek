# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(bindpeek VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# The test is a QObject; moc runs along automatically.
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

# The version lives here and nowhere else. Both programs get it as a compile
# definition and flake.nix reads it back out of this file, so a release means
# changing one number.
add_compile_definitions(BINDPEEK_VERSION="${PROJECT_VERSION}")

# The id of the desktop entry: the base name of the installed file and the
# application id the editor reports, so the window a compositor sees and the
# launcher a desktop shows are recognized as the same program. Defined once
# here, consumed by the install rule and by the editor.
set(BINDPEEK_DESKTOP_ID "bindpeek")
add_compile_definitions(BINDPEEK_DESKTOP_ID="${BINDPEEK_DESKTOP_ID}")

# The entry as it lives in this checkout, which is not the name it is installed
# under: the id above is what it becomes, this is where it comes from. Named
# once because the install rule and the icon name below both need it, and the
# install and uninstall scripts read the same file.
set(BINDPEEK_DESKTOP_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/assets/bindpeek.desktop)

# The name the installed icon file gets, taken out of the entry that asks for
# it. An entry names its icon by a key of its own, and that key and the file
# have to agree; reading the one out of the other is what keeps them agreeing.
# Neither follows from the id above, so neither can stand in for it.
file(STRINGS ${BINDPEEK_DESKTOP_SOURCE} _bindpeek_icon_line REGEX "^Icon=")
# Asked before the list is taken apart: list(GET) on an empty list is an error
# of its own and ends the configure before the sentence below can say which
# file was missing what.
if(NOT _bindpeek_icon_line)
  message(FATAL_ERROR "No Icon= key in ${BINDPEEK_DESKTOP_SOURCE}")
endif()
list(GET _bindpeek_icon_line 0 _bindpeek_icon_line)
string(REGEX REPLACE "^Icon=" "" BINDPEEK_ICON_NAME "${_bindpeek_icon_line}")
if(NOT BINDPEEK_ICON_NAME)
  message(FATAL_ERROR "The Icon= key in ${BINDPEEK_DESKTOP_SOURCE} is empty")
endif()

# 6.7 at least, and said here rather than found out in the middle of the build.
# Two things this uses arrived after 6.4: the light/dark setting Qt reports
# itself, and the form of qt_add_translations that takes more than one target.
# On an older Qt the second fails with a message about a target called TARGETS,
# which says nothing about what is actually wrong. This says it.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Network Qml Quick Widgets
                                  DBus LinguistTools)

# wlr-layer-shell binding for Qt. The overlay has to be a layer surface to get
# exclusive keyboard focus from the compositor.
find_package(LayerShellQt REQUIRED)

# libevdev ships only a pkg-config file, no CMake package.
find_package(PkgConfig REQUIRED)
pkg_check_modules(Libevdev REQUIRED IMPORTED_TARGET libevdev)

# KDE's own configuration library. It reads kglobalshortcutsrc exactly the way
# KDE writes it (double escaping of list commas and tabs); a hand-written
# parser would be guesswork here.
#
# Wanted, not required. It is needed by one backend out of three, and only on a
# desktop that has KDE Plasma 6 in the first place; a distribution that does not
# package the framework has no Plasma 6 to read either. Making the whole build
# depend on it would keep the panel off machines whose compositor it supports
# perfectly well, for want of a backend nobody there can use.
find_package(KF6Config QUIET)
if(KF6Config_FOUND)
  set(BINDPEEK_WITH_KDE ON)
  # Said as well, not only when the search comes up empty. The install script
  # tells the reader that this step names the outcome, and the case it says it
  # for is exactly this one: a framework already on the machine, under a name
  # no package list here carries. Quietly finding it would leave that reader
  # with nothing to read.
  message(STATUS "KF6 KConfig found, building with the KDE backend.")
else()
  set(BINDPEEK_WITH_KDE OFF)
  message(STATUS
    "KF6 KConfig not found, building without the KDE backend. "
    "Install kconfig (Arch), libkf6config-dev (Debian/Ubuntu), or "
    "kf6-kconfig-devel (Fedora/openSUSE) to read the shortcuts of a "
    "KDE Plasma session.")
endif()

add_subdirectory(src)

# Translation catalogs. lupdate refreshes the .ts files from the source tree,
# lrelease compiles them, and the .qm files are baked into the binary under the
# resource prefix main.cpp loads from. A language is added by adding a .ts file
# here, no code change.
qt_add_translations(
  TARGETS bindpeek bindpeek-editor
  # Named explicitly, see the list in src/CMakeLists.txt: lupdate builds its
  # project from the targets' sources, and QML is not one of them. Without this
  # a refresh finds no qsTr() at all and marks every interface string obsolete
  # in one go.
  SOURCES ${BINDPEEK_QML_SOURCES}
  TS_FILES translations/bindpeek_de.ts
  # A string whose source is gone is dropped rather than carried along as a
  # vanished entry. This target is the only thing that writes the catalogue, so
  # such an entry can never come back and is only noise in the next diff.
  #
  # Line numbers are left out for the same reason, one step further. They said
  # where a string stood, which moved whenever a line was inserted above it, so
  # the gate that holds the catalogue against the source failed for changes
  # that touched no string at all. Nothing reads them: a refresh matches on the
  # English sentence and so does every check. The price is that Linguist can no
  # longer jump to the place a string is written.
  #
  # The option has to be on every run, which holds for the same reason as the
  # first paragraph above: nothing else writes the catalogue. lupdate offers
  # to guess the setting from the file it is refreshing, but measured it does
  # not, and a refresh without it writes every line number straight back.
  LUPDATE_OPTIONS -no-obsolete -locations none
)

enable_testing()
add_subdirectory(tests)
