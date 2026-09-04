<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Troubleshooting

Every message the programs print goes to the terminal they were started from.
If they start with your session, that terminal is the session journal.

## The panel never appears

**Start it from a terminal and read what it says.** It refuses out loud rather
than failing quietly, and the sentence usually is the answer:

```bash
bindpeek
```

### "The keyboard service is not answering"

The panel does not read the keyboard itself. A service does, and it is started
by a socket unit that is not enabled yet:

```bash
sudo systemctl enable --now bindpeek-watch.socket
```

The socket is what has to be enabled, not the service: it holds the listening
end while nothing is running and starts the service when a panel connects.
Whether it is there at all:

```bash
systemctl status bindpeek-watch.socket
```

Then check that the modifiers actually arrive:

```bash
bindpeek --keys
```

It prints the held modifiers as they change and nothing else. If that stays
silent while you press Shift, look at what the service says:

```bash
journalctl -u bindpeek-watch.service -b
```

### The panel used to work and stopped after an upgrade

A panel that was already running keeps talking to the service it started with.
When the package is replaced under it, the two can disagree about what the
records look like, and the panel says so once and then keeps quiet. Start it
again, from the tray or from a terminal.

### The `input` group is still on your account

Earlier versions asked for it, and nothing removes it by itself. While it is
there, every program your account runs can read every key you press, including
what you type into other windows. bindpeek has no use for it any more:

```bash
sudo gpasswd -d "$USER" input
```

`./install.sh` and `./uninstall.sh` both offer to do this. The membership is
gone from your next login, not from the one you are in.

### "GNOME/Mutter does not implement wlr-layer-shell"

There is nothing to configure here. The panel needs a protocol that
GNOME/Mutter does not carry, and coming up as an ordinary window would take the
focus and break the very shortcut you were looking at.

<!-- environments:begin -->

`bindpeek --list` needs no screen at all and prints the same list as text. On a
session it cannot recognise, name the one to read:
`--environment mango`, `hyprland`, `sway` or `kde`.

<!-- environments:end -->

### "No supported environment detected"

The session was not recognised. Force one:

<!-- environments:begin -->

```bash
bindpeek --environment mango      # or hyprland, sway, or kde
```

<!-- environments:end -->

<!-- environments:begin -->

If that works, the detection is what is missing, and an issue with the output of
`env | grep -i -E 'xdg|wayland|hyprland|mango|sway|kde'` is worth opening.

<!-- environments:end -->

### "bindpeek is already running as process N"

One panel at a time, on purpose: two would be one panel drawn twice and
impossible to tell apart. If the process it names is gone, the lock file in the
runtime directory is stale, which a logout clears.

## The panel is there but empty, or shows nothing useful

Ask for the same list as text; it comes from exactly the same reader:

```bash
bindpeek --list
```

- **Nothing at all**: the session's configuration was found but held no
  shortcut this program recognises. On mango, only lines beginning with `bind=`
  count; `mousebind=` and `axisbind=` are not keyboard shortcuts. On sway, of
  the four words that bind something, only `bindsym` counts; the point below
  says why.
- **Fewer than you have bound, on sway**: five of the reasons are counted in a
  line above the list rather than passed over. What an `include` line pulls in
  is not part of what bindpeek reads. A pointer button is not a key. A
  `bindcode` names a key by its number and has no name to show. A bind wanting
  `Mod2`, `Mod3`, `Mod5` or `Lock` cannot be followed, because the panel reads
  Super, Ctrl, Alt and Shift and nothing else, and showing it under the
  modifiers it does read would put it under a combination that does not fire
  it. And a line with nothing to run, or with no key left once the group is
  taken off it, binds nothing that could be shown.

  `bindswitch` and `bindgesture` are left out as well and are not counted: a
  lid and a touchpad are not keys, so a note about them would say something is
  missing where nothing is.
- **Raw action names in the right column**: those are actions with no
  description of their own, and no text to derive one from either. See
  [Configuration](CONFIGURATION.md#where-the-text-in-the-panel-comes-from).
- **Headings with more in them than you expected**: a mango heading is the text
  of your own `# --- ... ---` comment, taken exactly as it stands. A sway
  heading is the name of the mode the bind stands in.

## The panel shows an old configuration

It should not: the source is read again immediately before every appearance.
Release the modifier and hold it again. If it still shows the old state, the
file bindpeek reads is not the file you edited; `bindpeek --list` prints what it
found and is the quickest way to see which one that is.

Note that a configuration linked in from somewhere else, as a package manager or
a dotfiles setup may do, is only as current as the last time that link was
rebuilt.

## A changed setting does nothing

Most settings apply the moment the file is saved. Two do not:

- The panel is only redrawn when it is next shown, so changes to what it holds
  are seen on the next hold.
- Nothing at all happens if `overlayEnabled` is `false`. The tray writes that
  line, and it survives a logout.

## The settings window opens by itself at login

That is the settings window telling you it found no tray. Its icon is how the
panel is switched on and off, so with nowhere to put one the window itself is
the only way in, and it waits ten seconds for a tray before standing in. On a
session that starts its bar and its programs at once, the bar usually arrives
first; if it does not, the message in the journal says so. The panel is a
separate program and has nothing to do with any of this.

If the session has no tray at all, this is what it looks like, and the window is
the way in.

## The tray icon is invisible or the wrong colour

The icon comes in two inks and is picked by the desktop's light/dark setting,
read from the desktop portal. A session that reports no scheme gets the dark
ink. Where a portal is missing, the tray may also be missing entirely, and the
settings window will open by itself as above.

## Nothing here fits

Open an issue with:

```bash
bindpeek --list 2>&1 | head -40
bindpeek --keys        # a few lines, then Ctrl+C
echo "$XDG_CURRENT_DESKTOP / $XDG_SESSION_TYPE / $WAYLAND_DISPLAY"
```

and the part of your compositor's configuration the shortcuts live in.
