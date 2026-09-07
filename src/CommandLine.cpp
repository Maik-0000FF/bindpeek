// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CommandLine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLatin1String>
#include <QString>

#include <cctype>
#include <iterator>

namespace bindpeek {
namespace {

// The informational options as Qt's parser takes them, in two halves because
// they are written in two ways.
//
// Written out here because the check below runs before the parser exists and
// therefore cannot ask it. A spelling missing from here is one that reaches a
// GUI application object which may have no display to be built in, so the long
// half is measured against the parser in the tests rather than trusted.
//
// addHelpOption() takes --help and --help-all, addVersionOption() takes
// --version.
constexpr const char *kInformationalLong[] = {
    "--help",
    "--help-all",
    "--version",
};

// The single letters of the same two options. Qt reads a run of short options
// as the options in it, so -vh is -v and -h, and each letter of such a run is
// looked at on its own.
constexpr char kInformationalShort[] = {'h', 'v'};

// The dashes an option is written with, once, so the caller names its own
// options the way the rest of the program does: without them.
constexpr char kOptionPrefix[] = "--";

// The end of the options. Qt takes everything behind it as a value, however it
// is spelled, so an option standing there is not one.
constexpr char kEndOfOptions[] = "--";

// True when the argument is a run of short options that holds an informational
// one. A single -v is the shortest run of the same kind.
//
// Letters only, which is what a short option is made of, so that a negative
// number and a lone dash are not read as a run of them. Qt is looser here and
// reads -5v as a run as well, and the difference costs nothing: a run holding
// a character no option goes by is refused by the parser, which is a printed
// line either way and never a window.
//
// Measured rather than assumed, because the answers are not obvious:
//
//   bindpeek -version  ->  Unknown options: e, r, s, i, o, n.
//   bindpeek -xyz      ->  Unknown options: x, y, z.
//   bindpeek -5v       ->  Unknown option '5'.
bool holdsShortOption(const char *argument) {
    if (argument[0] != '-' || argument[1] == '\0' || argument[1] == '-') {
        return false;
    }

    bool informational = false;
    for (const char *letter = argument + 1; *letter != '\0'; ++letter) {
        if (std::isalpha(static_cast<unsigned char>(*letter)) == 0) {
            return false;
        }
        for (const char known : kInformationalShort) {
            if (*letter == known) {
                informational = true;
            }
        }
    }
    return informational;
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
    QStringList text;
    text.reserve(static_cast<qsizetype>(std::size(kInformationalLong)) +
                 alsoText.size());
    for (const char *option : kInformationalLong) {
        text.append(QLatin1String(option));
    }
    for (const QString &option : alsoText) {
        text.append(QLatin1String(kOptionPrefix) + option);
    }

    QStringList valued;
    valued.reserve(takingValue.size());
    for (const QString &option : takingValue) {
        valued.append(QLatin1String(kOptionPrefix) + option);
    }

    // Compared whole, so an option joined to its value by an equals sign is
    // read as the one argument it is and never as the option it contains.
    for (int i = 1; i < argc; ++i) {
        const QLatin1String argument(argv[i]);
        // Nothing behind this is an option any more, so nothing behind it can
        // be one of these.
        if (argument == QLatin1String(kEndOfOptions)) {
            return false;
        }
        if (text.contains(argument) || holdsShortOption(argv[i])) {
            return true;
        }
        // The next argument belongs to this option, whatever it is spelled
        // like. Stepping over it is what keeps a file called "-v" from being
        // read as a request for the version.
        if (valued.contains(argument)) {
            ++i;
        }
    }
    return false;
}

} // namespace bindpeek
