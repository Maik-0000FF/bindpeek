# shellcheck shell=bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# What the two programs are called and where they end up. Source it with
# $PROJECT_ROOT set and _style.sh already sourced; it sets no options of its
# own and it stops the script when the checkout it is given makes no sense.
#
# Everything here is read out of the build files rather than written down a
# second time, and it is read in one place rather than in each script. A name
# or a path spelled out twice is one that installs under one spelling and is
# looked for under the other the moment either changes.
#
# After sourcing, the caller can rely on:
#
#   $PANEL $SETTINGS      the two programs of the session
#   $WATCH                the service that reads the keyboard
#   $PROGRAMS             all three, for anything done to each installed file
#   $SESSION_PROGRAMS     the two of the session, for anything run as the user
#   $DESKTOP_ID           base name of the installed desktop entry
#   $BUILD_DIR            where install.sh builds and uninstall.sh looks
#   $INSTALL_BINDIR       where the programs go
#   $INSTALL_DESKTOP      the installed desktop entry
#   $INSTALL_ICON         the installed icon
#   $AUTOSTART_ENTRY      the copy that starts the tray with the session
#   $SETTINGS_DIR         where the program keeps its settings
#   $INPUT_GROUP          the group that used to carry read access to the keyboard
#   $WATCH_SOCKET_UNIT    the unit that starts the service
#   $INSTALL_UNIT_DIR     where that unit is installed
#
# shellcheck disable=SC2034
# All of these are read by the sourcing script.

# One line out of a file, and never a failure.
#
# The callers run under "set -e" with "pipefail", where a file that is not
# there takes the whole script down at the first assignment, with sed's
# complaint as the only thing anybody sees. What is wanted instead is an empty
# answer, so the check further down can say which file it was and what to do.
_read_line() {
    sed -n "$1" "$2" 2>/dev/null | head -1 || true
}

_TOP_BUILD_FILE="$PROJECT_ROOT/CMakeLists.txt"
_SRC_BUILD_FILE="$PROJECT_ROOT/src/CMakeLists.txt"

# The name of the project is the name of the panel; the settings window is that
# name with a suffix. Taken from here rather than from the order of the targets
# in the build file, where a third program added above the two would silently
# change which one is treated as the panel.
PROJECT_NAME=$(_read_line 's/^project(\([A-Za-z0-9_-]\{1,\}\) .*/\1/p' \
    "$_TOP_BUILD_FILE")
PANEL="$PROJECT_NAME"
SETTINGS="$PROJECT_NAME-editor"
# The service that reads the keyboard, which is not one of the two the person
# using this ever starts: it is started by its socket unit when the panel
# connects.
WATCH="$PROJECT_NAME-watch"

# Every target, so anything done to each installed file cannot miss one. Read
# as well
# as derived on purpose: the three names above are checked against it below, so a
# rename in the build file that did not reach this file stops the script rather
# than half installing.
#
# The name is taken and the rest of the line thrown away, because there are two
# shapes of that line: the sources on the lines below, or a list handed in on
# the same line. A pattern that insisted on the first shape once stopped the
# install of a tree that built perfectly well.
mapfile -t PROGRAMS < <(
    sed -n 's/^add_executable(\([A-Za-z0-9_-]\{1,\}\).*$/\1/p' \
        "$_SRC_BUILD_FILE" 2>/dev/null
)

# The two that run in the session, which is what anything looking for a running
# process means. The service is left out on purpose: it runs under an account
# of its own that this script has no business signalling, and the service
# manager is what stops it.
SESSION_PROGRAMS=("$PANEL" "$SETTINGS")

DESKTOP_ID=$(_read_line 's/^set(BINDPEEK_DESKTOP_ID "\([^"]*\)").*/\1/p' \
    "$_TOP_BUILD_FILE")

# The desktop entry as it lives in the checkout. Its path is asked of the build
# file rather than found by looking for a .desktop file in the assets: the
# build installs exactly the one named there, and a second entry added later
# would leave the two picking different files.
_DESKTOP_RELATIVE=$(_read_line \
    's|^set(BINDPEEK_DESKTOP_SOURCE .*}/\([^")]*\)"\{0,1\})$|\1|p' \
    "$_TOP_BUILD_FILE")
DESKTOP_SOURCE="$PROJECT_ROOT/$_DESKTOP_RELATIVE"

# The name of the icon is the one the entry asks for by its Icon key, not the
# name of the entry: those two are free to differ, and what has to match is the
# entry and the file it names.
ICON_NAME=$(_read_line 's/^Icon=\(.*\)$/\1/p' "$DESKTOP_SOURCE")

if [ -z "$PROJECT_NAME" ] || [ -z "$DESKTOP_ID" ] ||
    [ -z "$_DESKTOP_RELATIVE" ] || [ -z "$ICON_NAME" ]; then
    # Where the desktop entry lives is the one path here that is worked out
    # rather than known, so it is named. But only when there is one: with the
    # line it comes from unreadable, what is left of it is the checkout
    # directory and a slash, and pointing at that says less than saying which
    # line could not be read.
    if [ -n "$_DESKTOP_RELATIVE" ]; then
        _WHERE_THE_ENTRY_IS="and in $DESKTOP_SOURCE."
    else
        _WHERE_THE_ENTRY_IS="and could not tell from either where the entry is."
    fi
    fail "Could not read the names out of the build files." \
        "Looked in $_TOP_BUILD_FILE," \
        "in $_SRC_BUILD_FILE," \
        "$_WHERE_THE_ENTRY_IS" \
        "Run this from the checkout it came with."
    exit 1
fi
if [ "${#PROGRAMS[@]}" -lt 3 ]; then
    # A sentence of its own. Everything above was read perfectly well, and
    # telling somebody to run this from the right checkout when they already
    # are is an answer to a question they did not ask.
    fail "The build file lists fewer than the three programs there should be." \
        "In $_SRC_BUILD_FILE, found: ${PROGRAMS[*]:-nothing}"
    exit 1
fi
for _expected in "$PANEL" "$SETTINGS" "$WATCH"; do
    _found=0
    for _program in "${PROGRAMS[@]}"; do
        [ "$_program" = "$_expected" ] && _found=1
    done
    if [ "$_found" = 0 ]; then
        fail "The build file has no program called $_expected." \
            "Found: ${PROGRAMS[*]}"
        exit 1
    fi
done
unset _expected _found _program

# Where install.sh builds, and the only directory whose install manifest
# uninstall.sh treats as this program's. A directory of its own, never the one
# a developer works in: a build tree configured inside the Nix development
# shell has store paths baked into its cache, and installing from it would put
# paths into the prefix that stop existing at the next garbage collection.
BUILD_DIR="$PROJECT_ROOT/build-install"

# The prefix cmake installs to when none is given, which is what both scripts
# use. Written here so the pair cannot install to one place and look in
# another, and so a build directory whose manifest is missing still has a place
# to fall back to.
INSTALL_PREFIX=/usr/local
INSTALL_BINDIR="$INSTALL_PREFIX/bin"
INSTALL_DESKTOP="$INSTALL_PREFIX/share/applications/$DESKTOP_ID.desktop"
INSTALL_ICON="$INSTALL_PREFIX/share/icons/hicolor/scalable/apps/$ICON_NAME.svg"

# Where the units the build writes are installed, which is one of the four
# directories the service manager reads system units from.
INSTALL_UNIT_DIR="$INSTALL_PREFIX/lib/systemd/system"

# The autostart directory follows the specification, because what reads it is a
# desktop environment and that is what a desktop environment follows.
AUTOSTART_ENTRY="${XDG_CONFIG_HOME:-$HOME/.config}/autostart/$DESKTOP_ID.desktop"

# The settings do not, and that is not an oversight here: the program spells
# this path out itself, so the script says where the settings are rather than
# where they arguably ought to be.
SETTINGS_DIR="$HOME/.config/$PROJECT_NAME"

# The group earlier versions asked for. Kept only so both scripts can find a
# membership that is still there and offer to take it away: nothing here needs
# it any more.
INPUT_GROUP=input

# The unit that puts the socket in place and starts the service when a panel
# connects. Read out of the build file, which is also where the unit files take
# it from, so a rename cannot leave the scripts enabling something that is no
# longer there.
WATCH_SOCKET_UNIT=$(_read_line \
    's/^set(BINDPEEK_WATCH_SOCKET_UNIT "\([^"]*\)").*/\1/p' "$_TOP_BUILD_FILE")
if [ -z "$WATCH_SOCKET_UNIT" ]; then
    fail "Could not read the name of the socket unit out of $_TOP_BUILD_FILE."
    exit 1
fi
