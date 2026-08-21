# shellcheck shell=bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared distribution detection for install.sh and uninstall.sh. Source it; it
# sets no options of its own.
#
# After detect_distro_info the caller can rely on:
#
#   $DISTRO         the family, one of: arch debian fedora suse nixos unknown
#   $DISTRO_LABEL   what to print, the distribution's own name where it gives one
#   $INVOKING_USER  who is running this, asked of the system rather than $USER
#
# Two scripts that each worked this out for themselves would answer differently
# the moment one of them learned about a new derivative, and the pair would
# then install under one name and refuse to remove under another.
#
# shellcheck disable=SC2034
# DISTRO, DISTRO_LABEL and INVOKING_USER are read by the sourcing script.
detect_distro_info() {
    # Asked of the system, not taken from $USER: that variable is empty in a
    # cron job or under su, and it is used further on to name the account whose
    # processes are stopped and whose group membership is changed.
    INVOKING_USER="$(id -un)"

    local id=""
    local id_like=""
    DISTRO_LABEL="unknown distribution"
    if [ -f /etc/os-release ]; then
        # shellcheck source=/dev/null
        . /etc/os-release
        id="${ID:-}"
        id_like="${ID_LIKE:-}"
        DISTRO_LABEL="${PRETTY_NAME:-${NAME:-$id}}"
    fi

    case "$id" in
        arch | manjaro | endeavouros | garuda | artix | cachyos) DISTRO=arch ;;
        debian | ubuntu | linuxmint | pop | kali | elementary | zorin | mx | neon)
            DISTRO=debian
            ;;
        fedora | nobara | bazzite) DISTRO=fedora ;;
        opensuse* | suse | sle*) DISTRO=suse ;;
        nixos) DISTRO=nixos ;;
        *)
            # No name of its own that is known here, so ask what it is like,
            # which is what a derivative sets when it wants to be treated as
            # its parent.
            case "$id_like" in
                *arch*) DISTRO=arch ;;
                *debian* | *ubuntu*) DISTRO=debian ;;
                *fedora* | *rhel*) DISTRO=fedora ;;
                *suse*) DISTRO=suse ;;
                *)
                    # Last resort: whichever package manager is on the machine
                    # says more about how to install than a missing file does.
                    if command -v pacman >/dev/null 2>&1; then
                        DISTRO=arch
                    elif command -v apt-get >/dev/null 2>&1; then
                        DISTRO=debian
                    elif command -v dnf >/dev/null 2>&1; then
                        DISTRO=fedora
                    elif command -v zypper >/dev/null 2>&1; then
                        DISTRO=suse
                    else
                        DISTRO=unknown
                    fi
                    ;;
            esac
            ;;
    esac
}
