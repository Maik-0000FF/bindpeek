# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_sources test_sources.cpp)
# Qt6::Network for the fake compositor the Hyprland backend is asked through.
target_link_libraries(test_sources PRIVATE bindpeek_sources Qt6::Network
                                           Qt6::Test)

# The sample files live in the source tree; the path is handed to the test so
# that it runs from any build directory.
target_compile_definitions(test_sources
  PRIVATE BINDPEEK_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/samples"
)

add_test(NAME sources COMMAND test_sources)

# Reads the QML and the controller header as text, so it needs neither a
# window nor a running session.
add_executable(test_qml_contract test_qml_contract.cpp)
target_link_libraries(test_qml_contract PRIVATE bindpeek_sources Qt6::Test)
target_compile_definitions(test_qml_contract
  PRIVATE BINDPEEK_SRC="${CMAKE_SOURCE_DIR}/src"
)

add_test(NAME qml_contract COMMAND test_qml_contract)

add_executable(test_settings test_settings.cpp)
target_link_libraries(test_settings PRIVATE bindpeek_sources Qt6::Test)

add_test(NAME settings COMMAND test_settings)

add_executable(test_compositor test_compositor.cpp)
target_link_libraries(test_compositor PRIVATE bindpeek_sources Qt6::Network
                                            Qt6::Test)

add_test(NAME compositor COMMAND test_compositor)

# The controller is the second consumer of groupBinds and lives in the
# executable, where no other test target reaches it. Its source is compiled in
# here together with the keyboard watch it holds a pointer to, which is what
# brings libevdev along; no device is opened, the watch is only constructed and
# used to deliver a signal.
add_executable(test_overlay
  test_overlay.cpp
  ${CMAKE_SOURCE_DIR}/src/OverlayController.cpp
  ${CMAKE_SOURCE_DIR}/src/KeyboardWatch.cpp
)
target_link_libraries(test_overlay PRIVATE bindpeek_sources Qt6::Test
                                           PkgConfig::Libevdev)

add_test(NAME overlay COMMAND test_overlay)

# The text output is written by main.cpp, which nothing links against, so the
# program itself is run. Not a duplicate of the grouping unit tests: those
# cover the function, this covers that the output actually goes through it.
# The duplicate heading lived here and only here, and this sample is the shape
# that produced it, a submap interrupting the entries around it.
#
# The assertion counts repetitions instead of looking for a particular word, so
# it holds in every language. Pinning the language was tried and does not work:
# Qt picks its catalogue through QLocale::uiLanguages(), which weighs LANGUAGE
# above LC_ALL, and a German session would then fail this test for no reason
# that has anything to do with the code.
#
# Headings are the lines that carry no indent, minus the summary on the first
# line. At least two are expected, or a run that printed nothing would pass.
#
# Two things about the shell it runs in. bash rather than sh, and pipefail with
# it, because the status of a pipeline is otherwise the status of its last
# command alone: a program that dies half way through hands what it managed to
# print to the awk, which is satisfied by it and reports success. Under a
# sanitizer that is precisely the run that has to fail. Not sh, because the
# shell that name points at on several distributions has no pipefail.
#
# And nothing is silenced. The program writes its notes to the error stream,
# which the awk never reads, so quieting it buys nothing and costs the one
# thing that matters when something goes wrong: the report is written to
# exactly that stream, and a run that throws it away fails with nothing to say
# why.
add_test(NAME list_groups_once COMMAND bash -c
  "set -o pipefail; $<TARGET_FILE:bindpeek> --list --environment hyprland --source '${CMAKE_CURRENT_SOURCE_DIR}/samples/hyprland-binds.json' | awk 'NR > 1 && NF && $0 !~ /^  / { seen[$0]++; n++ } END { if (n < 2) exit 1; for (h in seen) if (seen[h] != 1) exit 1 }'")

# The bound the panel is measured against lives in Appearance, which no other
# test target reaches: it is compiled into both executables rather than into
# the shared library, because it needs the GUI classes the library does not
# link. Its sources are compiled in here for that reason, and the scheme reader
# it holds a pointer to comes along; nothing here creates either object, the
# rule under test takes plain sizes.
add_executable(test_appearance
  test_appearance.cpp
  ${CMAKE_SOURCE_DIR}/src/Appearance.cpp
  ${CMAKE_SOURCE_DIR}/src/SystemScheme.cpp
)
target_link_libraries(test_appearance PRIVATE bindpeek_sources Qt6::Gui
                                              Qt6::DBus Qt6::Test)

add_test(NAME appearance COMMAND test_appearance)

# The rule that waits for a tray lives with the editor, which no other test
# target reaches, so its source is compiled in here. It needs neither a tray
# nor a bus nor a window: the question it asks is handed to it, which is why it
# was pulled out of main.cpp in the first place.
add_executable(test_tray_wait
  test_tray_wait.cpp
  ${CMAKE_SOURCE_DIR}/src/editor/TrayWait.cpp
)
target_include_directories(test_tray_wait
  PRIVATE ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(test_tray_wait PRIVATE Qt6::Test)

add_test(NAME tray_wait COMMAND test_tray_wait)

# The panel itself, instantiated and measured. Appearance and the scheme reader
# it holds are compiled in for the same reason as above: they are not in the
# shared library. Quick comes along because this one needs a window, which is
# why it is also the only test told to use the offscreen platform: it must run
# where there is no display, and asking for one would fail the whole suite.
add_executable(test_panel_layout
  test_panel_layout.cpp
  ${CMAKE_SOURCE_DIR}/src/Appearance.cpp
  ${CMAKE_SOURCE_DIR}/src/SystemScheme.cpp
)
target_link_libraries(test_panel_layout PRIVATE bindpeek_sources Qt6::Gui
                                                Qt6::DBus Qt6::Qml Qt6::Quick
                                                Qt6::Test)
# The QML is read from the source tree, so the test runs from any build
# directory. Same path the contract test is handed.
target_compile_definitions(test_panel_layout
  PRIVATE BINDPEEK_SRC="${CMAKE_SOURCE_DIR}/src"
)

add_test(NAME panel_layout COMMAND test_panel_layout)
# A ceiling of its own, because this one waits rather than computes.
#
# Every wait in it gives a fitting a minute before calling it stuck, and twelve
# of them run: ten written, and two of those run twice over, once per row of
# the case that is fed a table. A run in which nothing ever rests therefore
# takes twelve minutes and then says which fitting hung, which is the useful
# half of the twenty-five the default would let it sit there.
#
# So the ceiling is above that sum rather than on it. Cut at twelve minutes,
# the worst run would die in the middle of the last wait and print nothing at
# all, which is the one outcome this is meant to prevent. A passing run costs
# eight seconds here and a small multiple of that under a sanitizer.
set_tests_properties(panel_layout PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 900
)
