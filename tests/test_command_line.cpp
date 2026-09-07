// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the check that decides whether an invocation ends in a printed line
// and therefore needs no display.
//
// Two tests do it from two sides. The table below is the written contract, one
// row per case. The generated one holds the contract itself against Qt: for
// every spelling it makes up, it asks the parser whether that line ends in a
// printed one, and where it does, the check has to say so too. A written table
// only ever holds what somebody thought of; -vh was found by hand and not by
// a test, which is what the generated one is here to stop.

#include "CommandLine.h"

#include <QByteArray>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include <vector>

using namespace bindpeek;

namespace {

// The command line as the C runtime hands it over: the program's own name
// first, which is why the check starts at the second entry.
constexpr char kProgram[] = "bindpeek";

// The letters the generated spellings are built from: one of the two Qt
// answers, one it does not know, and one that is not a letter at all.
constexpr char kAlphabet[] = {'v', 'h', 'x', '5'};

// How long a generated run of short options gets. Two is enough for -vh, the
// spelling that started this; three costs nothing and covers a run where the
// known letter stands in the middle.
constexpr int kLongestRun = 3;

// Builds an argv from the arguments, program name included, and keeps the
// bytes alive for as long as the pointers are looked at.
class Line {
public:
    explicit Line(const QList<QByteArray> &arguments,
                  const QByteArray &program = QByteArray(kProgram)) {
        m_bytes.append(program);
        m_bytes.append(arguments);
        m_argv.reserve(m_bytes.size());
        for (QByteArray &argument : m_bytes) {
            m_argv.push_back(argument.data());
        }
    }

    int argc() const { return static_cast<int>(m_argv.size()); }
    char **argv() { return m_argv.data(); }

private:
    QList<QByteArray> m_bytes;
    std::vector<char *> m_argv;
};

// Every run of short options up to kLongestRun, plus the long spellings worth
// asking about. Made rather than listed, so a spelling nobody thought of is
// still asked.
QList<QByteArray> spellings() {
    QList<QByteArray> made;
    QList<QByteArray> runs{QByteArray()};
    for (int length = 0; length < kLongestRun; ++length) {
        QList<QByteArray> longer;
        for (const QByteArray &run : runs) {
            for (const char letter : kAlphabet) {
                longer.append(run + letter);
            }
        }
        for (const QByteArray &run : longer) {
            made.append(QByteArray("-") + run);
        }
        runs = longer;
    }

    made.append({"--help", "--help-all", "--version", "--Version", "--ver",
                 "--nope", "-", "--"});
    return made;
}

} // namespace

class TestCommandLine : public QObject {
    Q_OBJECT

private slots:
    void readsTheLine_data();
    void readsTheLine();
    void theProgramsOwnNameIsNotAnArgument();
    void saysTextWhereverTheParserPrints_data();
    void saysTextWhereverTheParserPrints();
    void identityComesFromTheBuild();
};

// The table from the header, as it is run.
void TestCommandLine::readsTheLine_data() {
    QTest::addColumn<QList<QByteArray>>("arguments");
    QTest::addColumn<QStringList>("needingDisplay");
    QTest::addColumn<QStringList>("takingValue");
    QTest::addColumn<bool>("expected");

    const QStringList none;
    const QStringList source{QStringLiteral("source")};
    const QStringList environment{QStringLiteral("environment")};

    QTest::newRow("nothing to show")
        << QList<QByteArray>{} << none << none << false;
    QTest::newRow("--version")
        << QList<QByteArray>{"--version"} << none << none << true;
    QTest::newRow("-v") << QList<QByteArray>{"-v"} << none << none << true;
    QTest::newRow("--help")
        << QList<QByteArray>{"--help"} << none << none << true;
    QTest::newRow("--help-all")
        << QList<QByteArray>{"--help-all"} << none << none << true;
    // Qt reads a run of short options as the options in it, and neither
    // program has a short option of its own, so a run is always a line.
    QTest::newRow("a run of short options")
        << QList<QByteArray>{"-vh"} << none << none << true;
    QTest::newRow("the same run the other way round")
        << QList<QByteArray>{"-hv"} << none << none << true;
    // Refused by the parser, which is a printed line as well. Answering false
    // here is what made these die without a word where there is no display.
    QTest::newRow("a run of others")
        << QList<QByteArray>{"-xyz"} << none << none << true;
    QTest::newRow("a digit in the run")
        << QList<QByteArray>{"-5v"} << none << none << true;
    QTest::newRow("the wrong case")
        << QList<QByteArray>{"--Version"} << none << none << true;
    QTest::newRow("a plain mistype")
        << QList<QByteArray>{"--unknownopt"} << none << none << true;
    // An option of the caller that prints and stops is not on the list, so it
    // needs no naming to be answered as text.
    QTest::newRow("prints and stops")
        << QList<QByteArray>{"--list"} << environment << environment << true;
    // One that carries on to a window is, and stops the line being text.
    QTest::newRow("carries on to a window")
        << QList<QByteArray>{"--environment", "hyprland"} << environment
        << environment << false;
    QTest::newRow("stands on its own")
        << QList<QByteArray>{"--source", "/x", "--version"} << source << source
        << true;
    // The one the value list exists for: a file named like an option is a
    // file, and reading it as a request for the version would build a plain
    // application object for a run that goes on to put a window on the screen.
    QTest::newRow("the value of --source")
        << QList<QByteArray>{"--source", "--version"} << source << source
        << false;
    QTest::newRow("the value of --environment")
        << QList<QByteArray>{"--environment", "-h"} << environment
        << environment << false;
    QTest::newRow("joined, so a value")
        << QList<QByteArray>{"--source=--version"} << source << source << false;
    // The step over a value is one argument, not everything after it.
    QTest::newRow("only the one value is stepped over")
        << QList<QByteArray>{"--source", "/x", "-v"} << source << source
        << true;
    // Everything behind the end of the options is a value to Qt, so an option
    // standing there is not answered and must not be read as one here.
    QTest::newRow("behind the end of the options")
        << QList<QByteArray>{"--", "--version"} << none << none << false;
    QTest::newRow("the end comes after it")
        << QList<QByteArray>{"--version", "--"} << none << none << true;
    // Measured: this prints the version. The end marker is taken as the value
    // of --source, so it never ends the options and --version is one.
    QTest::newRow("the end taken as a value")
        << QList<QByteArray>{"--source", "--", "--version"} << source << source
        << true;
    QTest::newRow("a lone dash")
        << QList<QByteArray>{"-"} << none << none << false;
    QTest::newRow("an empty argument")
        << QList<QByteArray>{""} << none << none << false;
    // A value option with nothing behind it steps past the end of the line
    // rather than off it.
    QTest::newRow("a value that never came")
        << QList<QByteArray>{"--source"} << source << source << false;
}

void TestCommandLine::readsTheLine() {
    QFETCH(QList<QByteArray>, arguments);
    QFETCH(QStringList, needingDisplay);
    QFETCH(QStringList, takingValue);
    QFETCH(bool, expected);

    Line line(arguments);
    QCOMPARE(
        wantsTextOnly(line.argc(), line.argv(), needingDisplay, takingValue),
        expected);
}

// The first entry of a command line is what the program was called, and a
// program can be called anything. Reading it as an argument would answer a
// question nobody asked.
void TestCommandLine::theProgramsOwnNameIsNotAnArgument() {
    Line line({}, QByteArray("--version"));
    QVERIFY(!wantsTextOnly(line.argc(), line.argv()));
}

void TestCommandLine::saysTextWhereverTheParserPrints_data() {
    QTest::addColumn<QByteArray>("spelling");

    for (const QByteArray &spelling : spellings()) {
        QTest::newRow(spelling.constData()) << spelling;
    }
}

// The contract, held against Qt itself for a program with no options of its
// own, which is what the settings window is: wherever the parser ends the run
// with a printed line, the check has to say so, or that line is printed by a
// process that first had to build a GUI application object and may have had no
// display to build it in.
void TestCommandLine::saysTextWhereverTheParserPrints() {
    QFETCH(QByteArray, spelling);

    QCommandLineParser parser;
    prepareParser(parser, QStringLiteral("a description"));

    // parse() rather than process(), which prints the answer and ends the
    // process, taking the test run with it. What it leaves behind is the same
    // three things process() acts on: a line it could not read, and the two
    // options it answers.
    const QStringList line{QLatin1String(kProgram), QLatin1String(spelling)};
    const bool refused = !parser.parse(line);
    const bool answered = parser.isSet(QStringLiteral("version")) ||
                          parser.isSet(QStringLiteral("help"));
    if (!refused && !answered) {
        // Read as a positional argument, so the run carries on to whatever the
        // program does with none. Nothing to hold the check to here.
        return;
    }

    Line raw({spelling});
    QVERIFY2(
        wantsTextOnly(raw.argc(), raw.argv()),
        "the parser ends this line in print and the check does not say so");
}

void TestCommandLine::identityComesFromTheBuild() {
    setApplicationIdentity();

    // Both come from the build rather than from a line typed into a source
    // file: the name is this target's own, and the version is the one number
    // the whole package is built with.
    QCOMPARE(QCoreApplication::applicationName(),
             QLatin1String(BINDPEEK_PROGRAM_NAME));
    QCOMPARE(QCoreApplication::applicationVersion(),
             QLatin1String(BINDPEEK_VERSION));
    QVERIFY(!QCoreApplication::applicationVersion().isEmpty());
}

QTEST_APPLESS_MAIN(TestCommandLine)
#include "test_command_line.moc"
