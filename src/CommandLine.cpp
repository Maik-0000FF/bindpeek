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

// The end of the options. Qt takes everything behind it as a value, however it
// is spelled, so an option standing there is not one.
constexpr char kEndOfOptions[] = "--";

// True when the argument is written like an option: a dash with something
// behind it. A lone dash is not one, and neither is a value or a name that
// happens to stand on the line.
bool looksLikeOption(QLatin1String argument) {
    return argument.size() >= 2 && argument.startsWith(QLatin1Char('-'));
}

// The name an option goes by, without its dashes and without a value joined to
// it: "--source" and "--source=/x" both give "source".
//
// A run of short options gives the run, "vh" for -vh, which is on no caller's
// list: neither program has a short option of its own, so a run is never one
// of theirs and always ends in a printed line, whether Qt answers it or
// refuses it.
QString optionName(QLatin1String argument) {
    QString name(argument);
    while (name.startsWith(QLatin1Char('-'))) {
        name.remove(0, 1);
    }
    const qsizetype joined = name.indexOf(QLatin1Char('='));
    if (joined >= 0) {
        name.truncate(joined);
    }
    return name;
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

bool wantsTextOnly(int argc, char **argv, const QStringList &needingDisplay,
                   const QStringList &takingValue) {
    for (int i = 1; i < argc; ++i) {
        const QLatin1String argument(argv[i]);

        // Nothing behind this is an option any more, so nothing behind it can
        // end the run in a line of its own.
        if (argument == QLatin1String(kEndOfOptions)) {
            return false;
        }
        if (!looksLikeOption(argument)) {
            continue;
        }

        const QString name = optionName(argument);
        if (!needingDisplay.contains(name)) {
            return true;
        }

        // The next argument belongs to this option, whatever it is spelled
        // like. Stepping over it is what keeps a file called "-v" from being
        // read as a request for the version. A value joined by an equals sign
        // is already inside this argument and takes no second one.
        if (takingValue.contains(name) &&
            !argument.contains(QLatin1Char('='))) {
            ++i;
        }
    }
    return false;
}

} // namespace bindpeek
