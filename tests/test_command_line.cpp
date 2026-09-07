// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the check that decides whether an invocation is answered as text.
//
// It runs before the application object exists and therefore cannot ask the
// parser what it would recognise, so the two are compared here instead: a
// spelling the parser answers but the check does not know is one that reaches
// a GUI application object which may have no display to be built in, and the
// program aborts where it should have printed a line.

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

} // namespace

class TestCommandLine : public QObject {
    Q_OBJECT

private slots:
    void readsTheLine_data();
    void readsTheLine();
    void theProgramsOwnNameIsNotAnArgument();
    void knowsEverySpellingTheParserAnswers_data();
    void knowsEverySpellingTheParserAnswers();
    void identityComesFromTheBuild();
};

// The table from the header, as it is run.
void TestCommandLine::readsTheLine_data() {
    QTest::addColumn<QList<QByteArray>>("arguments");
    QTest::addColumn<QStringList>("alsoText");
    QTest::addColumn<QStringList>("takingValue");
    QTest::addColumn<bool>("expected");

    const QStringList none;
    const QStringList list{QStringLiteral("list")};
    const QStringList source{QStringLiteral("source")};
    const QStringList environment{QStringLiteral("environment")};

    QTest::newRow("nothing to show")
        << QList<QByteArray>{} << none << none << false;
    QTest::newRow("--version")
        << QList<QByteArray>{"--version"} << none << none << true;
    QTest::newRow("-v") << QList<QByteArray>{"-v"} << none << none << true;
    QTest::newRow("--help")
        << QList<QByteArray>{"--help"} << none << none << true;
    QTest::newRow("-h") << QList<QByteArray>{"-h"} << none << none << true;
    QTest::newRow("--help-all")
        << QList<QByteArray>{"--help-all"} << none << none << true;
    QTest::newRow("not named")
        << QList<QByteArray>{"--list"} << none << none << false;
    QTest::newRow("named by the caller")
        << QList<QByteArray>{"--list"} << list << none << true;
    QTest::newRow("stands on its own")
        << QList<QByteArray>{"--source", "/x", "--version"} << none << source
        << true;
    // The one the check exists for: a file named like an option is a file, and
    // reading it as a request for the version would build a plain application
    // object for a run that goes on to put a window on the screen.
    QTest::newRow("the value of --source")
        << QList<QByteArray>{"--source", "--version"} << none << source
        << false;
    QTest::newRow("the value of --environment")
        << QList<QByteArray>{"--environment", "-h"} << none << environment
        << false;
    QTest::newRow("joined, so a value")
        << QList<QByteArray>{"--source=--version"} << none << source << false;
    // A caller that names no such option has none, and the same line then says
    // what it plainly says.
    QTest::newRow("unnamed, so read as an option")
        << QList<QByteArray>{"--source", "--version"} << none << none << true;
    // The step over a value is one argument, not everything after it.
    QTest::newRow("only the one value is stepped over")
        << QList<QByteArray>{"--source", "/x", "-v"} << none << source << true;
    QTest::newRow("one dash short")
        << QList<QByteArray>{"-version"} << none << none << false;
    QTest::newRow("options are lower case")
        << QList<QByteArray>{"--Version"} << none << none << false;
    QTest::newRow("an empty argument")
        << QList<QByteArray>{""} << none << none << false;
    // A value option with nothing behind it steps past the end of the line
    // rather than off it.
    QTest::newRow("a value that never came")
        << QList<QByteArray>{"--source"} << none << source << false;
}

void TestCommandLine::readsTheLine() {
    QFETCH(QList<QByteArray>, arguments);
    QFETCH(QStringList, alsoText);
    QFETCH(QStringList, takingValue);
    QFETCH(bool, expected);

    Line line(arguments);
    QCOMPARE(wantsTextOnly(line.argc(), line.argv(), alsoText, takingValue),
             expected);
}

// The first entry of a command line is what the program was called, and a
// program can be called anything. Reading it as an argument would answer a
// question nobody asked.
void TestCommandLine::theProgramsOwnNameIsNotAnArgument() {
    Line line({}, QByteArray("--version"));
    QVERIFY(!wantsTextOnly(line.argc(), line.argv()));
}

// Every spelling that ends a run with a printed answer, asked of a parser
// built the way both programs build theirs.
void TestCommandLine::knowsEverySpellingTheParserAnswers_data() {
    QTest::addColumn<QByteArray>("option");

    QTest::newRow("--help") << QByteArray("--help");
    QTest::newRow("-h") << QByteArray("-h");
    QTest::newRow("--help-all") << QByteArray("--help-all");
    QTest::newRow("--version") << QByteArray("--version");
    QTest::newRow("-v") << QByteArray("-v");
}

void TestCommandLine::knowsEverySpellingTheParserAnswers() {
    QFETCH(QByteArray, option);

    QCommandLineParser parser;
    prepareParser(parser, QStringLiteral("a description"));

    // parse() rather than process(): the latter prints the answer and ends the
    // process, which would take the test run with it. What is asked is only
    // whether the option is one the parser knows, and an unknown one is the
    // one thing parse() reports as an error.
    const QStringList line{QLatin1String(kProgram), QLatin1String(option)};
    QVERIFY2(parser.parse(line), qPrintable(parser.errorText()));

    Line raw({option});
    QVERIFY2(wantsTextOnly(raw.argc(), raw.argv()),
             "the parser answers this spelling and the check does not know it");
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
