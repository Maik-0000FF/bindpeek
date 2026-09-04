#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Removes what install.sh put on the machine: the three programs, the units of
# the service that reads the keyboard, the desktop entry and its icon, the
# autostart entry, and on request the settings.
#
# What it does not touch is the packages that were installed to build with.
# Other things on the machine want Qt and cmake too, and taking them away
# because one program is going would be the wrong call to make for somebody.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/_style.sh
. "$PROJECT_ROOT/scripts/_style.sh"
# shellcheck source=scripts/_distro.sh
. "$PROJECT_ROOT/scripts/_distro.sh"
# shellcheck source=scripts/_ask.sh
. "$PROJECT_ROOT/scripts/_ask.sh"

if [ -z "${HOME:-}" ]; then
    fail "HOME is not set, so there is no home directory to clean up."
    exit 1
fi
# shellcheck source=scripts/_paths.sh
. "$PROJECT_ROOT/scripts/_paths.sh"

# The manifest of the build install.sh made, and the only one taken as this
# program's. Another build directory in the checkout belongs to somebody
# working on the code, and its manifest names whatever prefix that build was
# installed to, which may be a scratch directory or somebody else's tree;
# running rm over those paths because they happened to be the first match is
# not a mistake worth risking. Without this one, the prefix is used instead.
MANIFEST="$BUILD_DIR/install_manifest.txt"

banner "bindpeek: remove"

detect_distro_info

if [ "${EUID:-$(id -u)}" -eq 0 ]; then
    fail "Do not run this with sudo." \
        "Run it as yourself. Sudo is asked for where files under the prefix" \
        "are removed, and the questions below are about your own account."
    exit 1
fi

if [ "$DISTRO" = nixos ]; then
    step "Nothing here to remove on NixOS."
    note "The programs come from the flake and go with the configuration that"
    note "brought them in. What may be left is your own settings:"
    note "    $SETTINGS_DIR"
    exit 0
fi

# --- What there is to remove ------------------------------------------------

# Worked out before anything is stopped or asked for, because both of those
# depend on it: the rights below are only wanted where there is a file to
# remove, and stopping the programs is no use if the removal then cannot go
# ahead.
FROM_MANIFEST=0
TO_REMOVE=()
if [ -f "$MANIFEST" ]; then
    FROM_MANIFEST=1
    # The guard on the last line is not decoration: cmake writes the manifest
    # without a closing newline, so a plain read loop drops the final path.
    # Measured on this project, that path is the icon, which would then be left
    # behind by every removal there has ever been.
    while IFS= read -r file || [ -n "$file" ]; do
        [ -n "$file" ] || continue
        [ -e "$file" ] && TO_REMOVE+=("$file")
    done < "$MANIFEST"
else
    # No manifest from an install made here, so the prefix cmake uses by
    # default is the best that can be said, and it is where install.sh puts
    # things.
    CANDIDATES=("$INSTALL_DESKTOP" "$INSTALL_ICON")
    CANDIDATES+=("$INSTALL_UNIT_DIR/$WATCH_SOCKET_UNIT")
    CANDIDATES+=("$INSTALL_UNIT_DIR/$WATCH_SERVICE_UNIT")
    for program in "${PROGRAMS[@]}"; do
        CANDIDATES+=("$INSTALL_BINDIR/$program")
    done
    for file in "${CANDIDATES[@]}"; do
        [ -e "$file" ] && TO_REMOVE+=("$file")
    done
fi

# --- Can a password be asked for --------------------------------------------

# Removing those files wants administrative rights, and sudo may want a
# password for them. Asked before the first of them rather than found out in
# the middle: a run with no terminal to ask on reaches sudo and waits there
# with nothing to show for it, which is the same silent halt the questions
# themselves were taught not to make.
#
# Only where there is something to remove. A machine where nothing of it is
# left still has an autostart entry and settings to clear away, and none of
# that wants rights of any kind.
#
# A machine that grants the rights without a password, which is what a
# container or a rule of its own does, passes here and carries on unattended.
# Whether anything below wants administrative rights. Two things do: removing
# the installed files, and switching off a unit that is still enabled. Asked
# together and before the first of them, or a run with nothing left to remove
# would skip this and walk into a password prompt at the unit instead.
NEEDS_ROOT=0
if [ "${#TO_REMOVE[@]}" -ne 0 ]; then
    NEEDS_ROOT=1
fi
if [ -d /run/systemd/system ] &&
    { systemctl is-enabled --quiet "$WATCH_SOCKET_UNIT" 2>/dev/null ||
        systemctl is-active --quiet "$WATCH_SOCKET_UNIT" 2>/dev/null; }; then
    UNIT_IS_ON=1
else
    UNIT_IS_ON=0
fi
if [ "$UNIT_IS_ON" = 1 ]; then
    NEEDS_ROOT=1
fi

if [ "$NEEDS_ROOT" = 1 ] &&
    ! sudo -n true 2>/dev/null && ! can_ask; then
    fail "Removing what was installed wants administrative rights, and" \
        "there is no terminal to ask for a password on." \
        "Run it where it can ask, or give this account a sudo rule that needs" \
        "no password." \
        "Nothing has been changed."
    exit 1
fi

# --- Stop what is running ---------------------------------------------------

step "Stopping"
STOPPED=0
if [ "$UNIT_IS_ON" = 1 ]; then
    # Switched off before the files go, or the manager keeps a socket listening
    # for a unit that is no longer on disk. --now takes the running service
    # down with it.
    sudo systemctl disable --now "$WATCH_SOCKET_UNIT" >/dev/null 2>&1 || true
    ok "$WATCH_SOCKET_UNIT"
    STOPPED=1
fi
for program in "${SESSION_PROGRAMS[@]}"; do
    # Same match as the install: the name pkill compares is cut short, so the
    # whole command line is searched instead, with or without a path in front.
    if pkill -u "$INVOKING_USER" -f "^([^ ]*/)?$program(\$| )" 2>/dev/null; then
        ok "$program"
        STOPPED=1
    fi
done
[ "$STOPPED" = 1 ] || note "nothing was running"

# --- The installed files ----------------------------------------------------

step "Installed files"
if [ "$FROM_MANIFEST" = 1 ]; then
    note "from $MANIFEST"
else
    note "no manifest from ./install.sh, using $INSTALL_PREFIX"
fi
if [ "${#TO_REMOVE[@]}" -eq 0 ]; then
    note "nothing of it is there"
else
    for file in "${TO_REMOVE[@]}"; do
        sudo rm -f "$file"
        ok "$file"
    done
fi

# The manager keeps its own picture of what units exist, and the two just
# deleted are still in it. Told here rather than left for the next boot, where
# they would show up as units that cannot be started.
#
# Only when something was actually removed. With nothing to remove there is
# nothing to forget either, and this is the one step that would otherwise ask
# for a password on a run that was told it would not need one.
if [ -d /run/systemd/system ] && [ "${#TO_REMOVE[@]}" -ne 0 ]; then
    sudo systemctl daemon-reload
fi

# --- The autostart entry ----------------------------------------------------

step "Autostart"
if [ -f "$AUTOSTART_ENTRY" ]; then
    rm -f "$AUTOSTART_ENTRY"
    ok "removed $AUTOSTART_ENTRY"
else
    note "no entry at $AUTOSTART_ENTRY"
fi
note "A line in a compositor's own startup file is not known here and stays."

# --- The settings -----------------------------------------------------------

# Asked, and left alone by default. Somebody removing a program is often about
# to install it again, and the settings are the one thing that cannot be built
# back from this checkout.
step "Settings"
if [ -d "$SETTINGS_DIR" ]; then
    ask "Remove your settings in $SETTINGS_DIR as well? [y/N]"
    if [ "$ASK_EOF" = 1 ]; then
        warn "nobody answered, so they stay"
    else
        case "$REPLY" in
            [Yy]*)
                rm -rf "$SETTINGS_DIR"
                ok "removed"
                ;;
            *) note "kept" ;;
        esac
    fi
else
    note "none at $SETTINGS_DIR"
fi

# --- A membership that is no longer wanted ----------------------------------

# Earlier versions asked for this group, and it granted far more than the panel
# ever used: every program of that account could read every key, whatever
# window it was typed into. Nothing here needs it any more, so an account still
# carrying it is carrying it for nothing.
#
# Offered rather than done. Nothing here knows what else on this machine was
# given the group for, and taking it from an account that needs it elsewhere is
# not something to do quietly.
step "An older membership"
if id -nG "$INVOKING_USER" | tr ' ' '\n' | grep -qx "$INPUT_GROUP"; then
    warn "$INVOKING_USER is in the '$INPUT_GROUP' group." \
        "While it is there, every program this account runs can read every" \
        "key pressed, including what is typed into other windows."
    ask "Remove $INVOKING_USER from the '$INPUT_GROUP' group? [y/N]"
    if [ "$ASK_EOF" = 1 ]; then
        note "Nobody to ask, so it is left alone. To drop it:"
        note "    sudo gpasswd -d $INVOKING_USER $INPUT_GROUP"
    else
        case "$REPLY" in
            [Yy]*)
                sudo gpasswd -d "$INVOKING_USER" "$INPUT_GROUP" >/dev/null
                ok "removed; this login keeps it until the next one"
                ;;
            *)
                note "left alone; something else may have been given it too"
                ;;
        esac
    fi
else
    ok "not in the '$INPUT_GROUP' group"
fi
echo
banner "bindpeek: removed"
