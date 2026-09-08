// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the check that decides whether an invocation is answered as text
// and needs no display.
//
// Three tests, from three sides. The table is the written contract, one row
// per case. The generated test holds that contract against Qt itself: for
// every spelling it makes up, on a line with the panel's own options and on
// one without, it asks the parser whether the run is answered and printed, and
// where it is, the check has to say so too. A written table only ever holds
// what somebody thought of, and -vh was found by hand rather than by a test.
// The third names Qt's own options, which are cut out of the line by the GUI
// application object and must therefore never be answered here.

#include "CommandLine.h"

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <vector>

using namespace bindpeek;

namespace {

// The command line as the C runtime hands it over: the program's own name
// first, which is why the check starts at the second entry.
constexpr char kProgram[] = "bindpeek";

// The letters the generated runs are built from: the two Qt answers, one it
// does not know, and one that is not a letter at all.
constexpr char kAlphabet[] = {'v', 'h', 'x', '5'};

// How long a generated run of short options gets. Two is enough for -vh, the
// spelling that started this; three covers a run with the known letter in the
// middle.
constexpr int kLongestRun = 3;

// The panel's own options, as it names them to the check and adds them to its
// parser. Written out here rather than reached for: the point of the generated
// test is to measure the check against Qt, and a copy of the panel's table
// would measure it against itself.
constexpr char kTextOption[] = "list";
constexpr char kValueOption[] = "source";

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

    QStringList asStrings() const {
        QStringList out;
        for (const QByteArray &argument : m_bytes) {
            out.append(QString::fromLatin1(argument));
        }
        return out;
    }

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
                 "--nope", "--list", "--source", "-", "--"});
    return made;
}

} // namespace

class TestCommandLine : public QObject {
    Q_OBJECT

private slots:
    void readsTheLine_data();
    void readsTheLine();
    void theProgramsOwnNameIsNotAnArgument();
    void saysTextWhereverTheParserAnswers_data();
    void saysTextWhereverTheParserAnswers();
    void leavesQtsOwnOptionsToQt_data();
    void leavesQtsOwnOptionsToQt();
    void identityComesFromTheBuild();
};

// The table from the header, as it is run.
void TestCommandLine::readsTheLine_data() {
    QTest::addColumn<QList<QByteArray>>("arguments");
    QTest::addColumn<QStringList>("alsoText");
    QTest::addColumn<QStringList>("takingValue");
    QTest::addColumn<bool>("expected");

    const QStringList none;
    const QStringList list{QLatin1String(kTextOption)};
    const QStringList source{QLatin1String(kValueOption)};

    QTest::newRow("nothing to show")
        << QList<QByteArray>{} << none << none << false;
    QTest::newRow("--version")
        << QList<QByteArray>{"--version"} << none << none << true;
    QTest::newRow("--help")
        << QList<QByteArray>{"--help"} << none << none << true;
    QTest::newRow("--help-all")
        << QList<QByteArray>{"--help-all"} << none << none << true;
    QTest::newRow("-v") << QList<QByteArray>{"-v"} << none << none << true;
    QTest::newRow("-h") << QList<QByteArray>{"-h"} << none << none << true;
    // Qt reads a run of short options as the options in it.
    QTest::newRow("a run of those letters")
        << QList<QByteArray>{"-vh"} << none << none << true;
    QTest::newRow("the same run the other way round")
        << QList<QByteArray>{"-hv"} << none << none << true;
    // A run holding anything else is left to the GUI application object: it
    // may be one of Qt's own, which never reach the parser at all.
    QTest::newRow("a run of others")
        << QList<QByteArray>{"-xyz"} << none << none << false;
    QTest::newRow("a digit in the run")
        << QList<QByteArray>{"-5v"} << none << none << false;
    // Qt takes this one and the argument behind it out of the line before any
    // parser sees it, so what stood there is gone and the run is not text.
    // Spelled from the one constant rather than written out again, for the
    // reason the list below is read rather than copied.
    const QByteArray taking = QByteArray("--") + kOptionQtTakesWithValue;
    const QByteArray takingShort = QByteArray("-") + kOptionQtTakesWithValue;
    QTest::newRow("Qt takes both")
        << QList<QByteArray>{taking, "--version"} << none << none << false;
    QTest::newRow("the same in one dash")
        << QList<QByteArray>{takingShort, "--version"} << none << none << false;
    QTest::newRow("the caller's own option is taken as well")
        << QList<QByteArray>{taking, "--list"} << list << none << false;
    // A value joined to it, or standing between as its own argument, leaves
    // the option behind it where it is, and those lines are answered.
    QTest::newRow("joined, so the option behind it stands")
        << QList<QByteArray>{taking + "=port:1", "--version"} << none << none
        << true;
    QTest::newRow("the value stands between")
        << QList<QByteArray>{taking, "port:1", "--version"} << none << none
        << true;
    QTest::newRow("named by the caller")
        << QList<QByteArray>{"--list"} << list << none << true;
    QTest::newRow("not named")
        << QList<QByteArray>{"--list"} << none << none << false;
    // Unknown, and an unknown one may be Qt's, so it is not answered here.
    QTest::newRow("the wrong case")
        << QList<QByteArray>{"--Version"} << none << none << false;
    QTest::newRow("a plain mistype")
        << QList<QByteArray>{"--nope"} << none << none << false;
    // One dash makes a run of short options, three make neither, so neither
    // is read as the long option it resembles.
    QTest::newRow("one dash, so a run")
        << QList<QByteArray>{"-source"} << none << source << false;
    QTest::newRow("three dashes, so neither")
        << QList<QByteArray>{"---source"} << none << source << false;
    QTest::newRow("stands on its own")
        << QList<QByteArray>{"--source", "/x", "--version"} << none << source
        << true;
    // The one the value list exists for: a file named like an option is a
    // file, and reading it as a request for the version would build a plain
    // application object for a run that goes on to put a window on the screen.
    QTest::newRow("the value of --source")
        << QList<QByteArray>{"--source", "--version"} << none << source
        << false;
    QTest::newRow("joined, so a value")
        << QList<QByteArray>{"--source=--version"} << none << source << false;
    // The step over a value is one argument, not everything after it.
    QTest::newRow("only the one value is stepped over")
        << QList<QByteArray>{"--source", "/x", "-v"} << none << source << true;
    // Measured: Qt answers this with "Nach '--source' fehlt der Wert", which
    // the GUI application object is built for like any other refusal.
    QTest::newRow("the value never came")
        << QList<QByteArray>{"--source"} << none << source << false;
    // Everything behind the end of the options is a value to Qt, so an option
    // standing there is not answered and must not be read as one here.
    QTest::newRow("behind the end of the options")
        << QList<QByteArray>{"--", "--version"} << none << none << false;
    QTest::newRow("the end comes after it")
        << QList<QByteArray>{"--version", "--"} << none << none << true;
    // Measured: this prints the version. The end marker is taken as the value
    // of --source, so it never ends the options and --version is one.
    QTest::newRow("the end taken as a value")
        << QList<QByteArray>{"--source", "--", "--version"} << none << source
        << true;
    QTest::newRow("a lone dash")
        << QList<QByteArray>{"-"} << none << none << false;
    QTest::newRow("an empty argument")
        << QList<QByteArray>{""} << none << none << false;
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

void TestCommandLine::saysTextWhereverTheParserAnswers_data() {
    QTest::addColumn<QList<QByteArray>>("arguments");
    QTest::addColumn<bool>("withOptions");

    const QByteArray value = QByteArray("--") + kValueOption;
    for (const QByteArray &spelling : spellings()) {
        // Three shapes for each: on its own, behind a value option, and behind
        // one that already has its value. The middle shape is where a spelling
        // is a value rather than an option, and the check has to tell them
        // apart the way the parser does.
        const QList<QList<QByteArray>> shapes{
            {spelling}, {value, spelling}, {value, "/x", spelling}};
        for (const QList<QByteArray> &shape : shapes) {
            for (const bool withOptions : {false, true}) {
                const QByteArray name =
                    shape.join(' ') + (withOptions ? " (panel)" : " (bare)");
                QTest::newRow(name.constData()) << shape << withOptions;
            }
        }
    }
}

// The contract, held against Qt: wherever the parser answers the run and
// prints, the check has to say so, or that line is printed by a process which
// first had to build a GUI application object and may have had no display to
// build it in.
//
// Only where it answers. A line the parser refuses is left to the GUI object
// on purpose, because an option this program does not know may still be one of
// Qt's own.
void TestCommandLine::saysTextWhereverTheParserAnswers() {
    QFETCH(QList<QByteArray>, arguments);
    QFETCH(bool, withOptions);

    QStringList alsoText;
    QStringList takingValue;
    QCommandLineParser parser;
    prepareParser(parser, QStringLiteral("a description"));
    if (withOptions) {
        alsoText.append(QLatin1String(kTextOption));
        takingValue.append(QLatin1String(kValueOption));
        parser.addOption(QCommandLineOption(QLatin1String(kTextOption),
                                            QStringLiteral("prints")));
        QCommandLineOption source(QLatin1String(kValueOption),
                                  QStringLiteral("reads"));
        source.setValueName(QStringLiteral("path"));
        parser.addOption(source);
    }

    Line line(arguments);
    // parse() rather than process(), which prints the answer and ends the
    // process, taking the test run with it.
    //
    // Read in the order process() acts: a line it could not read is refused
    // before anything is answered, and parse() sets the options it did
    // recognise even in a line it refused. -5xv is such a line, and Qt prints
    // the refusal rather than the version.
    const bool read = parser.parse(line.asStrings());
    // help-all is asked for separately: addHelpOption() registers it as an
    // option of its own, and isSet("help") is false for it. Left out, the six
    // lines carrying it never reached the check below, which is exactly the
    // kind of gap this test is here to close.
    const bool answered =
        read && (parser.isSet(QStringLiteral("version")) ||
                 parser.isSet(QStringLiteral("help")) ||
                 parser.isSet(QStringLiteral("help-all")) ||
                 (withOptions && parser.isSet(QLatin1String(kTextOption))));
    if (!answered) {
        return;
    }

    QVERIFY2(wantsTextOnly(line.argc(), line.argv(), alsoText, takingValue),
             "the parser answers this line and the check does not say so");
}

void TestCommandLine::leavesQtsOwnOptionsToQt_data() {
    QTest::addColumn<QByteArray>("option");

    // Read from the one list rather than written out again here. A copy is
    // how testability came to be missing from one of two places that named
    // the same thing.
    for (const char *option : kOptionsQtTakes) {
        QTest::newRow(option) << QByteArray(option);
    }
}

void TestCommandLine::leavesQtsOwnOptionsToQt() {
    QFETCH(QByteArray, option);

    for (const QByteArray &dashes : {QByteArray("-"), QByteArray("--")}) {
        Line line({dashes + option, "somevalue"});
        QVERIFY2(!wantsTextOnly(line.argc(), line.argv()),
                 (dashes + option +
                  ": one of Qt's own, and answering it here takes it away from "
                  "Qt")
                     .constData());
    }
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
