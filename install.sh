#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds bindpeek from this checkout and installs the two programs: the panel
# that shows the shortcuts of the running session, and the settings window that
# carries the tray icon.
#
# Two things here widen what the machine allows rather than what it merely
# offers, and both are asked for rather than done: membership in the group that
# grants read access to the keyboard, and starting the tray with every login.
# Where there is nobody to ask, neither happens.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/_style.sh
. "$PROJECT_ROOT/scripts/_style.sh"
# shellcheck source=scripts/_distro.sh
. "$PROJECT_ROOT/scripts/_distro.sh"
# shellcheck source=scripts/_ask.sh
. "$PROJECT_ROOT/scripts/_ask.sh"
# shellcheck source=scripts/_packages.sh
. "$PROJECT_ROOT/scripts/_packages.sh"

# Asked before the paths are worked out, because several of them are built from
# it and an empty one would silently put files at the root of the filesystem.
if [ -z "${HOME:-}" ]; then
    fail "HOME is not set, so there is no home directory to install into."
    exit 1
fi
# shellcheck source=scripts/_paths.sh
. "$PROJECT_ROOT/scripts/_paths.sh"

banner "bindpeek: install"

# --- Who is running this ----------------------------------------------------

detect_distro_info
note "Distribution: $DISTRO_LABEL"

if [ "${EUID:-$(id -u)}" -eq 0 ]; then
    fail "Do not run this with sudo." \
        "Run it as yourself. Sudo is asked for at the two steps that need" \
        "it, and the group membership below has to name a real account."
    exit 1
fi

if [ "$DISTRO" = nixos ]; then
    step "NixOS builds this from the flake, not from here."
    note "  nix build            # result/bin/$PANEL, result/bin/$SETTINGS"
    note "The group membership and the autostart belong in the system"
    note "configuration; the module in nix/ has an option for each."
    exit 0
fi

if [ "$DISTRO" = unknown ]; then
    fail "No package manager here that this script knows." \
        "By hand, in this order:" \
        "  1. install cmake, ninja, pkg-config, a C++20 compiler, Qt 6" \
        "     (base, declarative, svg, wayland, linguist tools)," \
        "     layer-shell-qt, libevdev and KDE's KConfig framework" \
        "  2. cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release" \
        "  3. cmake --build build -j\$(nproc)" \
        "  4. sudo cmake --install build" \
        "  5. add yourself to the '$INPUT_GROUP' group and log in again"
    exit 1
fi

# --- Can a password be asked for --------------------------------------------

# Several steps below want administrative rights, and sudo may want a password
# for them. Asked once here rather than found out in the middle of the work: a
# run with no terminal to ask on reaches the first of those steps and waits
# there with nothing to show for it, which is the same silent halt the
# questions themselves were taught not to make.
#
# A machine that grants the rights without a password, which is what a
# container or a rule of its own does, passes here and carries on unattended.
if ! sudo -n true 2>/dev/null && ! can_ask; then
    fail "This needs administrative rights, and there is no terminal to ask" \
        "for a password on." \
        "Run it where it can ask, or give this account a sudo rule that needs" \
        "no password."
    exit 1
fi

# --- What has to be there ---------------------------------------------------

step "Dependencies"
# The list itself lives beside this script, where the checks that run on every
# push read the same one and install it in a container of each family.
if ! dependencies_for; then
    # Only reachable once a family is added to the detection without a list to
    # go with it. Without this the loop below would walk an empty list, report
    # nothing missing, and go on to a build that cannot work.
    fail "No package list for '$DISTRO'." \
        "The family is recognised but nothing here knows what it calls the" \
        "things this needs."
    exit 1
fi
# What is wanted on top of that, and may not exist here at all.
optional_dependencies_for || OPTIONAL=()

MISSING=()
for package in "${DEPENDENCIES[@]}"; do
    if is_installed "$package"; then
        item yes "$package"
    else
        item no "$package"
        MISSING+=("$package")
    fi
done

MISSING_OPTIONAL=()
for package in "${OPTIONAL[@]}"; do
    if is_installed "$package"; then
        item yes "$package (for the KDE backend)"
    else
        item no "$package (for the KDE backend)"
        MISSING_OPTIONAL+=("$package")
    fi
done

# What is lost without the framework, worded once and said wherever that
# happens: the offer was declined here, or the distribution has no such package
# to offer at all.
#
# What it does not say is that the backend is gone for certain. The question
# here is about a package, and answering it is not the same as answering
# whether the framework is on the machine: a framework installed under a name
# this script does not carry, or from somewhere a package manager cannot see,
# is found by the build all the same and the backend is built. Which of the two
# it was is not guessed here, it is said by the configure step, which looks for
# the framework itself and prints what it found.
WITHOUT_KDE=(
    "The build then leaves the KDE backend out, unless the framework is"
    "already there under some other name; the configure step below says"
    "which of the two it was. Only a KDE Plasma session could not be read."
)

if [ "${#MISSING[@]}" -ne 0 ] || [ "${#MISSING_OPTIONAL[@]}" -ne 0 ]; then
    echo
    # The one question a run with nobody at the keyboard still answers with
    # yes: installing what is needed to build is the whole errand, and it adds
    # to the machine rather than changing what it allows.
    ask "Install the $((${#MISSING[@]} + ${#MISSING_OPTIONAL[@]})) missing package(s)? [Y/n]"

    DECLINED=0
    case "$REPLY" in
        [Nn]*) DECLINED=1 ;;
    esac

    # What a No means depends on what is missing, and the two are not one
    # thing:
    #
    #   required   optional   answer     what happens
    #   --------   --------   --------   ---------------------------------
    #   none       none       not asked  nothing to install
    #   none       some       yes        installed, a failure is tolerated
    #   none       some       no         left uninstalled, the run carries on
    #   some       any        yes        the required ones, then the rest
    #   some       any        no         nothing to build with, the run ends
    #
    # A No while only the framework is missing refuses a package, not the
    # build. What is left is the build a distribution gets that carries no such
    # package, and that one is not treated as a reason to stop either. Whether
    # the KDE backend ends up in it is not decided here: the build looks for
    # the framework itself, so a copy already on the machine is used whatever
    # was answered about the package.
    if [ "$DECLINED" -eq 1 ]; then
        if [ "${#MISSING[@]}" -ne 0 ]; then
            fail "Nothing to build with."
            exit 1
        fi
        warn "Left out: ${MISSING_OPTIONAL[*]}." "${WITHOUT_KDE[@]}"
        ok "carrying on"
    else
        if [ "${#MISSING[@]}" -ne 0 ]; then
            install_packages "${MISSING[@]}"
        fi
        # Separately, and a failure here is not the end of the run. On a
        # distribution that does not carry the framework at all, the package
        # manager answers that it knows no such package, and the right thing
        # then is to build without the backend rather than to stop: what is
        # left out is the reading of KDE's shortcuts, and a system without the
        # framework has no KDE Plasma 6 whose shortcuts there would be to read.
        for package in "${MISSING_OPTIONAL[@]}"; do
            if ! install_packages "$package"; then
                warn "$package is not available here." "${WITHOUT_KDE[@]}"
            fi
        done
        ok "installed"
    fi
else
    ok "all present"
fi

# --- Build ------------------------------------------------------------------

step "Building"
# Ninja rather than the default generator: it is in the list above, so it is
# there, and it does not rely on make being on a minimal machine.
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"
ok "built"

# --- Will this session work -------------------------------------------------

# Asked of the program that was just built rather than guessed from a variable
# the session sets: the panel reads the shortcuts of a session it recognizes,
# and whether it recognizes this one is exactly what --list answers.
#
# Skipped without a Wayland session, which is an install over ssh or from a
# text console, and the container the tests run in. Not knowing is not the same
# as knowing it will not work, and neither is a reason to stop.
if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    step "This session"
    if SESSION_REPORT=$("$BUILD_DIR/src/$PANEL" --list 2>&1 >/dev/null); then
        ok "the shortcuts of this session can be read"
    else
        warn "the panel cannot read this session yet" \
            "$SESSION_REPORT" \
            "The programs install either way and work in a session they know."
    fi
fi

# --- Install ----------------------------------------------------------------

step "Installing"
sudo cmake --install "$BUILD_DIR"
ok "installed to $INSTALL_BINDIR"

# Stopped after the install, not before it. A replaced file leaves the running
# program on the copy it started from, so nothing breaks by waiting; going the
# other way round would leave somebody with no tray and no panel if the install
# were the step that failed.
#
# Matched against the whole command line rather than the process name: the name
# pkill compares is cut at fifteen characters, which is shorter than the name of
# the settings window. An entry in an autostart file starts the bare name and a
# terminal a full path, so both are allowed for.
for program in "${PROGRAMS[@]}"; do
    pkill -u "$INVOKING_USER" -f "^([^ ]*/)?$program(\$| )" 2>/dev/null || true
done

# --- Reading the keyboard ---------------------------------------------------

# The panel appears while a modifier is held down. A Wayland program is told
# nothing about a key until it has the focus, and the panel deliberately never
# takes it, so the modifiers are read from the event devices below the
# compositor instead. That needs the group, and there is no narrower grant for
# it on Linux.
#
# Two questions, not one, and they have different answers. What this login can
# do is what `id` reports for the running process; what the account has been
# granted is what it reports for the name. A membership added a moment ago
# shows up in the second and not in the first, and it is the first that decides
# whether the panel starts today.
step "Reading the keyboard"
NEEDS_RELOGIN=0
if id -nG | tr ' ' '\n' | grep -qx "$INPUT_GROUP"; then
    ok "this login can read the keyboard"
elif id -nG "$INVOKING_USER" | tr ' ' '\n' | grep -qx "$INPUT_GROUP"; then
    warn "$INVOKING_USER is in the '$INPUT_GROUP' group, but this login is not"
    NEEDS_RELOGIN=1
else
    warn "$INVOKING_USER is not in the '$INPUT_GROUP' group" \
        "Without it the panel refuses to start. With it, every program you" \
        "run can read every key you press, including what you type into" \
        "other windows. The grant is per account, not per program."
    ask "Add $INVOKING_USER to the '$INPUT_GROUP' group? [y/N]"
    case "$REPLY" in
        [Yy]*)
            # A minimal image can be without the group, where usermod would
            # fail rather than create it.
            getent group "$INPUT_GROUP" >/dev/null || sudo groupadd "$INPUT_GROUP"
            sudo usermod -aG "$INPUT_GROUP" "$INVOKING_USER"
            ok "added"
            NEEDS_RELOGIN=1
            ;;
        *)
            warn "left alone; the panel will say so when it refuses to start"
            ;;
    esac
fi

# --- Starting with the session ----------------------------------------------

# The entry that is installed with the package, copied rather than written a
# second time: a second copy of Exec or Icon here would be the one that goes
# stale.
#
# A no removes an entry that is there. Running this again and saying no is how
# somebody turns the autostart off, and a no that left the old entry in place
# would look like it had done that while changing nothing.
step "Starting with the session"
ask "Start the tray icon at login? A no removes an entry that is already there. [Y/n]"
if [ "$ASK_EOF" = 1 ]; then
    note "Nobody to ask, so the autostart is left as it is."
elif [[ "$REPLY" == [Nn]* ]]; then
    if [ -f "$AUTOSTART_ENTRY" ]; then
        rm -f "$AUTOSTART_ENTRY"
        ok "removed $AUTOSTART_ENTRY"
    else
        note "Not set up. Start it by hand with: $SETTINGS"
    fi
elif [ -f "$INSTALL_DESKTOP" ]; then
    mkdir -p "$(dirname "$AUTOSTART_ENTRY")"
    cp "$INSTALL_DESKTOP" "$AUTOSTART_ENTRY"
    ok "entry written to $AUTOSTART_ENTRY"
    note "That file is read by desktop environments. A compositor started"
    note "from a script, such as Sway, Hyprland or mango, reads nothing of"
    note "the sort; there the way to start it is a line of its own in the"
    note "compositor's startup file:"
    note "    $SETTINGS &"
else
    warn "no installed entry at $INSTALL_DESKTOP to copy"
fi

# --- Done -------------------------------------------------------------------

echo
banner "bindpeek: installed"
if [ "$NEEDS_RELOGIN" = 1 ]; then
    warn "Log out and back in, or this login still cannot read the keyboard."
    echo
fi
note "Start the tray icon and the settings window:"
note "    $SETTINGS"
note ""
note "Hold a modifier to bring the panel up. Print the same list as text:"
note "    $PANEL --list"
note ""
note "Remove all of it again with ./uninstall.sh"
