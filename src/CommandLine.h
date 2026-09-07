// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QCommandLineParser;
class QString;

namespace bindpeek {

// What the programs of this package say about themselves on the command line.
//
// The panel and the settings window are two binaries with two sets of options,
// but --help and --version are the same question and have to give the same
// answer. They are set up here so that neither program carries its own list of
// them and a third one cannot answer differently.
//
// The keyboard service does not come through here. It carries no Qt at all,
// which is the argument for trusting the one program that holds the keyboard
// descriptors, and answers the same two options in its own main with the same
// two values from the build.

// Tells the application object what this binary is called and which version it
// was built as, both from the build. Qt answers --version with exactly these
// two, so it has to be said before the parser runs.
//
// The name is the binary's, and differs per program. It is not the name of the
// product, which is one for all three and is what AppInfo::name() answers and
// the about dialog shows.
void setApplicationIdentity();

// Gives the parser its description and the two informational options, and
// nothing else: the options a program has of its own are added by that program
// afterwards, and it calls process() itself once they are all in.
//
// Takes the parser rather than returning one, because QCommandLineParser can
// neither be copied nor moved.
void prepareParser(QCommandLineParser &parser, const QString &description);

// True when this invocation ends in a printed line and needs no display.
//
// Asked before the application object exists, because a GUI one aborts where
// there is no display, and asking a program its version over SSH is fair.
//
// Read the other way round from what it sounds like. It does not carry a list
// of what is informational: such a list only ever holds what somebody thought
// to write down, and Qt takes more spellings than that. -vh is -v and -h, and
// an option that is none is refused in a printed line as well. So the caller
// names the opposite, the few options of its own that carry on to a window,
// and everything else written like an option ends in text.
//
// needingDisplay names those options, without their dashes. An option that is
// not in the list either prints and stops or is refused by the parser, and
// both are lines rather than windows.
//
// takingValue names the options that are followed by a value, so that a value
// spelled like an option is read as the value it is. Without it a file called
// "-v" handed to --source would answer yes here, and the program would build a
// plain application object and then go on to put a window on the screen.
//
// What arrives here, and what it answers. The lists are written as the panel
// names them, so "source" both needs a display and takes a value:
//
//   argv                      display   value    answer  why
//   (none)                    -         -        false   nothing to show
//   --version                 -         -        true    not on the list
//   -v / -vh / -hv            -         -        true    nor is a run of them
//   --help / --help-all       -         -        true    nor these
//   -xyz / --Version          -         -        true    refused, still a line
//   --list                    -         -        true    prints and stops
//   --environment hyprland    environ.  environ. false   carries on to a window
//   --source /x --version     source    source   true    stands on its own
//   --source --version        source    source   false   the value of --source
//   --environment -h          environ.  environ. false   the value again
//   --source=--version        source    source   false   joined, so a value
//   --source /x -v            source    source   true    one value, not the
//   rest
//   --source                  source    source   false   a value never came
//   -- --version              -         -        false   behind the end
//   --version --              -         -        true    the end comes after
//   --source -- --version     source    source   true    the end taken as value
//   -                         -         -        false   no option, a lone dash
//   ""                        -         -        false   an empty argument
bool wantsTextOnly(int argc, char **argv,
                   const QStringList &needingDisplay = {},
                   const QStringList &takingValue = {});

} // namespace bindpeek
