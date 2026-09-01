<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# How it works

Three things have to happen for a cheat sheet to be useful while you are holding
the keys it is about: something has to know which keys are down, something has
to know what they fire, and the panel has to be on screen without getting in the
way. Each of them has a catch, and this is how bindpeek answers them.

## Knowing which modifiers are down

A Wayland program is told nothing about the keyboard until the compositor gives
it the focus. That is the protocol working as intended, and it is fatal here:
the panel would have to take the focus to see the modifier, and then the
shortcut you are asking about would fire into the panel instead of the program
you meant.

So the keyboard is not read through Wayland at all. bindpeek opens the event
devices under `/dev/input` and watches them directly, below the compositor. It
reads and never grabs, so every key still goes where it was going. What it looks
at is which modifiers are down and whether some other key was pressed, and
nothing else.

That is what membership in the `input` group is for, and it is worth being plain
about the price:

> Every program you run gains read access to every input device, and can record
> every keystroke on the machine, including what you type into other windows.
> The grant is per account, not per program, and Linux has no narrower one for
> this.

`bindpeek --keys` prints the held modifiers as they change and nothing else,
which is the quickest way to see whether the devices can be read at all.

## Knowing what they fire

Each supported session keeps its shortcuts somewhere else, and bindpeek reads
the place that session really uses:

| Session | Where the shortcuts come from |
| --- | --- |
| mango | the configuration the running compositor was started with, found from its own command line |
| Hyprland | the running instance, asked over its IPC socket |
| sway | the running instance, asked over its IPC socket. It answers with the text of its main configuration file alone, so the variables in it are resolved here, and a line that pulls in another file is reported rather than followed: those binds are not in the answer |
| KDE Plasma | `kglobalshortcutsrc`, read with KDE's own configuration library so the escaping is read the way KDE writes it |

The source is read again immediately before every appearance, so a
configuration you have just edited is on screen the next time you hold a
modifier. Nothing is cached and nothing has to be restarted.

Where a session offers no description for a shortcut, one is worked out from the
action itself. What that means for what you see is in
[Configuration](CONFIGURATION.md#where-the-text-in-the-panel-comes-from).

## Being on screen without being in the way

The panel is a `wlr-layer-shell` surface on the overlay layer. Three properties
make it a bystander:

- **Keyboard interactivity is off.** The compositor sends it no key events at
  all, so the combination you are holding keeps working.
- **The input region is empty.** The pointer falls straight through to whatever
  is underneath. You cannot click the panel, and the focus cannot move to it.
- **It reserves no space.** Nothing is pushed aside to make room.

This is also the one hard requirement: `wlr-layer-shell` is a Wayland protocol
that every wlroots-based compositor implements, as do Hyprland, KWin and niri,
and GNOME/Mutter does not, nor Cinnamon, whose compositor is a fork of Mutter.
On a session without it, bindpeek says which session
it found and what is missing, rather than coming up as an ordinary window with a
title bar.

## Why two programs

`bindpeek` is the panel. `bindpeek-editor` is the settings window, and it
carries the tray icon.

They are separate because the tray has to outlive the panel. Switching the panel
off from the tray ends that process; if the two were one program, the switch
would take the tray with it and there would be nothing left to switch it back
on. The settings window drives the panel through the same settings file the
panel watches, so what you see in its preview and what appears on screen come
from the same code.

## What it does not do

- It opens no network connection of any kind. Every socket it touches is local:
  the compositor's own, the session bus for the light/dark setting and the tray
  icon, and one socket of its own so a second start of the settings window
  raises the window that is already open.
- It writes nowhere but `~/.config/bindpeek`, plus a lock file and that socket
  in the runtime directory.
- It runs no shortcut. The panel shows what a combination is bound to and has no
  way to carry it out; the key goes to the compositor and the compositor does
  what it was told. Two things it does start: the panel, which the settings
  window starts and stops from its tray, and your browser, if you click one of
  the three links in the about dialog.
