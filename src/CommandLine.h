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

// The one option name of Qt's that is followed by its value, spelled once here
// and read from this line by the list below and by the check that steps over
// what stands behind it, so the two cannot drift apart.
inline constexpr const char *kOptionQtTakesWithValue = "qmljsdebugger";

// The option names Qt takes for itself.
//
// An application object with a screen cuts these out of the command line
// before any parser sees it, in both spellings, so an option of this package
// carrying one of these names is never the one that is acted on. Which of the
// two loses is not worth working out: the answer moves with the spelling, with
// the application class, and with what else stands on the line, and none of
// the outcomes is one anybody asked for.
//
// Not only the object with a screen, either. Measured: a bare QCoreApplication
// takes -qmljsdebugger and its value as well, so that name is swallowed even
// on a run that was answered as text and built no window. It is the only name
// here measured to do that on the path without a screen, which is why it
// carries a name of its own: wantsTextOnly has to step over that argument, or
// an option standing there is read as one this run will act on while Qt has
// already taken it away. What the classes with a screen take together with the
// argument behind them is not said here, because measuring it needs a display.
// It is not that those never matter: the check runs at every start, before any
// application object exists, and one of Qt's own standing beside an
// informational option is what the paragraph at wantsTextOnly is about.
//
// The union of what the classes take, because the programs share this list:
// stylesheet, widgetcount, qdevel and qdebug belong to the settings window,
// which builds the class that takes the widest set, and geometry, title and
// icon are taken under X11. A name is refused here for all of them.
//
// This is a list of names to refuse, not one to act on, and that is what
// settles what belongs on it. A name too many costs nobody anything: no option
// of this package is going to be called "geometry". A name too few is a gap.
// So a name goes on as soon as there is reason to think Qt uses it, rather
// than once somebody has measured that it does; three of these would need an X
// display to measure and are on the list all the same.
//
// It follows that the list is allowed to age. It decides nothing, so falling
// behind Qt costs an assurance, never a working option.
//
// Held against a program's own options while that program is built, which is
// what src/main.cpp does with it, and read by the test rather than copied
// there.
inline constexpr const char *kOptionsQtTakes[] = {
    "geometry",
    "icon",
    "platform",
    "platformpluginpath",
    "platformtheme",
    "plugin",
    "qdebug",
    "qdevel",
    kOptionQtTakesWithValue,
    "qwindowgeometry",
    "qwindowicon",
    "qwindowtitle",
    "reverse",
    "session",
    "style",
    "stylesheet",
    "testability",
    "title",
    "widgetcount",
};

// Whether two option names are the same, usable while the program is built.
// std::strcmp is not required to be, and this is a handful of names against a
// handful more.
constexpr bool sameOptionName(const char *one, const char *other) {
    while (*one != '\0' && *one == *other) {
        ++one;
        ++other;
    }
    return *one == *other;
}

// Whether the name is one of Qt's, for the assurance a program makes about its
// own table of options.
constexpr bool isOptionQtTakes(const char *name) {
    for (const char *taken : kOptionsQtTakes) {
        if (sameOptionName(name, taken)) {
            return true;
        }
    }
    return false;
}

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
// that one cuts out almost nothing. Closing that would mean acting on Qt's
// list of names rather than only refusing them, for a line nobody writes: a
// stylesheet is not set in order to ask a program its version.
//
// Almost nothing, because -qmljsdebugger goes even there, and it takes the
// argument behind it with it whenever one stands there. That one argument is
// stepped over here, in both spellings, and only when no value is joined to
// it. Without the step, a line like "--qmljsdebugger --list" is read as a
// request for the listing, the plain application object is built for it, and
// the parser is then handed a line Qt has already emptied: nothing is set, and
// the run walks on into the panel with an application object that has no
// screen.
//
// Six shapes, each measured against the built programs, and what each run
// does:
//
//   --qmljsdebugger --version    Qt takes both, so nothing text-only is left
//                                and the run takes the path that shows the
//                                panel
//   -qmljsdebugger --version     the same in one dash
//   --qmljsdebugger=port:1 --v…  the value is joined, so --version stands and
//                                the version is printed
//   --qmljsdebugger port:1 --v…  the value stands between, so --version stands
//                                as well and the version is printed
//   --version --qmljsdebugger    answered at --version before the step is
//                                reached, and the name behind it is one Qt
//                                refuses, so the run ends on that refusal
//   --qmljsdebugger              nothing stands behind it, so Qt takes nothing
//                                and the name is one it refuses: measured on
//                                qtpaths6, which answers "Unknown option
//                                'qmljsdebugger'"
//
// The last shape is why the step is written as a step and not as a refusal of
// its own: it walks off the end of the line and ends the loop, which is the
// answer that shape has anyway.
//
// Which leaves one rule for whoever adds an option to a program: it must not
// be given a name Qt already uses. Those are listed at the top of this file,
// and a program that keeps a table of its own options holds it against them
// while it is built, which is what the panel does.
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
//   --qmljsdebugger --version -      -        false   Qt takes both
//   -qmljsdebugger --version  -      -        false   the same in one dash
//   --qmljsdebugger=p -v      -      -        true    joined, so -v stands
//   --qmljsdebugger p -v      -      -        true    the value stands between
//   -v --qmljsdebugger        -      -        true    -v is read before it
//   --qmljsdebugger           -      -        false   nothing behind it to take
//   --list                    list   -        true    named by the caller
//   --Version / --nope        -      -        false   unknown, so left to Qt
//   -source                   -      -        false   one dash, so a run
//   ---source                 -      -        false   three dashes, neither
//   --source /x --version     -      source   true    stands on its own
//   --source --version        -      source   false   the value of --source
//   --source=--version        -      source   false   joined, so a value
//   --source --qmljsdebugger  -      source   true    the caller's step comes
//     --version                                       first, and Qt then takes
//                                                     the other pair and
//                                                     leaves --source without
//                                                     a value: measured, the
//                                                     run ends on Qt's own
//                                                     line about the missing
//                                                     value, which the plain
//                                                     application object
//                                                     prints without a display
//   --source /x -v            -      source   true    one value, not the rest
//   --source                  -      source   false   the value never came
//   -- --version              -      -        false   behind the end
//   --version --              -      -        true    the end comes after
//   --source -- --version     -      source   true    the end taken as value
//   --qmljsdebugger -- --v…   -      -        true    Qt takes the end as its
//                                                     value, so the option
//                                                     behind it is one and the
//                                                     step has to come before
//                                                     the end is read
//   -                         -      -        false   no option, a lone dash
//   ""                        -      -        false   an empty argument
bool wantsTextOnly(int argc, char **argv, const QStringList &alsoText = {},
                   const QStringList &takingValue = {});

} // namespace bindpeek
