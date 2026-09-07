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
// informational options, plus any option the caller names (without dashes).
//
// Asked before the application object exists, because a GUI one aborts where
// there is no display and asking a program its version over SSH is fair.
//
// What arrives here, and what it answers:
//
//   argv                          alsoText   answer  why
//   (none)                        {}         false   nothing to show
//   --version                     {}         true    informational
//   -v                            {}         true    the short spelling
//   --help / -h / --help-all      {}         true    informational
//   --list                        {}         false   not named
//   --list                        {"list"}   true    named by the caller
//   --source /x --version         {}         true    seen anywhere in the line
//   --source=--version            {}         false   a value, not an option
//   -version                      {}         false   Qt does not take it either
//   --Version                     {}         false   options are lower case
//   ""                            {}         false   an empty argument
bool wantsTextOnly(int argc, char **argv, const QStringList &alsoText = {});

} // namespace bindpeek
