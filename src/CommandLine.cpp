// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CommandLine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLatin1String>
#include <QString>

#include <iterator>

namespace bindpeek {
namespace {

// The informational options in every spelling Qt accepts: addVersionOption()
// takes -v and --version, addHelpOption() takes -h and --help and answers
// --help-all as well.
//
// Written out here because the check below runs before the parser exists and
// therefore cannot ask it. A spelling missing from this list is one that
// reaches a GUI application object which may have no display to be built in,
// so the list is measured against the parser in the tests rather than trusted.
constexpr const char *kInformationalOptions[] = {
    "--help", "-h", "--help-all", "--version", "-v",
};

// The dashes an option is written with, once, so the caller names its own
// options the way the rest of the program does: without them.
constexpr char kOptionPrefix[] = "--";

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

bool wantsTextOnly(int argc, char **argv, const QStringList &alsoText) {
    QStringList text;
    text.reserve(static_cast<qsizetype>(std::size(kInformationalOptions)) +
                 alsoText.size());
    for (const char *option : kInformationalOptions) {
        text.append(QLatin1String(option));
    }
    for (const QString &option : alsoText) {
        text.append(QLatin1String(kOptionPrefix) + option);
    }

    // Compared whole. An option that carries a value arrives either as two
    // arguments or joined by an equals sign, and neither of these takes one,
    // so a --source whose value happens to read like one of them is a value
    // and stays one.
    for (int i = 1; i < argc; ++i) {
        if (text.contains(QLatin1String(argv[i]))) {
            return true;
        }
    }
    return false;
}

} // namespace bindpeek
