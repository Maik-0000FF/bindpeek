# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

{
  description = "bindpeek: a quick peek at the shortcuts assigned in the running session (mango, Hyprland, KDE)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      # Linux only: all three sources are Linux desktops, and the later overlay
      # hangs on wlr-layer-shell.
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      # Libraries to build and link against. Defined once, shared by the package
      # and the dev shell.
      dependencies = pkgs: [
        pkgs.qt6.qtbase
        pkgs.qt6.qtdeclarative # QML engine for the overlay and the editor
        pkgs.qt6.qtsvg # renders the tray icon
        pkgs.qt6.qtwayland # Wayland platform plugin
        pkgs.kdePackages.layer-shell-qt # wlr-layer-shell surface
        pkgs.libevdev # reads /dev/input below the compositor
        # KDE own configuration library: reads kglobalshortcutsrc the way KDE
        # writes it.
        pkgs.kdePackages.kconfig
      ];

      mkPkg =
        pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "bindpeek";
          # Read out of CMakeLists.txt so the version is not maintained twice.
          version = builtins.head (builtins.match
            ".*project\\(bindpeek VERSION ([0-9.]+).*"
            (builtins.readFile ./CMakeLists.txt));
          src = self;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config # libevdev is found through pkg-config
            pkgs.qt6.qttools # lrelease for the translation catalogs
            pkgs.qt6.wrapQtAppsHook
          ];
          buildInputs = dependencies pkgs;

          doCheck = true;
          # What the tests need and the sandbox does not hand them.
          #
          # The test binaries are not wrapped the way the installed programs
          # are, so the one that draws the panel would find neither QtQuick
          # nor a font: the imports live in a store path of their own, and a
          # build has no fonts and nowhere to cache them. A panel measured
          # without a font measures nothing at all, which would leave the
          # test either failing here or, worse, passing on zeroes.
          preCheck = ''
            export QML2_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/lib/qt-6/qml"
            export QML_IMPORT_PATH="$QML2_IMPORT_PATH"
            export FONTCONFIG_FILE=${
              pkgs.makeFontsConf { fontDirectories = [ pkgs.dejavu_fonts ]; }
            }
            export HOME="$TMPDIR"
          '';
          checkPhase = ''
            runHook preCheck
            ctest --output-on-failure
            runHook postCheck
          '';

          meta = {
            description = "Cheat sheet for the shortcuts assigned in the running session";
            license = nixpkgs.lib.licenses.gpl3Plus;
            platforms = systems;
            mainProgram = "bindpeek";
          };
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        rec {
          bindpeek = mkPkg pkgs;
          default = bindpeek;
        }
      );

      # For anyone who wants the package inside their own nixpkgs set, which
      # is what the module's default `package` reads.
      overlays.default = final: _prev: {
        bindpeek = mkPkg final;
      };

      # Carries the package along, so importing this module works without also
      # adding the overlay. mkDefault, so `programs.bindpeek.package` can still
      # be pointed elsewhere.
      nixosModules.bindpeek =
        { pkgs, lib, ... }:
        {
          imports = [ ./nix/module.nix ];
          programs.bindpeek.package = lib.mkDefault self.packages.${pkgs.stdenv.hostPlatform.system}.default;
        };
      nixosModules.default = self.nixosModules.bindpeek;

      # `nix flake check` does look at nixosModules, but only at their shape: a
      # module carrying an option that does not exist passes it and fails first
      # in somebody's rebuild. Measured, not assumed. So the module is put into
      # real systems here and the outcome of every switch is asserted.
      checks = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          inherit (nixpkgs) lib;

          # A system that exists only to be evaluated: a root filesystem and a
          # boot loader are all the module system insists on.
          base = {
            boot.loader.grub.devices = [ "/dev/null" ];
            fileSystems."/" = {
              device = "/dev/null";
              fsType = "ext4";
            };
            system.stateVersion = "24.05";
            users.users.alice.isNormalUser = true;
          };

          # The flake output, which carries its own package.
          evalWith =
            extra:
            lib.nixosSystem {
              inherit system;
              modules = [
                self.nixosModules.default
                base
                extra
              ];
            };

          # The module on its own, where the option default decides where the
          # package comes from. Kept apart from the above on purpose: the flake
          # output sets the package with mkDefault, which outranks the option
          # default, so a case built on it never reads `pkgs.bindpeek` and
          # cannot say anything about the overlay. Measured with an overlay
          # that throws when touched, and it was not touched.
          evalDirect =
            extra:
            lib.nixosSystem {
              inherit system;
              modules = [
                ./nix/module.nix
                base
                extra
              ];
            };

          probe =
            evaluated:
            let
              inherit (evaluated) config;
              # Every entry the module put under the autostart directory, found
              # rather than named: the file name lives in the package and in the
              # module, and a third spelling here would let this check pass a
              # rename that leaves a link pointing nowhere.
              autostart = lib.filter (
                name: lib.hasPrefix "xdg/autostart/" name
              ) (builtins.attrNames config.environment.etc);
            in
            {
              # Forced below. Reading an ordinary option is not enough: an
              # undefined one is only reported once the closure is
              # instantiated, which is the whole reason this check exists.
              inherit (config.system.build.toplevel) drvPath;
              inherit autostart;
              package = builtins.any (
                p: (p.pname or "") == "bindpeek"
              ) config.environment.systemPackages;
              packageName = config.programs.bindpeek.package.pname or "";
              autostartCount = builtins.length autostart;
              members = config.users.groups.input.members;
            };

          # Bound rather than written out at both places it is needed: the
          # entry below is read from this very system, and a second copy of the
          # configuration could be switched without the other noticing.
          autoStartSystem = evalWith {
            programs.bindpeek.enable = true;
            programs.bindpeek.autoStart = true;
          };

          cases = [
            {
              name = "enabled installs the package and nothing more";
              system = evalWith { programs.bindpeek.enable = true; };
              expect = {
                package = true;
                autostartCount = 0;
                members = [ ];
                packageName = "bindpeek";
              };
            }
            {
              name = "disabled changes nothing at all";
              system = evalWith { programs.bindpeek.enable = false; };
              expect = {
                package = false;
                autostartCount = 0;
                members = [ ];
                packageName = "bindpeek";
              };
            }
            {
              name = "input access reaches the named user and nobody else";
              system = evalWith {
                programs.bindpeek.enable = true;
                programs.bindpeek.inputAccessFor = [ "alice" ];
              };
              expect = {
                package = true;
                autostartCount = 0;
                members = [ "alice" ];
                packageName = "bindpeek";
              };
            }
            {
              name = "autostart installs exactly one entry";
              system = autoStartSystem;
              expect = {
                package = true;
                autostartCount = 1;
                members = [ ];
                packageName = "bindpeek";
              };
            }
            {
              # Reads `pkgs.bindpeek`, which is what says anything about the
              # option default at all. A marker package is put there and has to
              # be the one that arrives.
              name = "the option default takes the package from the overlay";
              system = evalDirect {
                programs.bindpeek.enable = true;
                nixpkgs.overlays = [ (_final: _prev: { bindpeek = pkgs.hello; }) ];
              };
              expect = {
                package = false;
                autostartCount = 0;
                members = [ ];
                packageName = "hello";
              };
            }
            {
              # And the same road with the overlay this flake actually hands
              # out, or a typo in the attribute it defines would be nobody's
              # problem until somebody's rebuild.
              name = "this flake's own overlay supplies that package";
              system = evalDirect {
                programs.bindpeek.enable = true;
                nixpkgs.overlays = [ self.overlays.default ];
              };
              expect = {
                package = true;
                autostartCount = 0;
                members = [ ];
                packageName = "bindpeek";
              };
            }
          ];

          measure =
            case:
            let
              got = probe case.system;
              forced = builtins.seq got.drvPath got;
              wrong = lib.filterAttrs (name: want: forced.${name} != want) case.expect;
            in
            map (
              name:
              "${case.name}: ${name} is ${builtins.toJSON forced.${name}}, expected ${builtins.toJSON case.expect.${name}}"
            ) (builtins.attrNames wrong);

          # Imported on its own and without the overlay there is no package to
          # be had, and the module says so rather than letting Nix guess at a
          # name. Nothing else covers that message.
          missingPackage = builtins.tryEval (
            builtins.seq
              (evalDirect { programs.bindpeek.enable = true; }).config.programs.bindpeek.package
              null
          );

          failures =
            lib.concatMap measure cases
            ++ lib.optional missingPackage.success
              "the module without a package evaluated instead of saying so";

          # The file the autostart entry links to. Named by the module, not by
          # this check, and looked at in the build below: a link into the
          # package that points nowhere evaluates perfectly well and breaks
          # first at activation time.
          autostartTarget =
            autoStartSystem.config.environment.etc.${lib.head (probe autoStartSystem).autostart}.source;
        in
        {
          module =
            if failures != [ ] then
              throw ("bindpeek NixOS module:\n  " + lib.concatStringsSep "\n  " failures)
            else
              pkgs.runCommand "bindpeek-module-check" { } ''
                if [ ! -e ${autostartTarget} ]; then
                  echo "the autostart entry links to a file that is not there:"
                  echo "  ${autostartTarget}"
                  exit 1
                fi
                touch $out
              '';
        }
      );

      apps = forAllSystems (system: rec {
        bindpeek = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/bindpeek";
        };
        default = bindpeek;
      });

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            name = "bindpeek-dev";
            nativeBuildInputs = [
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
              pkgs.clang-tools # clang-format + clang-tidy
              pkgs.reuse # licence header compliance
              pkgs.desktop-file-utils # validates the .desktop entry
              pkgs.shellcheck # scripts/*.sh
              # Reads the workflow the way the service that runs it does, and
              # shellchecks the shell inside it, which nothing else here sees.
              pkgs.actionlint
              pkgs.qt6.qttools
            ];
            buildInputs = dependencies pkgs;

            shellHook = ''
              # QML runtime imports for build-tree runs. Installed binaries get
              # this baked in through wrapQtApps at packaging time.
              export QML2_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/lib/qt-6/qml''${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
              export QML_IMPORT_PATH="$QML2_IMPORT_PATH"
              # Layer-shell integration plugin, so the overlay becomes a real
              # layer surface regardless of what the system profile provides.
              export QT_PLUGIN_PATH="${pkgs.kdePackages.layer-shell-qt}/lib/qt-6/plugins''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
            '';
          };
        }
      );
    };
}
