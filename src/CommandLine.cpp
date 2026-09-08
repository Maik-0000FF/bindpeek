// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CommandLine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLatin1Char>
#include <QLatin1String>
#include <QString>

namespace bindpeek {
namespace {

// The informational options, without their dashes: addHelpOption() takes help
// and help-all, addVersionOption() takes version.
constexpr const char *kInformationalLong[] = {"help", "help-all", "version"};

// The same two as single letters. Qt reads a run of short options as the
// options in it, so -vh is -v and -h.
constexpr char kInformationalShort[] = {'h', 'v'};

// The end of the options. Qt takes everything behind it as a value, however it
// is spelled, so an option standing there is not one.
constexpr char kEndOfOptions[] = "--";

constexpr QLatin1Char kDash('-');
constexpr QLatin1Char kJoin('=');

// A run of short options: exactly one dash, and something behind it. Two
// dashes make a long option and three make neither.
bool isShortRun(QLatin1String argument) {
    return argument.size() >= 2 && argument.at(0) == kDash &&
           argument.at(1) != kDash;
}

// A long option: exactly two dashes, and a name behind them.
bool isLongOption(QLatin1String argument) {
    return argument.size() >= 3 && argument.at(0) == kDash &&
           argument.at(1) == kDash && argument.at(2) != kDash;
}

// The name a long option goes by, without its dashes and without a value
// joined to it: "--source" and "--source=/x" both give "source".
QString longName(QLatin1String argument) {
    QString name(argument.mid(2));
    const qsizetype joined = name.indexOf(kJoin);
    if (joined >= 0) {
        name.truncate(joined);
    }
    return name;
}

// True when every letter of the run is one of the informational ones, so the
// whole run is answered and printed. A run holding anything else is left to
// the GUI application object: it may be one of Qt's own, which are cut out of
// the line before the parser sees them.
bool isInformationalRun(QLatin1String argument) {
    for (qsizetype at = 1; at < argument.size(); ++at) {
        bool known = false;
        for (const char letter : kInformationalShort) {
            if (argument.at(at) == QLatin1Char(letter)) {
                known = true;
                break;
            }
        }
        if (!known) {
            return false;
        }
    }
    return true;
}

// Whether this argument is the one option of Qt's that is followed by its
// value, in either spelling. A value joined by an equals sign is inside the
// argument and makes the text differ from the name, so those lines say no
// here and the argument behind them stays where it is, which is what Qt does
// with them as well. Whether an argument stands behind it at all is not asked
// here; the caller steps and runs out of line, which answers the same.
bool takesTheArgumentBehindIt(QLatin1String argument) {
    if (argument.size() < 2 || argument.at(0) != kDash) {
        return false;
    }
    const qsizetype name = argument.at(1) == kDash ? 2 : 1;
    return argument.mid(name) == QLatin1String(kOptionQtTakesWithValue);
}

bool isInformationalName(const QString &name) {
    for (const char *option : kInformationalLong) {
        if (name == QLatin1String(option)) {
            return true;
        }
    }
    return false;
}

} // namespace

void setApplicationIdentity() {
    QCoreApplication::setApplicationName(QLatin1String(BINDPEEK_PROGRAM_NAME));
    QCoreApplication::setApplicationVersion(QLatin1String(BINDPEEK_VERSION));
}

void prepareParser(QCommandLineParser &parser, const QString &description) {
    parser.setApplicationDescription(description);
    parser.addHelpOption();
    parser.addVersionOption();
}

bool wantsTextOnly(int argc, char **argv, const QStringList &alsoText,
                   const QStringList &takingValue) {
    for (int i = 1; i < argc; ++i) {
        const QLatin1String argument(argv[i]);

        // Nothing behind this is an option any more, so nothing behind it can
        // be one of these.
        if (argument == QLatin1String(kEndOfOptions)) {
            return false;
        }

        // Qt takes this one and the argument behind it out of the line before
        // any parser sees it, so what stands there is gone whatever it is
        // spelled like. Stepping over it is what keeps "--qmljsdebugger
        // --list" from being read as a request this run cannot answer. With
        // nothing behind it Qt takes nothing and refuses the name instead, and
        // the step runs off the end of the line, which ends the loop with the
        // same answer.
        if (takesTheArgumentBehindIt(argument)) {
            ++i;
            continue;
        }

        if (isLongOption(argument)) {
            const QString name = longName(argument);
            if (isInformationalName(name) || alsoText.contains(name)) {
                return true;
            }
            // The next argument belongs to this option, whatever it is spelled
            // like. Stepping over it is what keeps a file called "-v" from
            // being read as a request for the version. A value joined by an
            // equals sign is already inside this argument and takes no second
            // one.
            if (takingValue.contains(name) && !argument.contains(kJoin)) {
                ++i;
            }
            continue;
        }

        if (isShortRun(argument) && isInformationalRun(argument)) {
            return true;
        }
    }
    return false;
}

} // namespace bindpeek
