# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# NixOS module for bindpeek.
#
# Installing the package is one switch. The two things that cannot be done
# safely by default are separate switches of their own, both off, because both
# widen what the machine allows rather than what it merely offers:
#
#   inputAccessFor  hands out the ability to read every keystroke
#   autoStart       starts a tray program in every user's session
#
# Enabling bindpeek alone therefore changes nothing except that the two
# programs exist on PATH.
#
# What the programs do with what they are given: they open no network
# connection of any kind. Every socket they touch is a local one, and they are
# of two kinds: the compositor's own, which is the Wayland display socket and,
# under Hyprland, that compositor's IPC socket as well, both at the place the
# session names for them, usually below $XDG_RUNTIME_DIR but not necessarily,
# since WAYLAND_DISPLAY may be an absolute path and the Hyprland socket falls
# back to /run/user/$UID; and the session bus, used to ask the desktop
# portal whether the palette is light or dark and to export the tray icon over
# StatusNotifier. To those the settings window adds one of its own, a socket
# in the runtime directory and the only one anything connects to rather than
# out of. It carries no data: a second start of the settings window connects
# to it and nothing is read from the connection, the connection itself being
# the whole message, which is that the window already open should come
# forward.
#
# Three things are read out of /proc, each of them one line long and each
# about a process the session named itself: the command line of the running
# compositor and the directory it was started in, together enough to learn
# which configuration file it reads, and the executable behind the running
# panel, so the settings window can tell its own panel from one left over by
# an earlier version of the package.
#
# Nothing is read from the keyboard other than which modifiers are down and
# whether some other key was pressed, nothing is written anywhere but the
# settings under ~/.config/bindpeek and, in the runtime directory, the panel's
# lock file and that socket, and the two links in the about dialog open only
# when somebody clicks them.
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

    inputAccessFor = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [ ];
      example = [ "alice" ];
      description = ''
        Users to add to the `input` group.

        bindpeek reads the keyboard through `/dev/input`, below the compositor.
        It has to: the panel appears while a modifier is being held, and a
        Wayland client receives no key events before it has focus, so there is
        no way to see the modifier that is supposed to summon the panel.

        Membership in `input` is therefore required for the overlay to work at
        all. Understand what it grants before setting this:

        > Every program the listed user runs gains read access to all input
        > devices, and can record every keystroke on the machine, including
        > passwords typed into other applications. The grant is per user, not
        > per program, and Linux offers no narrower one for this.

        Left empty, the package is still installed and the overlay refuses to
        start with a message naming this group, rather than failing silently.
      '';
    };

    autoStart = lib.mkOption {
      type = lib.types.bool;
      default = false;
      # environments:begin except kde
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

    users.groups.input.members = cfg.inputAccessFor;

    # The packaged entry, not a copy of it: a second spelling of Exec or Icon
    # here would drift away from the one the package installs.
    environment.etc."xdg/autostart/${desktopEntry}" = lib.mkIf cfg.autoStart {
      source = "${cfg.package}/share/applications/${desktopEntry}";
    };
  };
}
