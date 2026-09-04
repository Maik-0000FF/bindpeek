<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installation

Three ways in: the script, Nix, or by hand. All of them build from this
checkout; there is no binary package yet.

## With the script

```bash
git clone https://github.com/Maik-0000FF/bindpeek.git
cd bindpeek
./install.sh
```

Supported families: Arch, Debian/Ubuntu, Fedora, openSUSE, including the usual
derivatives of each, on a release carrying Qt 6.7 or newer. On the Debian side
that is Ubuntu 26.04 and Debian trixie upwards. An older release stops at the
configure step and says which Qt it found, rather than failing later with
something that reads like a fault in the code.

### What gets installed

Two things, and they are worth keeping apart.

**The programs themselves** go to `/usr/local`: the three binaries, the desktop
entry and its icon, and the two units of the keyboard service. Nothing else
goes there. Two more files are written outside it, both in your own home: the
autostart entry, if you say yes to that question, and
`~/.config/bindpeek/bindpeek.conf`, which the program writes for itself on its
first start.

**The packages needed to build them** are whatever your distribution calls
CMake, Ninja, pkg-config, a C++20 compiler, Qt 6.7 or newer (base,
declarative, svg, wayland and the Linguist tools), layer-shell-qt, libevdev and
libsystemd. On Debian and its derivatives the QML runtime modules come as
packages of their own and are in the list as well.

**One more that is wanted rather than needed**: KDE's KConfig framework, which
only the KDE backend uses. It is listed and installed alongside the others, but
where a distribution does not carry it the script says so and carries on.
Declining it at the question does the same, as long as everything actually
needed is there; a No while something needed is still missing ends the run
instead. What the build makes of it is not guessed: it looks for the framework
itself and the configure step says whether it found one, so a copy installed
under some other name is used all the same. A machine with none has no KDE
Plasma 6 either, so nothing is lost there that could have been used.

The names differ per family and they live in exactly one place,
`scripts/_packages.sh`, which is the file the script reads and the file the
automated checks install from. They are deliberately not copied into this page:
a second list is one that goes out of date.

The script prints every one of them with a plus or a minus before it asks
anything, so you see what is missing and what would be installed, and can say
no. To see the same list beforehand, without installing:

```bash
( . scripts/_distro.sh; detect_distro_info
  . scripts/_packages.sh
  dependencies_for && printf '%s\n' "${DEPENDENCIES[@]}" \
    || echo "no package list for '$DISTRO'" )
```

The brackets are not decoration: reading those files sets a good many variables,
including everything in `/etc/os-release`, and inside brackets they are gone
again when the line ends instead of staying in your shell. The second half is
what a family with no list of its own says. That is NixOS, which builds from the
flake instead, and it is also a distribution the script does not recognise,
where the way in is the by-hand build further down.

Nothing is ever removed. What was installed to build with stays when the
programs go.

### What the script does, in order

1. Names the distribution it found, and refuses to run under `sudo`. It asks
   for the rights it needs where it needs them.
2. Lists the packages that are missing and asks before installing them.
3. Builds with CMake and Ninja into `build-install/`, a directory of its own so
   a developer's build tree is never installed from.
4. Asks the running session whether its shortcuts can be read, using the program
   it has just built. This only reports; it never stops the installation.
5. Installs to `/usr/local`, then stops any copy that was already running.
6. **Asks** whether to switch on the service that reads the keyboard. Say no and
   everything is installed but the panel will refuse to start; it is one command
   away at any time. Where there is no service manager running, as in a
   container, the units are installed and the command is named instead.
7. Offers to take away the `input` group if an earlier version put your account
   in it. Nothing here needs it any more, and while it is there every program
   your account runs can read every key you press.
8. **Asks** whether to start the tray with your session.

### With no terminal to ask on

Whether somebody is sitting there is not something a script can find out; what
it can find out is whether there is a terminal to put a question to, and that is
what it goes by. The question goes to the terminal itself in both directions,
rather than to the streams the run was handed, and that settles two things at
once:

- `./install.sh < answers.txt` is not answered by that file. A redirection is
  not somebody agreeing to anything, and reading one as an answer is how a
  script comes to install something nobody said yes to.
- `./install.sh | tee log` still shows the question. It goes to the terminal,
  not into the log, so nobody is left waiting at a cursor with nothing beside
  it.

What really has no terminal is a job, a container started without one, or a run
detached from the session, and a run in the background is caught as well.

It then gets as far as it can without asking anything. The first thing it wants
is administrative rights, and where sudo would want a password for them, the
script stops right there and says so, before a single package is touched. Where
the rights are given without a password, which is what a container or a rule of
its own does, it carries on, and every question falls through to the answer it
offers: the packages are installed, the service is not switched on, the group is
not touched, and the autostart is left as it was. The script says which of them
it skipped.

What it cannot tell apart is a terminal with nobody in front of it. A run in a
detached session, or one handed a terminal of its own by a wrapper, passes for
somebody who can answer, and then waits at sudo's password prompt like anybody
else would.

## Nix / NixOS

The flake builds the three programs and carries a NixOS module:

Two separate places, so this is two excerpts rather than one file:

```nix
# flake inputs
bindpeek.url = "github:Maik-0000FF/bindpeek";
```

```nix
# NixOS configuration. `inputs` is only a name inside a module because the
# flake was told to pass it in, with specialArgs = { inherit inputs; } on the
# nixosSystem call; without that line this fails with "undefined variable".
imports = [ inputs.bindpeek.nixosModules.default ];
programs.bindpeek = {
  enable = true;
  autoStart = true;
};
```

`enable` puts the three programs on `PATH` and switches on the service that
reads the keyboard, socket and all. That service is not a switch of its own,
because there is nothing to weigh: it grants no account anything, it runs
nothing until a panel connects, and without it the panel cannot see a modifier
and refuses to start.

`autoStart` is a switch, and off by default, because it starts a program in
every graphical session on the machine. It installs the desktop entry into
`/etc/xdg/autostart`.

An earlier version had `inputAccessFor`, which put the listed users in the
`input` group. It is gone, and a configuration still carrying it stops the
rebuild with a sentence saying so. Removing the option does not remove the
membership, so take that away as well, for each account that has it:

```bash
sudo gpasswd -d alice input
```

Just building it, without the module:

```bash
nix build            # result/bin/bindpeek, result/bin/bindpeek-editor
nix run .#bindpeek -- --list
```

## By hand

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo systemctl enable --now bindpeek-watch.socket
```

What has to be installed first, under whatever your distribution calls it:
CMake, Ninja, pkg-config, a C++20 compiler, Qt 6 (base, declarative, svg,
wayland and the Linguist tools), layer-shell-qt, libevdev and libsystemd. On
Debian and its derivatives the QML runtime modules are packaged separately and are needed as
well. KDE's KConfig framework is wanted on top of that and the build says so if
it is missing, leaving out the KDE backend and nothing else.
`scripts/_packages.sh` has the exact names for all four families, and it is the
same file the script and the automated checks read.

## Autostart under a bare compositor

<!-- environments:begin except kde -->

`~/.config/autostart` and `/etc/xdg/autostart` are read by desktop
environments. A compositor started from a script, such as sway, Hyprland or
mango, implements nothing of the sort, and there the autostart option has no
effect at all. Put a line in the compositor's own startup file instead:

<!-- environments:end -->

```bash
bindpeek-editor &
```

Under a compositor that kills the process group its startup file ran in, detach
it:

```bash
setsid -f bindpeek-editor
```

## Removing it again

```bash
./uninstall.sh
```

It removes what was installed, using the list CMake wrote while installing, then
the autostart entry, and asks before touching your settings. It does not remove
the packages that were installed to build with: other things on the machine want
Qt and CMake too.
