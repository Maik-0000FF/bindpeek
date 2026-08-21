# shellcheck shell=bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared question for install.sh and uninstall.sh. Source it; it sets no
# options of its own.
#
# Call as:  ask "Install them? [Y/n]"
# The space before the cursor is added here, so no caller has to remember a
# trailing one and the question can be quoted as it stands further down.
#
# After ask the caller can rely on:
#
#   $REPLY     the answer, empty when there was none
#   $ASK_EOF   1 when there was nobody to answer at all
#
# The question is put to the terminal, not to standard input. A script run as
# `./install.sh < somefile` would otherwise read that file as the answers, one
# line per question, and a line of its text is not a No: it falls through to
# whatever the question offered as its default. Measured, the first line of a
# shell script answers "install everything" perfectly well.
#
# With no terminal there is nobody to ask, and that is not the same as somebody
# pressing Enter: a bare Enter means "take the default you offered", nobody at
# all means it would be taken FOR somebody. ASK_EOF says which of the two it
# was, so a question that changes more than the plain install can hold back on
# the second while an ordinary one carries on.
#
# shellcheck disable=SC2034
# ASK_EOF is read by the sourcing script.
# Whether a question can be put at all, which is two things.
#
# There has to be a terminal, and that is found out by opening it rather than
# by asking whether the device file is readable, which answers about the
# permissions of a node and not about whether it can be opened.
#
# And this run has to be the one the terminal is listening to. Opening it
# succeeds from a process group in the background as well; only the reading
# fails there, by way of a signal that stops the run. So the group the terminal
# has in the foreground is compared with the one this is in, and they differ in
# exactly the case that would otherwise wait forever. Without a terminal at all
# the first number is -1, and the two cannot match.
#
# Used by the scripts as well, before they reach a step that wants a password:
# nothing here can shield a program it merely starts.
#
# Where ps cannot say, having a terminal is taken as answer enough. That is
# what this was before, it is no worse than before, and the question itself is
# still guarded against the signal.
can_ask() {
    { : </dev/tty >/dev/tty; } 2>/dev/null || return 1

    local mine listening
    mine=$(ps -o pgid= -p $$ 2>/dev/null | tr -d ' ')
    listening=$(ps -o tpgid= -p $$ 2>/dev/null | tr -d ' ')
    if [ -z "$mine" ] || [ -z "$listening" ]; then
        return 0
    fi
    [ "$mine" = "$listening" ]
}

ask() {
    # Cleared first, or an unanswered question would leave the previous answer
    # standing and quietly apply it to this one as well.
    REPLY=""
    ASK_EOF=0

    if ! can_ask; then
        ASK_EOF=1
        echo
        echo "Nothing here can be asked: $1"
        return
    fi

    # A run in the background is halted by the question rather than answered:
    # reading the terminal from a process group that is not the one in the
    # foreground raises SIGTTIN, and what that signal does by default is stop
    # the process. A script that hangs without a word is worse than one that
    # says it cannot ask, so the signal is ignored for the length of the read,
    # which turns the halt into an ordinary failure and lands in the branch
    # below. Its counterpart on the writing side goes with it, for a terminal
    # set to stop background output.
    #
    # Measured under a terminal of its own: without this the run is halted at
    # the question and stays that way, with it the question comes back
    # unanswered and the run carries on to the sentence below.
    trap '' TTIN TTOU

    # The question is printed by a command of its own rather than by the read.
    # A read that prints its own prompt writes it on the stream it also writes
    # its complaints on, so sending that stream to the terminal, which is the
    # only way the question is seen, sends the complaints there as well, past
    # every attempt to catch them. Printed apart, the question goes to the
    # terminal on the ordinary output stream, while anything either command has
    # to complain about goes to the group's error stream and is thrown away.
    # What is said instead is the sentence below.
    if ! { printf '%s ' "$1" >/dev/tty && read -r REPLY </dev/tty; } 2>/dev/null; then
        # A line typed but never finished is not an answer either.
        REPLY=""
        ASK_EOF=1
        echo
        echo "Nobody there to answer: $1"
    fi

    # Back to the default. Neither script installs a handler of its own, so
    # there is nothing here to put back but the default itself.
    trap - TTIN TTOU
}
