| `src/watch/` | The service. It reads the event devices under `/dev/input` and never grabs them, so every key still goes where it was going. Its own program, without Qt |<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture

Three programs, two static libraries, one QML panel drawn from one set of
tokens, and one library that carries nothing but a header: the shape of the
record the service and the panel agree on. That last one is what keeps the
panel from reaching the code that opens devices. This is what lives where and
why it lives there rather than somewhere else.

## The three programs

```
bindpeek          the panel. Asks the service which modifiers are held, reads
                  the shortcuts, draws the list on a layer-shell surface, and
                  nothing else.

bindpeek-editor   the settings window and the tray icon. Starts and stops the
                  panel, writes the settings file the panel watches.

bindpeek-watch    the service that reads the event devices. Started by its
                  socket when a panel connects, ended when the last one leaves,
                  and run under an account that exists only while it does.
```

The first two are separate processes because the tray has to outlive the panel:
the tray switch is what starts it again, and a switch that ends its own program
has no way back.

The third is separate for a different reason. Reading the event devices means
being able to read every keystroke, and a right given to your account is a
right every program you run inherits, for as long as the account exists. Kept
in a program of its own, that ability belongs to the program instead, and lasts
as long as the program runs. The panel is not linked against libevdev at all,
which makes the line a property of the build rather than a habit.

## The tree

<!-- environments:begin -->

| Path | What is in it |
| --- | --- |
| `src/Source.*` | What a shortcut is, how modifiers are named and ordered, how binds are grouped. The vocabulary every backend speaks |
| `src/SourceMango.*`, `src/SourceHyprland.*`, `src/SourceSway.*`, `src/SourceKde.*` | One backend per session, each reading the place that session really uses |
| `src/Settings.*` | The settings file: reading it, the table of what every value may be, and writing the commented template |
| `src/Compositor.*` | Whether this session can show a layer surface at all, and what to say when it cannot |
| `src/AppInfo.*` | The one sentence that describes the program, and the two paths in the runtime directory both programs agree on |
| `src/SystemScheme.*` | The desktop's light/dark setting, asked of the portal. What `followSystemScheme` follows, and what picks the ink of the tray icon |
| `src/watch/` | The service. It reads the event devices under `/dev/input` and never grabs them, so every key still goes where it was going. Its own program, without Qt |
| `src/watch/Protocol.h` | The eight bytes the service and the panel agree on, and the only file both of them read |
| `src/WatchClient.*` | The panel's end of that socket: connect, read a record, pass it on. Opens no device, and links nothing that could |
| `src/OverlayController.*` | When the panel is on screen: the delay, what is held, what to show |
| `src/Appearance.*` | The settings turned into what the panel draws with, shared by the panel and the preview |
| `src/LayerPlacement.*` | Anchors and margins from the position setting, for the one surface a compositor places: the panel |
| `src/Theme.qml` | Every colour, size and distance the panel uses, in one place |
| `src/PanelBody.qml` | The plate itself. The same item the settings window shows in its preview |
| `src/Overlay.qml` | Where the plate sits and when, on the real panel |
| `src/editor/` | The settings window, its model, and the process it starts |
| `assets/systemd/` | The socket and service units, with the socket path and the program path filled in by the build |
| `nix/module.nix` | The NixOS module |
| `scripts/` | The shared halves of the install pair, and `check.sh` |
| `tests/` | The suite |

<!-- environments:end -->

## Why a library

`bindpeek_sources` is a static library holding the backends, the settings and
the compositor check. Everything in it can be exercised without a window and
without a running session, which is what the tests do. What needs the GUI
classes stays out of it and is compiled into the programs that need it, and into
the test that measures it.

## One panel, drawn twice

The preview in the settings window is not a drawing of the panel. It is
`PanelBody.qml`, the same item, fed by `Appearance`, the same class, reading
`Theme.qml`, the same tokens. A change to how the panel looks cannot look
different in the preview, because there is nothing there to disagree with.

Placement is the one thing they do not share, because there is only one surface
to place: `applyPlacement` anchors the panel where the settings say, while the
preview is an item inside the settings window and is laid out there. What keeps
those two agreeing is `Appearance` again, which answers the same questions about
the position for both.

## How the programs talk

They do not, except through two files and two sockets:

- **The settings file** is the channel. The settings window writes it, the panel
  watches it, and what can change without a restart is applied where it lands.
- **A lock file** in the runtime directory marks the running panel, so a second
  start steps aside instead of drawing a second panel over the first, and so the
  settings window can tell whether a panel is running and whether it is the one
  that belongs to this installation.
- **A socket** in the runtime directory belongs to the settings window. A second
  start connects to it and is done; the connection is the whole message, and the
  window that is already open comes forward.
- **The service's socket** carries eight bytes at a time in one direction: which
  modifiers are held, and whether some other key went down. The panel never
  writes to it, so the program holding the keyboards has nothing to read and no
  parser to read it with.

## What runs when

```
modifier goes down
   -> the service sees it and writes eight bytes to its socket
   -> WatchClient reads them and reports it
   -> OverlayController starts the delay
   -> delay elapses, the source is read again, the panel is shown
   -> another modifier goes down: the list narrows
   -> everything is released: the panel goes
```

The source is read at the last moment on purpose. It is the difference between
a cheat sheet that shows the configuration and one that shows the configuration
as it is right now.

## The gates

`scripts/check.sh` runs every check that can be run locally: formatting, licence
headers, the desktop entry, the workflow file, shellcheck, the build, the suite,
the translation catalogue, qmlformat, qmllint and clang-tidy. With `--nix` it
adds the flake. The automated checks on every push run the same script, plus the
build and the install pair in a container of each supported distribution.
