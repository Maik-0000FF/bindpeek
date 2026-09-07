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

// True when this invocation is answered as text and needs no display: the
// informational options, plus any option the caller names in alsoText (each
// without its dashes).
//
// Asked before the application object exists, because a GUI one aborts where
// there is no display and asking a program its version over SSH is fair.
//
// takingValue names the options of the caller that are followed by a value, so
// that a value which happens to be spelled like an option is read as the value
// it is. Without it a file called "-v" handed to --source would answer yes
// here, the program would build a plain application object and then go on to
// put a window on the screen with it.
//
// What arrives here, and what it answers. The two lists are written as the
// caller names them, so "list" is alsoText and "source" takes a value:
//
//   argv                      also   value    answer  why
//   (none)                    -      -        false   nothing to show
//   --version                 -      -        true    informational
//   -v                        -      -        true    the short spelling
//   --help / -h / --help-all  -      -        true    informational
//   -vh / -hv                 -      -        true    a run of short options
//   -version                  -      -        true    a run holding -v
//   -xyz                      -      -        false   a run holding neither
//   --list                    -      -        false   not named
//   --list                    list   -        true    named by the caller
//   --source /x --version     -      source   true    stands on its own
//   --source --version        -      source   false   the value of --source
//   --environment -h          -      env.     false   the value again
//   --source=--version        -      source   false   joined, so a value
//   --source --version        -      -        true    unnamed, so an option
//   --source /x -v            -      source   true    one value, not the rest
//   --source                  -      source   false   a value never came
//   -- --version              -      -        false   behind the end
//   --version --              -      -        true    the end comes after
//   --source -- --version     -      source   true    the end taken as a value
//   -5v                       -      -        false   a digit, so not a run
//   --Version                 -      -        false   options are lowercase
//   ""                        -      -        false   an empty argument
bool wantsTextOnly(int argc, char **argv, const QStringList &alsoText = {},
                   const QStringList &takingValue = {});

} // namespace bindpeek
