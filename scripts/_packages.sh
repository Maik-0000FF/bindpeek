# shellcheck shell=bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# What has to be on the machine before this can be built, under the four sets
# of names the supported families give it, and the two commands that go with
# them.
#
# One contract for all three: source _distro.sh, call detect_distro_info, and
# they all read the family out of $DISTRO. Handing it to one of them and not
# to the others would be two rules for one question, and the caller would be
# free to ask about one family while installing for another.
#
# The lists live here rather than in install.sh because the checks that run on
# every push install exactly this set, in a container of each family. A name
# that is wrong on one distribution therefore fails there, rather than in front
# of somebody who was only trying to install the program. A list written out a
# second time in a workflow file would prove the workflow, not the script.
#
# $PACKAGE_SUDO goes in front of the installing command. It is "sudo" here,
# which is what a person needs; a run that is already root, which is what a
# container is, sets it to nothing.
: "${PACKAGE_SUDO=sudo}"

# The set for one family, into $DEPENDENCIES.
#
# All four hold the same things under different names: the build tools, Qt 6
# with the QML runtime, the layer-shell binding the panel is drawn as, libevdev
# for reading the keyboard, and KDE's KConfig for reading the shortcut file the
# KDE backend is given.
#
# The Debian list names the QML modules one at a time because that distribution
# ships them separately, and a missing one is not a build error: the program
# links, starts, and comes up with an empty window.
#
# shellcheck disable=SC2034
# DEPENDENCIES is read by whoever called this.
dependencies_for() {
    case "$DISTRO" in
        arch)
            DEPENDENCIES=(cmake ninja pkgconf gcc
                qt6-base qt6-declarative qt6-svg qt6-wayland qt6-tools
                layer-shell-qt libevdev kconfig)
            ;;
        debian)
            DEPENDENCIES=(cmake ninja-build pkg-config g++
                qt6-base-dev qt6-declarative-dev libqt6svg6-dev qt6-wayland
                qt6-tools-dev qt6-l10n-tools
                qml6-module-qtquick qml6-module-qtquick-controls
                qml6-module-qtquick-layouts qml6-module-qtquick-templates
                qml6-module-qtquick-window qml6-module-qtqml-workerscript
                liblayershellqtinterface-dev libevdev-dev libkf6config-dev)
            ;;
        fedora)
            DEPENDENCIES=(cmake ninja-build gcc-c++ pkgconf
                qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel
                qt6-qtwayland qt6-qttools-devel qt6-linguist
                layer-shell-qt-devel libevdev-devel kf6-kconfig-devel)
            ;;
        suse)
            DEPENDENCIES=(cmake ninja gcc-c++ pkgconf-pkg-config
                qt6-base-devel qt6-declarative-devel qt6-svg-devel qt6-wayland
                qt6-linguist-devel
                layer-shell-qt6-devel libevdev-devel kf6-kconfig-devel)
            ;;
        *)
            DEPENDENCIES=()
            return 1
            ;;
    esac
}

# Whether one package is there. Asked of the package manager rather than by
# looking for a file: what matters is whether the distribution considers it
# installed, which is also what the command below would change.
is_installed() {
    case "$DISTRO" in
        arch) pacman -Q "$1" >/dev/null 2>&1 ;;
        debian) dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "^install ok installed$" ;;
        fedora | suse) rpm -q "$1" >/dev/null 2>&1 ;;
        *) return 1 ;;
    esac
}

# shellcheck disable=SC2086
# PACKAGE_SUDO is deliberately unquoted: empty it must disappear rather than
# become an empty first argument.
install_packages() {
    case "$DISTRO" in
        # Every one of the four is told not to ask. The caller has already put
        # the question, having listed what is missing, and a second one from
        # the package manager is either a repetition or, in a run with nobody
        # at the keyboard, a wait with no end.
        arch) $PACKAGE_SUDO pacman -S --needed --noconfirm "$@" ;;
        debian) $PACKAGE_SUDO apt-get update &&
            $PACKAGE_SUDO apt-get install -y "$@" ;;
        fedora) $PACKAGE_SUDO dnf install -y "$@" ;;
        suse) $PACKAGE_SUDO zypper --non-interactive install "$@" ;;
        *) return 1 ;;
    esac
}
