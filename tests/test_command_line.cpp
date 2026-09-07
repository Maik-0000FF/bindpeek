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
    explicit Line(const QList<QByteArray> &arguments) {
        m_bytes.append(QByteArray(kProgram));
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
    void knowsEverySpellingTheParserAnswers_data();
    void knowsEverySpellingTheParserAnswers();
    void identityComesFromTheBuild();
};

// The table from the header, as it is run.
void TestCommandLine::readsTheLine_data() {
    QTest::addColumn<QList<QByteArray>>("arguments");
    QTest::addColumn<QStringList>("alsoText");
    QTest::addColumn<bool>("expected");

    const QStringList none;
    const QStringList list{QStringLiteral("list")};

    QTest::newRow("nothing to show") << QList<QByteArray>{} << none << false;
    QTest::newRow("--version")
        << QList<QByteArray>{"--version"} << none << true;
    QTest::newRow("-v") << QList<QByteArray>{"-v"} << none << true;
    QTest::newRow("--help") << QList<QByteArray>{"--help"} << none << true;
    QTest::newRow("-h") << QList<QByteArray>{"-h"} << none << true;
    QTest::newRow("--help-all")
        << QList<QByteArray>{"--help-all"} << none << true;
    QTest::newRow("not named") << QList<QByteArray>{"--list"} << none << false;
    QTest::newRow("named by the caller")
        << QList<QByteArray>{"--list"} << list << true;
    QTest::newRow("seen anywhere in the line")
        << QList<QByteArray>{"--source", "/x", "--version"} << none << true;
    // A value that reads like an option is a value: neither informational
    // option takes one, so nothing here can be a value of theirs.
    QTest::newRow("a value, not an option")
        << QList<QByteArray>{"--source=--version"} << none << false;
    QTest::newRow("one dash short")
        << QList<QByteArray>{"-version"} << none << false;
    QTest::newRow("options are lower case")
        << QList<QByteArray>{"--Version"} << none << false;
    QTest::newRow("an empty argument")
        << QList<QByteArray>{""} << none << false;
    // The program's own name is not an argument, whatever it is called.
    QTest::newRow("the program's own name")
        << QList<QByteArray>{} << none << false;
}

void TestCommandLine::readsTheLine() {
    QFETCH(QList<QByteArray>, arguments);
    QFETCH(QStringList, alsoText);
    QFETCH(bool, expected);

    Line line(arguments);
    QCOMPARE(wantsTextOnly(line.argc(), line.argv(), alsoText), expected);
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
