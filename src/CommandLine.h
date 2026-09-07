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
// there is no display, and asking a program its version over SSH is fair.
//
// takingValue names the options of the caller that are followed by a value, so
// that a value spelled like an option is read as the value it is. Without it a
// file called "-v" handed to --source would answer yes here, and the program
// would build a plain application object and then go on to put a window on the
// screen with it.
//
// Only what is certainly text is answered yes. An argument that is written
// like an option and stands on neither list is left to the GUI application
// object, because Qt cuts its own options out of the line before the parser
// ever sees them, in both spellings: -platform and --platform, -style,
// -session, -reverse and the rest are gone by then. Measured, and it is what
// keeps those working. Reading an unknown option as text instead would answer
// a mistyped one over SSH and break every one of Qt's.
//
// What it costs is that a mistyped option without any display at all ends the
// run without a word, because Qt cannot build the application object that
// would have printed the line. That is Qt's behaviour and every program built
// on it has it; the alternative is guessing which unknown options are Qt's.
//
// And one of Qt's own standing beside an informational one is refused rather
// than acted on: "-style Fusion --version" answers "Unknown options: s, t, y,
// l, e." because the version wins, the plain application object is built, and
// that one cuts nothing out. Closing this would mean carrying Qt's list of
// options here, which ages, for a line nobody writes: a stylesheet is not set
// in order to ask a program its version.
//
// Which leaves one rule for whoever adds an option to a program: it must not
// be given a name Qt already uses, style, session, reverse, platform and the
// rest of them. Named in alsoText, such an option would answer yes here, the
// plain application object would be built, and Qt's own would be refused
// instead of acted on. Nothing here can catch that, because the two are the
// same word by then; the place to see it is the table the option is added to.
//
// What arrives here, and what it answers. The lists are written as the panel
// names them, so "list" is text and "source" takes a value:
//
//   argv                      text   value    answer  why
//   (none)                    -      -        false   nothing to show
//   --version / --help        -      -        true    informational
//   --help-all                -      -        true    informational
//   -v / -h / -vh / -hv       -      -        true    a run of those letters
//   -xyz / -5v                -      -        false   a run of others
//   -platform / --platform    -      -        false   Qt's own, left to Qt
//   --list                    list   -        true    named by the caller
//   --Version / --nope        -      -        false   unknown, so left to Qt
//   -source                   -      -        false   one dash, so a run
//   ---source                 -      -        false   three dashes, neither
//   --source /x --version     -      source   true    stands on its own
//   --source --version        -      source   false   the value of --source
//   --source=--version        -      source   false   joined, so a value
//   --source /x -v            -      source   true    one value, not the rest
//   --source                  -      source   false   the value never came
//   -- --version              -      -        false   behind the end
//   --version --              -      -        true    the end comes after
//   --source -- --version     -      source   true    the end taken as value
//   -                         -      -        false   no option, a lone dash
//   ""                        -      -        false   an empty argument
bool wantsTextOnly(int argc, char **argv, const QStringList &alsoText = {},
                   const QStringList &takingValue = {});

} // namespace bindpeek
