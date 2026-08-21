<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture

Two programs, one static library, one QML panel drawn from one set of tokens.
This is what lives where and why it lives there rather than somewhere else.

## The two programs

```
bindpeek          the panel. Reads the keyboard, reads the shortcuts, draws the
                  list on a layer-shell surface, and nothing else.

bindpeek-editor   the settings window and the tray icon. Starts and stops the
                  panel, writes the settings file the panel watches.
```

They are separate processes because the tray has to outlive the panel: the tray
switch is what starts it again, and a switch that ends its own program has no
way back.

## The tree

| Path | What is in it |
| --- | --- |
| `src/Source.*` | What a shortcut is, how modifiers are named and ordered, how binds are grouped. The vocabulary every backend speaks |
| `src/SourceMango.*`, `src/SourceHyprland.*`, `src/SourceKde.*` | One backend per session, each reading the place that session really uses |
| `src/Settings.*` | The settings file: reading it, the table of what every value may be, and writing the commented template |
| `src/Compositor.*` | Whether this session can show a layer surface at all, and what to say when it cannot |
| `src/AppInfo.*` | The one sentence that describes the program, and the two paths in the runtime directory both programs agree on |
| `src/SystemScheme.*` | The desktop's light/dark setting, asked of the portal. What `followSystemScheme` follows, and what picks the ink of the tray icon |
| `src/KeyboardWatch.*` | The event devices under `/dev/input`, read and never grabbed |
| `src/OverlayController.*` | When the panel is on screen: the delay, what is held, what to show |
| `src/Appearance.*` | The settings turned into what the panel draws with, shared by the panel and the preview |
| `src/LayerPlacement.*` | Anchors and margins from the position setting, for the one surface a compositor places: the panel |
| `src/Theme.qml` | Every colour, size and distance the panel uses, in one place |
| `src/PanelBody.qml` | The plate itself. The same item the settings window shows in its preview |
| `src/Overlay.qml` | Where the plate sits and when, on the real panel |
| `src/editor/` | The settings window, its model, and the process it starts |
| `nix/module.nix` | The NixOS module |
| `scripts/` | The shared halves of the install pair, and `check.sh` |
| `tests/` | The suite |

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

## How the two programs talk

They do not, except through two files and one socket:

- **The settings file** is the channel. The settings window writes it, the panel
  watches it, and what can change without a restart is applied where it lands.
- **A lock file** in the runtime directory marks the running panel, so a second
  start steps aside instead of drawing a second panel over the first, and so the
  settings window can tell whether a panel is running and whether it is the one
  that belongs to this installation.
- **A socket** in the runtime directory belongs to the settings window. A second
  start connects to it and is done; the connection is the whole message, and the
  window that is already open comes forward.

## What runs when

```
modifier goes down
   -> KeyboardWatch reports it
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
