# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# NixOS module for bindpeek.
#
# Installing the package is one switch. It brings the service that reads the
# keyboard with it, socket and all, because that service is how the panel sees
# a modifier at all and because it grants nothing to anybody: it runs under an
# account the service manager makes and unmakes around it, and no login on the
# machine gains anything by it being there.
#
# One thing is a switch of its own, off, because it widens what the machine
# does rather than what it merely offers:
#
#   autoStart       starts a tray program in every user's session
#
# What the programs do with what they are given: they open no network
# connection of any kind. Every socket they touch is a local one, and they are
# of three kinds: the compositor's own, which is the Wayland display socket
# and, under Hyprland, that compositor's IPC socket as well, both at the place
# the session names for them, usually below $XDG_RUNTIME_DIR but not
# necessarily, since WAYLAND_DISPLAY may be an absolute path and the Hyprland
# socket falls back to /run/user/$UID; the session bus, used to ask the desktop
# portal whether the palette is light or dark and to export the tray icon over
# StatusNotifier; and the keyboard service's own socket, which carries which
# modifiers are held and the bare fact that some other key went down, and
# nothing else in either direction. To those the settings window adds one of
# its own, a socket in the runtime directory and the only one anything connects
# to rather than out of. It carries no data: a second start of the settings
# window connects to it and nothing is read from the connection, the connection
# itself being the whole message, which is that the window already open should
# come forward.
#
# Three things are read out of /proc, each of them one line long and each
# about a process the session named itself: the command line of the running
# compositor and the directory it was started in, together enough to learn
# which configuration file it reads, and the executable behind the running
# panel, so the settings window can tell its own panel from one left over by
# an earlier version of the package.
#
# The keyboard is read by the service and by nothing else. What it reads is
# masked in the kernel to the keys alone, what it hands out is which modifiers
# are down and whether some other key was pressed, and it answers only to
# somebody logged in. Nothing is written anywhere but the settings under
# ~/.config/bindpeek and, in the runtime directory, the panel's lock file and
# that socket, and the two links in the about dialog open only when somebody
# clicks them.
{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.bindpeek;

  # The name the package installs the entry under, which is BINDPEEK_DESKTOP_ID
  # in CMakeLists.txt. A build system and a module cannot share a value, so
  # this is the one place outside the build that has to be changed along with
  # it; written once here rather than at both ends of the link below, where a
  # half-done rename would break the autostart at activation time.
  desktopEntry = "bindpeek.desktop";
in
{
  # Said rather than dropped. A configuration that still carries the old option
  # stops the build with this sentence, which is the point: the membership it
  # asked for is still on those accounts, and it grants what it always granted.
  # Letting the option vanish would take the grant out of the configuration
  # while leaving it on the machine, and nobody would be told.
  imports = [
    (lib.mkRemovedOptionModule [ "programs" "bindpeek" "inputAccessFor" ] ''
      bindpeek no longer needs anybody to be in the `input` group. The
      keyboard is read by a service of its own, which the package brings with
      it and which runs under an account the service manager makes and unmakes
      around it.

      Remove this option. The membership it added is not removed by removing
      it, so take it away as well, on each account that has it:

          sudo gpasswd -d <user> input

      Leaving it in place means every program those accounts run can still read
      every key pressed on this machine, which is what the option always
      granted and what the service exists to avoid.
    '')
  ];

  options.programs.bindpeek = {
    enable = lib.mkEnableOption "bindpeek, a cheat sheet for the shortcuts assigned in the running session";

    package = lib.mkOption {
      type = lib.types.package;
      default =
        pkgs.bindpeek or (throw ''
          bindpeek: no package to install.

          This module was imported on its own, so it looked for `pkgs.bindpeek`
          and there is none. Pick one of:

            - import the flake's `nixosModules.default`, which brings its own
              build along and needs nothing else
            - add the flake's `overlays.default` to nixpkgs, which is what
              puts `pkgs.bindpeek` there
            - set `programs.bindpeek.package` to a package of your own
        '');
      defaultText = lib.literalExpression "pkgs.bindpeek";
      description = ''
        The package to install. Defaults to `pkgs.bindpeek`, which the flake's
        `overlays.default` provides; importing the flake's
        `nixosModules.default` sets it to the flake's own build instead, so the
        overlay is not required.
      '';
    };

    autoStart = lib.mkOption {
      type = lib.types.bool;
      default = false;
      # environments:begin
      description = ''
        Start the editor, and with it the tray icon and the panel, in every
        graphical session on this machine. This installs the package's own
        desktop entry into `/etc/xdg/autostart`.

        The panel comes up with the editor unless it was switched off from the
        tray, which is remembered in the settings file.

        Only a desktop environment reads that directory: KDE Plasma, GNOME,
        Xfce, LXQt and their like, which implement the XDG autostart
        specification. A bare compositor started from a script, such as
        sway, Hyprland or mango, implements nothing of the sort. There this
        option has no effect whatsoever, silently, and the way to start the
        tray is a line in the compositor's own startup file:

        ```
        bindpeek-editor &
        ```

        System-wide and therefore for all users. For a single user under a
        desktop environment, copy the same file into `~/.config/autostart`
        instead, or let the desktop's own autostart settings do it.
      '';
      # environments:end
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    # The units the package carries, and the socket switched on with them. The
    # socket is what makes the service exist at all: it holds the listening end
    # while nothing is running, and starts the service when a panel connects.
    #
    # On rather than optional, because there is nothing to weigh. It grants no
    # account anything, it runs nothing until a panel asks, and without it the
    # panel this module installs cannot see a modifier and refuses to start.
    systemd.packages = [ cfg.package ];
    systemd.sockets.bindpeek-watch.wantedBy = [ "sockets.target" ];

    # The packaged entry, not a copy of it: a second spelling of Exec or Icon
    # here would drift away from the one the package installs.
    environment.etc."xdg/autostart/${desktopEntry}" = lib.mkIf cfg.autoStart {
      source = "${cfg.package}/share/applications/${desktopEntry}";
    };
  };
}
