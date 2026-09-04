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

So the keyboard is not read through Wayland at all. The event devices under
`/dev/input` are watched directly, below the compositor. They are read and
never grabbed, so every key still goes where it was going. What is looked at is
which modifiers are down and whether some other key was pressed, and nothing
else.

Reading them is not something the panel does. A program that reads those
devices can read every keystroke on the machine, and granting that to your
account would grant it to every other program you run, for as long as the
account exists. So a service of its own does the reading:

- It runs under an account the service manager makes when it starts and unmakes
  when it stops. No login on the machine gains anything by it being there.
- It is started by a socket when a panel connects, and it ends itself once the
  last panel has gone. Nothing holds a keyboard while nothing is showing.
- What leaves it is which modifiers are held and the bare fact that some other
  key went down. No key codes, no characters.
- It reads nothing from whoever connects, and it answers only somebody who is
  logged in at a screen of this machine.

The panel therefore holds no keyboard descriptor at all, and cannot: it is not
even linked against the library that would open one.

`bindpeek --keys` prints the held modifiers as they change and nothing else,
which is the quickest way to see whether the service can be reached.

## Knowing what they fire

Each supported session keeps its shortcuts somewhere else, and bindpeek reads
the place that session really uses:

<!-- environments:begin -->

| Session | Where the shortcuts come from |
| --- | --- |
| mango | the configuration the running compositor was started with, found from its own command line |
| Hyprland | the running instance, asked over its IPC socket |
| sway | the running instance, asked over its IPC socket. It answers with the text of its main configuration file alone, so the variables in it are resolved here, and a line that pulls in another file is reported rather than followed: those binds are not in the answer |
| KDE Plasma | `kglobalshortcutsrc`, read with KDE's own configuration library so the escaping is read the way KDE writes it |

<!-- environments:end -->

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

## Why three programs

`bindpeek` is the panel. `bindpeek-editor` is the settings window, and it
carries the tray icon. `bindpeek-watch` is the service that reads the keyboard.

The first two are separate because the tray has to outlive the panel. Switching
the panel off from the tray ends that process; if the two were one program, the
switch would take the tray with it and there would be nothing left to switch it
back on. The settings window drives the panel through the same settings file the
panel watches, so what you see in its preview and what appears on screen come
from the same code.

The third is separate so that reading the keyboard is something a program can
do rather than something your account can do. It starts when a panel connects
and ends when the last one leaves, and while it runs it belongs to an account
that exists only for it.

## What it does not do

- It opens no network connection of any kind. Every socket it touches is local:
  the compositor's own, the session bus for the light/dark setting and the tray
  icon, the service's socket for the modifiers, and one socket of its own so a
  second start of the settings window raises the window that is already open.
- It writes nowhere but `~/.config/bindpeek`, plus a lock file and that socket
  in the runtime directory.
- It reads no key but the modifiers. What the service hands out is which of the
  four are held and the bare fact that some other key went down, never which
  one, and nothing on the panel's side of that socket could ask for more.
- It runs no shortcut. The panel shows what a combination is bound to and has no
  way to carry it out; the key goes to the compositor and the compositor does
  what it was told. Two things it does start: the panel, which the settings
  window starts and stops from its tray, and your browser, if you click one of
  the three links in the about dialog.
