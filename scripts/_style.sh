# shellcheck shell=bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared output style for install.sh and uninstall.sh. Source it; it sets no
# options of its own.
#
# The colours live here rather than at the top of both scripts. They are the
# same handful of escape sequences either way, and a pair that has drifted
# makes the two halves of one program look like two programs.
#
# Everything the scripts print goes through one of these, so how a step looks
# is decided once:
#
#   banner "Installation"    the heading a run opens with
#   step   "Building..."     what is about to happen
#   ok     "installed"       a step that worked
#   note   "any line"        an ordinary line, lined up under the others
#   warn   "no tray" "why"   worth knowing, not a failure
#   fail   "no compiler"     the reason the script is about to stop
#   item   "cmake"           one line of a list, present or missing
#
# The two that report trouble write to standard error, the rest to standard
# output, and they take the explanation with them: every argument after the
# first is a line of its own, lined up under the marker and written the same
# way. Left to note, the complaint and the reason for it would be split across
# the two streams, and a run kept in a file would show a bare "do not do that"
# with nothing that says what to do instead.
#
# Colour is dropped when standard output is not a terminal, or when NO_COLOR
# is set. The two lines that go to standard error follow the same decision,
# which is right where both are the same terminal and both are redirected, and
# that is every run but a contrived one.
# A log file or a CI transcript is read as text, and escape sequences in it are
# noise that hides the very lines they were meant to pick out.
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    STYLE_RED=$'\033[0;31m'
    STYLE_GREEN=$'\033[0;32m'
    STYLE_YELLOW=$'\033[1;33m'
    STYLE_BLUE=$'\033[0;34m'
    STYLE_OFF=$'\033[0m'
else
    STYLE_RED=""
    STYLE_GREEN=""
    STYLE_YELLOW=""
    STYLE_BLUE=""
    STYLE_OFF=""
fi

# The rule under and over a banner. A fixed width rather than one measured from
# the heading, which would make the box jump about between the steps that use
# it; wide enough to leave room for a longer heading than the ones there are.
STYLE_RULE="========================================"

banner() {
    printf '%s%s%s\n' "$STYLE_BLUE" "$STYLE_RULE" "$STYLE_OFF"
    printf '%s  %s%s\n' "$STYLE_BLUE" "$1" "$STYLE_OFF"
    printf '%s%s%s\n\n' "$STYLE_BLUE" "$STYLE_RULE" "$STYLE_OFF"
}

step() { printf '\n%s%s%s\n' "$STYLE_BLUE" "$1" "$STYLE_OFF"; }
ok() { printf '%s  ok%s  %s\n' "$STYLE_GREEN" "$STYLE_OFF" "$1"; }
note() { printf '      %s\n' "$1"; }

# The lines under a marker, lined up with the text beside it.
#
# An argument may itself be several lines, which is what a report caught from
# another program looks like, and each of them is indented rather than the
# first alone: a block that starts under the marker and then falls back to the
# left margin reads as two messages. An empty argument is nothing to say, not
# a blank line in the middle of a reason.
#
# The names are local because these two are called from anywhere, including
# from inside a loop over a variable somebody else is using.
_style_reason() {
    local argument line
    for argument in "$@"; do
        [ -n "$argument" ] || continue
        # Fed without a newline of its own and read to the end regardless, or
        # an argument that already ends in one would be followed by the blank
        # indented line this exists to avoid.
        while IFS= read -r line || [ -n "$line" ]; do
            printf '      %s\n' "$line" >&2
        done < <(printf '%s' "$argument")
    done
}

# The first line carries the marker, every further one is the explanation and
# goes the same way.
warn() {
    printf '%s  !!%s  %s\n' "$STYLE_YELLOW" "$STYLE_OFF" "$1" >&2
    shift
    _style_reason "$@"
}

fail() {
    printf '%s  no%s  %s\n' "$STYLE_RED" "$STYLE_OFF" "$1" >&2
    shift
    _style_reason "$@"
}

# One line of a list. The first argument says whether it is there.
item() {
    if [ "$1" = yes ]; then
        printf '%s  +%s %s\n' "$STYLE_GREEN" "$STYLE_OFF" "$2"
    else
        printf '%s  -%s %s\n' "$STYLE_RED" "$STYLE_OFF" "$2"
    fi
}
