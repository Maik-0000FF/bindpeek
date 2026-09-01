// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the backends against the input tables that sit next to read().
// No window and no session of any kind needed: the files in samples/ cover
// every shape, the broken ones included, and the one backend that talks to a
// running compositor is answered by a socket the test stands up itself.

#include "Compositor.h"
#include "Source.h"
#include "SourceHyprland.h"
#ifdef BINDPEEK_WITH_KDE
#include "SourceKde.h"
#endif
#include "SourceMango.h"
#include "SourceSway.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTextStream>
#include <QThread>

#include <cstring>

using namespace bindpeek;

namespace {

QString sample(const QString &name) {
    return QStringLiteral(BINDPEEK_SAMPLES) + QLatin1Char('/') + name;
}

// Looks an entry up by its display text ("SUPER+CTRL+T").
const Bind *find(const QList<Bind> &binds, const QString &shortcut) {
    for (const Bind &bind : binds) {
        if (shortcutText(bind) == shortcut) {
            return &bind;
        }
    }
    return nullptr;
}

// The description of one entry, empty when there is no such entry.
//
// Reached through here rather than by dereferencing find() at the call site: a
// changed spelling then fails as a comparison, naming both sides, instead of
// taking the whole binary down on a null pointer and reporting nothing but the
// signal.
QString descriptionOf(const QList<Bind> &binds, const QString &shortcut) {
    const Bind *bind = find(binds, shortcut);
    return (bind == nullptr) ? QString() : bind->description;
}

// The same for the heading an entry sits under.
QString groupOf(const QList<Bind> &binds, const QString &shortcut) {
    const Bind *bind = find(binds, shortcut);
    return (bind == nullptr) ? QString() : bind->group;
}

int count(const QList<Bind> &binds, const QString &shortcut) {
    int hits = 0;
    for (const Bind &bind : binds) {
        if (shortcutText(bind) == shortcut) {
            ++hits;
        }
    }
    return hits;
}

// Writes one file into a directory and hands back its path.
QString writeFile(const QDir &directory, const QString &name,
                  const QString &content) {
    // Not const: it is returned below, and const would force a copy.
    QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream(&file) << content;
    return path;
}

// Bounds the waits around the fake compositor below. Generous on purpose: it
// only keeps a hang from lasting forever, nothing normally comes near it.
constexpr int kServerWaitMs = 5000;

// Stands in for HYPRLAND_INSTANCE_SIGNATURE. Short on purpose: the socket path
// is built below the runtime directory and a UNIX socket name is limited to
// 108 bytes, temporary directory included.
constexpr char kSignature[] = "bindpeek_test";

// Puts the variables the socket backends are found through back the way they
// were: the two the Hyprland backend reads, and the one sway is asked at.
// Without this a test pointing them at a temporary directory would leave the
// next one looking at a directory that has since been removed.
class Environment {
public:
    Environment()
        : m_runtimeDir(qgetenv(kRuntimeDirVar)),
          m_signature(qgetenv(kHyprlandSignatureVar)),
          m_swaySocket(qgetenv(kSwaySocketVar)) {}
    ~Environment() {
        restore(kRuntimeDirVar, m_runtimeDir);
        restore(kHyprlandSignatureVar, m_signature);
        restore(kSwaySocketVar, m_swaySocket);
    }
    Environment(const Environment &) = delete;
    Environment &operator=(const Environment &) = delete;

private:
    static void restore(const char *name, const QByteArray &value) {
        if (value.isEmpty()) {
            qunsetenv(name);
        } else {
            qputenv(name, value);
        }
    }

    QByteArray m_runtimeDir;
    QByteArray m_signature;
    QByteArray m_swaySocket;
};

// Answers one request the way Hyprland does: read the command, write the
// reply, then close, because closing is the only end of message the protocol
// has. It runs on a thread of its own because read() blocks while it waits, so
// nothing on the calling thread could accept the connection.
class FakeCompositor : public QThread {
public:
    // closeWhenDone false stands for a compositor that writes and then keeps
    // the connection, which is the only way a reply arrives half finished:
    // the protocol ends a message by closing.
    FakeCompositor(QString path, QByteArray reply, bool closeWhenDone = true)
        : m_path(std::move(path)), m_reply(std::move(reply)),
          m_closeWhenDone(closeWhenDone) {}

    // A failing QVERIFY leaves its test function at once. Without this the
    // thread could still be running then, and destroying a running QThread
    // takes the whole test binary down instead of reporting one failure.
    ~FakeCompositor() override { wait(); }

    // Returns once the socket is there, so the caller may connect right after.
    bool startAndWait() {
        start();
        m_ready.acquire();
        return m_listening;
    }

    // What the client sent. Only to be read once the thread has finished.
    QByteArray request() const { return m_request; }

protected:
    void run() override {
        QLocalServer server;
        m_listening = server.listen(m_path);
        m_ready.release();
        if (!m_listening) {
            return;
        }
        if (!server.waitForNewConnection(kServerWaitMs)) {
            return;
        }
        QLocalSocket *client = server.nextPendingConnection();
        if (client == nullptr) {
            return;
        }
        if (client->waitForReadyRead(kServerWaitMs)) {
            m_request = client->readAll();
        }
        client->write(m_reply);
        client->waitForBytesWritten(kServerWaitMs);
        if (m_closeWhenDone) {
            client->disconnectFromServer();
            return;
        }
        // Held open until the reader gives up and goes away, which is what
        // ends this thread.
        client->waitForDisconnected(kServerWaitMs);
    }

private:
    QString m_path;
    QByteArray m_reply;
    bool m_closeWhenDone = true;
    QSemaphore m_ready;
    bool m_listening = false;
    QByteArray m_request;
};

} // namespace

class TestSources : public QObject {
    Q_OBJECT

private slots:
    // --- normalization ----------------------------------------------------

    void normalizeModifier_data();
    void normalizeModifier();
    void normalizeKey_data();
    void normalizeKey();
    void modifiersGetOrdered();
    void matchesEveryCombinationTheHeldKeysCanReach_data();
    void matchesEveryCombinationTheHeldKeysCanReach();
    void aMissedMatchLeavesNothingBehind();
    void heldModifiersKeepThePressOrder();
    void heldModifiersRebuildFromTheDevices();
    void groupsFollowTheirFirstAppearance();
    void groupsSurviveAnInterruptedSource();
    void groupingAnEmptyListGivesNothing();
    void groupingByPositionNamesEveryPlace();

    // --- mango ------------------------------------------------------------

    void mangoCountsOnlyRealBinds();
    void mangoKnowsGroups();
    void mangoDerivesDescriptions();
    void mangoSkipsBrokenLines();
    void mangoFollowsWhatTheConfigurationPullsIn();
    void mangoReadsEachFileOnce();
    void mangoSeesThroughALinkToTheSameFile();
    void mangoFallsBackToTheFileItsPackageShips_data();
    void mangoFallsBackToTheFileItsPackageShips();
    void mangoNamesTheTwoFilesInOrder();
    void mangoAsksTheRuleAboutTheRightTwoFiles();
    void mangoLooksInTheHomeDirectoryFirst();
    void mangoNamesAFileItCannotRead();
    void mangoStartsEachFileUnderItsOwnHeading();
    void mangoReportsMissingFile();

    // --- KDE --------------------------------------------------------------
    //
    // Declared only where the backend is built. A test that cannot be run is
    // better left unnamed than skipped: a skipped one reads as a doubt about
    // the code, and there is none here, the backend is simply not in this
    // build.
#ifdef BINDPEEK_WITH_KDE
    void kdeFiltersUnassigned();
    void kdeSplitsMultipleShortcuts();
    void kdeUsesFriendlyGroupNames();
    void kdeKeepsCommaInsideDescription();
    void kdeFallsBackToTheKey();
    void kdeNormalizesMeta();
    void kdeHandlesThePlusKey();
    void kdeReportsMissingFile();
#endif

    // --- Hyprland ---------------------------------------------------------

    void hyprlandCountsOnlyKeyboardBinds();
    void hyprlandDecodesModmask();
    void hyprlandPrefersTheWrittenDescription();
    void hyprlandDerivesDescriptions();
    void hyprlandGroupsBySubmap();
    void hyprlandNamesKeycodeAndCatchall();
    void hyprlandSkipsWhatItCannotShow();
    void hyprlandReportsMissingFile();
    void hyprlandReportsUnusableReplies_data();
    void hyprlandReportsUnusableReplies();
    void hyprlandReportsNoInstance();
    void hyprlandReportsASocketThatIsNotThere();
    void hyprlandAsksTheRunningCompositor();

    void hyprlandKeepsThePromiseOfADescription();
    void hyprlandNamesWhatALuaConfigurationLeavesOut();
    void hyprlandRefusesAnUnreadableModmask_data();
    void hyprlandRefusesAnUnreadableModmask();
    void hyprlandNamesTheCountsWhenNothingRemains();

    // --- sway -------------------------------------------------------------

    void swayReadsTheSample();
    void swayResolvesAVariableBuiltFromAnother();
    void swaySkipsWhatItCannotName();
    void swaySaysWhenAnIncludeIsNotFollowed();
    void swaySaysWhenThereIsNothingToShow();
    void swayHeadsBindsWithTheirMode();
    void swayAlwaysNamesSomething_data();
    void swayAlwaysNamesSomething();
    void swayReadsABindEndingInABraceAsABlock();
    void swayReadsEveryBindingWordEndingInABraceAsABlock();
    void swayEndsABlockOnTheLastWord();
    void swayOpensNoBlockOnAVariableHoldingABrace();
    void swayNamesAModeThroughAVariable();
    void swayTakesABraceFromTheNextLine();
    void swayOpensNoBlockOnAWordEndingInABrace();
    void swayReadsEveryBindingWordAsOne();
    void swayDropsAGroupWhereverItStands();
    void swayKeepsAModeAcrossAnInnerBlock();
    void swayRefusesAnAnswerTooLargeToBeOne();
    void swayAsksTheRunningCompositor();
    void hyprlandFindsTheSocketWithoutARuntimeDir();
    void hyprlandBlamesTheClockNotTheAnswer();
    void hyprlandReportsAConnectionClosedWithoutAnAnswer();
};

// ---------------------------------------------------------------------------
// normalization
// ---------------------------------------------------------------------------

void TestSources::normalizeModifier_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("SUPER") << "SUPER" << "SUPER";
    QTest::newRow("super") << "super" << "SUPER";
    QTest::newRow("Meta") << "Meta" << "SUPER";
    QTest::newRow("Mod4") << "Mod4" << "SUPER";
    QTest::newRow("Ctrl") << "Ctrl" << "CTRL";
    QTest::newRow("Control") << "Control" << "CTRL";
    QTest::newRow("Alt") << "Alt" << "ALT";
    QTest::newRow("Shift") << "Shift" << "SHIFT";
    QTest::newRow("surrounding blanks") << "  Meta  " << "SUPER";
    QTest::newRow("a key") << "t" << "";
    QTest::newRow("media key") << "Volume Down" << "";
    QTest::newRow("empty") << "" << "";
}

void TestSources::normalizeModifier() {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(bindpeek::normalizeModifier(input), expected);
}

void TestSources::normalizeKey_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("letter") << "t" << "T";
    QTest::newRow("digit") << "1" << "1";
    QTest::newRow("an arrow key, however it is spelled") << "Left" << "←";
    QTest::newRow("a key that shows a character") << "slash" << "/";
    QTest::newRow("the same spelled with capitals") << "Grave" << "`";
    QTest::newRow("a key that prints nothing") << "space" << "␣";
    QTest::newRow("a key with no agreed symbol") << "escape" << "Escape";
    QTest::newRow("media key") << "Volume Down" << "Volume Down";
    QTest::newRow("function key") << "F10" << "F10";
    QTest::newRow("the separator as a key") << "+" << "Plus";
    // Spelled out and shouted. Named keys are looked up in lower case, so
    // both are the same key; only the capitalisation fallback would care, and
    // it would leave the second as it found it.
    QTest::newRow("the same key spelled out") << "plus" << "Plus";
    QTest::newRow("the same key in capitals") << "PLUS" << "Plus";
    QTest::newRow("surrounding blanks") << "  t  " << "T";
    QTest::newRow("empty") << "" << "";
}

void TestSources::normalizeKey() {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(bindpeek::normalizeKey(input), expected);
}

void TestSources::modifiersGetOrdered() {
    // However a source spells them, the display is always the same.
    const QStringList mixed = {QStringLiteral("SHIFT"), QStringLiteral("SUPER"),
                               QStringLiteral("CTRL")};
    QCOMPARE(orderModifiers(mixed),
             QStringList({QStringLiteral("SUPER"), QStringLiteral("CTRL"),
                          QStringLiteral("SHIFT")}));
    // Duplicates drop out.
    QCOMPARE(orderModifiers({QStringLiteral("ALT"), QStringLiteral("ALT")}),
             QStringList({QStringLiteral("ALT")}));
}

// The table next to bindMatchesHeld(), run.
void TestSources::matchesEveryCombinationTheHeldKeysCanReach_data() {
    QTest::addColumn<QStringList>("modifiers");
    QTest::addColumn<QStringList>("held");
    QTest::addColumn<bool>("shown");
    QTest::addColumn<QStringList>("missing");

    const QStringList super = {QStringLiteral("SUPER")};
    const QStringList superCtrl = {QStringLiteral("SUPER"),
                                   QStringLiteral("CTRL")};
    const QStringList ctrlSuper = {QStringLiteral("CTRL"),
                                   QStringLiteral("SUPER")};
    const QStringList superCtrlShift = {QStringLiteral("SUPER"),
                                        QStringLiteral("CTRL"),
                                        QStringLiteral("SHIFT")};

    QTest::newRow("the combination that fires now")
        << super << super << true << QStringList();
    QTest::newRow("one modifier further on")
        << superCtrl << super << true << QStringList({QStringLiteral("CTRL")});
    QTest::newRow("two modifiers further on")
        << superCtrlShift << super << true
        << QStringList({QStringLiteral("CTRL"), QStringLiteral("SHIFT")});
    // The hand may take any route to the same shortcut.
    QTest::newRow("held in the other order")
        << superCtrl << ctrlSuper << true << QStringList();
    // Holding more than the shortcut needs does not reach it: the extra
    // modifier goes to the compositor along with the key.
    QTest::newRow("a modifier the shortcut does not want")
        << super << superCtrl << false << QStringList();
    QTest::newRow("a different modifier entirely")
        << QStringList({QStringLiteral("CTRL")}) << super << false
        << QStringList();
    QTest::newRow("no modifier at all")
        << QStringList() << super << false << QStringList();
    QTest::newRow("nothing held")
        << super << QStringList() << false << QStringList();
}

void TestSources::matchesEveryCombinationTheHeldKeysCanReach() {
    QFETCH(QStringList, modifiers);
    QFETCH(QStringList, held);
    QFETCH(bool, shown);
    QFETCH(QStringList, missing);

    QStringList rest;
    QCOMPARE(bindMatchesHeld(modifiers, held, &rest), shown);
    if (shown) {
        QCOMPARE(rest, missing);
    }
}

// The list handed in is emptied on every call, including the ones that do not
// match.
//
// Measured with one list reused across calls, which is what a caller writing
// it does to save an allocation per row: without the clearing, the modifiers
// of the last shortcut that did match survive into the next answer and are
// printed in front of a key that does not want them. The table test above
// cannot see it, it looks at the list only when there was a match.
void TestSources::aMissedMatchLeavesNothingBehind() {
    const QStringList superOnly = {QStringLiteral("SUPER")};
    QStringList missing;

    QVERIFY(bindMatchesHeld({QStringLiteral("SUPER"), QStringLiteral("CTRL")},
                            superOnly, &missing));
    QCOMPARE(missing, QStringList({QStringLiteral("CTRL")}));

    // Not reachable from here, so there is nothing it still wants.
    QVERIFY(!bindMatchesHeld({QStringLiteral("CTRL")}, superOnly, &missing));
    QVERIFY(missing.isEmpty());

    // Nor when nothing is held at all.
    QVERIFY(bindMatchesHeld({QStringLiteral("SUPER"), QStringLiteral("ALT")},
                            superOnly, &missing));
    QVERIFY(
        !bindMatchesHeld({QStringLiteral("SUPER")}, QStringList(), &missing));
    QVERIFY(missing.isEmpty());
}

// The heading reads the way the hand moved, so the order is the press order
// and not the display order the filter uses.
void TestSources::heldModifiersKeepThePressOrder() {
    HeldModifiers held;
    QVERIFY(held.isEmpty());

    QVERIFY(held.press(QString::fromLatin1(modifier::kCtrl)));
    QVERIFY(held.press(QString::fromLatin1(modifier::kSuper)));
    QCOMPARE(held.names(),
             QStringList({QStringLiteral("CTRL"), QStringLiteral("SUPER")}));

    // The same modifier again, from the other side of the keyboard or from a
    // second one. Moving it to the end would rewrite a heading being read.
    QVERIFY(!held.press(QString::fromLatin1(modifier::kCtrl)));
    QCOMPARE(held.names(),
             QStringList({QStringLiteral("CTRL"), QStringLiteral("SUPER")}));

    QVERIFY(held.release(QString::fromLatin1(modifier::kCtrl)));
    QCOMPARE(held.names(), QStringList({QStringLiteral("SUPER")}));
    // Releasing what is not down changes nothing and is not worth announcing.
    QVERIFY(!held.release(QString::fromLatin1(modifier::kCtrl)));
}

void TestSources::heldModifiersRebuildFromTheDevices() {
    HeldModifiers held;
    held.press(QString::fromLatin1(modifier::kShift));
    held.press(QString::fromLatin1(modifier::kSuper));

    // The devices agree with what was seen: nothing to correct, nothing to
    // announce.
    QVERIFY(!held.reconcile(
        QSet<QString>{QStringLiteral("SHIFT"), QStringLiteral("SUPER")}));

    // A release that never arrived. What is left keeps its place.
    QVERIFY(held.reconcile(QSet<QString>{QStringLiteral("SUPER")}));
    QCOMPARE(held.names(), QStringList({QStringLiteral("SUPER")}));

    // A press that never arrived: there is no place to restore for it, so it
    // goes last, after everything whose order is known.
    QVERIFY(held.reconcile(
        QSet<QString>{QStringLiteral("SUPER"), QStringLiteral("CTRL")}));
    QCOMPARE(held.names(),
             QStringList({QStringLiteral("SUPER"), QStringLiteral("CTRL")}));

    QVERIFY(held.reconcile(QSet<QString>()));
    QVERIFY(held.isEmpty());
}

void TestSources::groupsFollowTheirFirstAppearance() {
    const QList<Bind> binds = {
        Bind{{},
             QStringLiteral("A"),
             QStringLiteral("a"),
             QStringLiteral("Second")},
        Bind{{},
             QStringLiteral("B"),
             QStringLiteral("b"),
             QStringLiteral("First")},
    };
    const QList<BindGroup> groups = groupBinds(binds);

    // The order the source wrote them in, never the alphabet: the sections of
    // a bind.conf mean something in the order they stand.
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).name, QStringLiteral("Second"));
    QCOMPARE(groups.at(1).name, QStringLiteral("First"));
}

void TestSources::groupsSurviveAnInterruptedSource() {
    // What a Hyprland reply looks like: a submap sits wherever the
    // configuration puts it, so the entries of one heading are interrupted by
    // another. Comparing each entry with the one before it would print
    // "Other" twice, which is exactly what --list used to do.
    const QList<Bind> binds = {
        Bind{{},
             QStringLiteral("A"),
             QStringLiteral("a"),
             QStringLiteral("Other")},
        Bind{{},
             QStringLiteral("B"),
             QStringLiteral("b"),
             QStringLiteral("resize")},
        Bind{{},
             QStringLiteral("C"),
             QStringLiteral("c"),
             QStringLiteral("Other")},
    };
    const QList<BindGroup> groups = groupBinds(binds);

    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).name, QStringLiteral("Other"));
    QCOMPARE(groups.at(0).binds.size(), 2);
    // The entries keep their own order inside the heading.
    QCOMPARE(groups.at(0).binds.at(0).key, QStringLiteral("A"));
    QCOMPARE(groups.at(0).binds.at(1).key, QStringLiteral("C"));
    QCOMPARE(groups.at(1).name, QStringLiteral("resize"));
    QCOMPARE(groups.at(1).binds.size(), 1);
}

// What the panel is handed: each heading in the order it first appears, and
// under it the places its binds sit in the list, in the order they sit there.
//
// Measured against the answer written out rather than against groupBinds(),
// which is this function with the binds put back and therefore agrees with it
// whatever either of them does.
void TestSources::groupingByPositionNamesEveryPlace() {
    const QList<Bind> binds = {
        Bind{{},
             QStringLiteral("A"),
             QStringLiteral("first"),
             QStringLiteral("Windows")},
        Bind{{},
             QStringLiteral("B"),
             QStringLiteral("second"),
             QStringLiteral("Programs")},
        // The interrupted heading, which is the case the grouping exists for.
        Bind{{},
             QStringLiteral("C"),
             QStringLiteral("third"),
             QStringLiteral("Windows")},
        Bind{{}, QStringLiteral("D"), QStringLiteral("fourth"), QString()},
    };

    const QList<BindGroupPositions> groups = groupBindPositions(binds);

    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups.at(0).name, QStringLiteral("Windows"));
    QCOMPARE(groups.at(0).at, QList<qsizetype>({0, 2}));
    QCOMPARE(groups.at(1).name, QStringLiteral("Programs"));
    QCOMPARE(groups.at(1).at, QList<qsizetype>({1}));
    QCOMPARE(groups.at(2).name, QString());
    QCOMPARE(groups.at(2).at, QList<qsizetype>({3}));
}

void TestSources::groupingAnEmptyListGivesNothing() {
    QVERIFY(groupBinds({}).isEmpty());
}

// ---------------------------------------------------------------------------
// mango
// ---------------------------------------------------------------------------

void TestSources::mangoCountsOnlyRealBinds() {
    SourceMango source(sample(QStringLiteral("mango-binds.conf")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // 13 valid bind= lines. mousebind=, axisbind=, comments and the broken
    // lines are not among them.
    QCOMPARE(binds.size(), 13);
    QVERIFY(find(binds, QStringLiteral("SUPER+T")) != nullptr);
    QVERIFY(find(binds, QStringLiteral("SUPER+CTRL+T")) != nullptr);
    // A shortcut without a modifier stays valid.
    QVERIFY(find(binds, QStringLiteral("F5")) != nullptr);
    // mousebind=SUPER,btn_left,... must not show up.
    QCOMPARE(count(binds, QStringLiteral("SUPER+Btn_left")), 0);
}

void TestSources::mangoKnowsGroups() {
    SourceMango source(sample(QStringLiteral("mango-binds.conf")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    const Bind *ghostty = find(binds, QStringLiteral("SUPER+T"));
    QVERIFY(ghostty != nullptr);
    QCOMPARE(ghostty->group, QStringLiteral("Programs"));

    const Bind *tag = find(binds, QStringLiteral("SUPER+1"));
    QVERIFY(tag != nullptr);
    QCOMPARE(tag->group, QStringLiteral("Tags"));
}

void TestSources::mangoDerivesDescriptions() {
    SourceMango source(sample(QStringLiteral("mango-binds.conf")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // spawn: PARAMS are the whole command line, blanks included.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+E")),
             QStringLiteral("ghostty -e yazi"));
    // Directions are translated.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+←")),
             QStringLiteral("Focus left"));
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+J")),
             QStringLiteral("Focus down"));
    // Empty PARAMS: the text comes from the ACTION alone.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+V")),
             QStringLiteral("Toggle floating"));
    // Three fields without a trailing comma are valid.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+CTRL+D")),
             QStringLiteral("Toggle split direction"));
    // PARAMS "1,0": only the first field is the tag number.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+1")),
             QStringLiteral("Tag 1"));
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+SHIFT+9")),
             QStringLiteral("Window to tag 9"));
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+ALT+H")),
             QStringLiteral("Layout scroller"));
}

void TestSources::mangoSkipsBrokenLines() {
    SourceMango source(sample(QStringLiteral("mango-binds.conf")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // Broken lines do not vanish silently, they get reported.
    QVERIFY(!binds.isEmpty());
    QVERIFY(error.contains(QStringLiteral("4")));

    // The note that carries "bind=" in its own text produces nothing.
    QCOMPARE(count(binds, QStringLiteral("SUPER+X")), 0);
}

// The table next to sourceFiles(), run.
//
// A configuration that names other files is the ordinary case rather than the
// exotic one: mango's own default splits nothing, but a configuration of any
// size does, and the file it splits into carries whatever name its author
// chose. Assuming that name is what made the panel show nothing at all for
// anyone who had chosen differently.
void TestSources::mangoFollowsWhatTheConfigurationPullsIn() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir home(dir.path());

    // Named with a tilde, absolutely, and relatively: all three spellings
    // occur in configurations, and mango reads all three.
    const QString keys =
        writeFile(home, QStringLiteral("keys.conf"),
                  QStringLiteral("bind=SUPER,t,spawn,ghostty\n"));
    QVERIFY(!keys.isEmpty());
    const QString more =
        writeFile(home, QStringLiteral("more.conf"),
                  QStringLiteral("bind=SUPER,b,spawn,firefox\n"));
    QVERIFY(!more.isEmpty());
    const QString config = writeFile(home, QStringLiteral("config.conf"),
                                     QStringLiteral("bind=SUPER,c,killclient,\n"
                                                    "source=keys.conf\n"
                                                    "source=%1\n"
                                                    "source=\n")
                                         .arg(more));
    QVERIFY(!config.isEmpty());

    // The file itself first, then what it named, in the order it named them.
    QCOMPARE(SourceMango::sourceFiles(config),
             QStringList({config, keys, more}));

    // And every bind out of all three, whichever file it stood in.
    SourceMango source(config);
    QString error;
    const QList<Bind> binds = source.read(&error);
    QCOMPARE(binds.size(), 3);
    QVERIFY(find(binds, QStringLiteral("SUPER+T")) != nullptr);
    QVERIFY(find(binds, QStringLiteral("SUPER+B")) != nullptr);
    QVERIFY(find(binds, QStringLiteral("SUPER+C")) != nullptr);
}

// A configuration that names itself, or two that name each other, is read once
// and not forever.
void TestSources::mangoReadsEachFileOnce() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir home(dir.path());

    const QString second =
        writeFile(home, QStringLiteral("second.conf"),
                  QStringLiteral("bind=SUPER,b,spawn,firefox\n"
                                 "source=first.conf\n"));
    QVERIFY(!second.isEmpty());
    const QString first =
        writeFile(home, QStringLiteral("first.conf"),
                  QStringLiteral("bind=SUPER,t,spawn,ghostty\n"
                                 "source=first.conf\n"
                                 "source=second.conf\n"));
    QVERIFY(!first.isEmpty());

    QCOMPARE(SourceMango::sourceFiles(first), QStringList({first, second}));

    SourceMango source(first);
    QString error;
    const QList<Bind> binds = source.read(&error);
    // Two binds, not four: neither file is read a second time.
    QCOMPARE(binds.size(), 2);
}

// The same file under two names is still one file. A link is how a
// configuration is shared between two setups, and counting it twice lists
// every shortcut in it twice, which reads as a broken configuration.
void TestSources::mangoSeesThroughALinkToTheSameFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir home(dir.path());

    const QString keys =
        writeFile(home, QStringLiteral("keys.conf"),
                  QStringLiteral("bind=SUPER,t,spawn,ghostty\n"));
    QVERIFY(!keys.isEmpty());
    const QString link = home.filePath(QStringLiteral("alias.conf"));
    QVERIFY(QFile::link(keys, link));

    // Three ways to the one file: its own name, a link to it, and the same
    // name written as a path.
    const QString config = writeFile(home, QStringLiteral("config.conf"),
                                     QStringLiteral("source=keys.conf\n"
                                                    "source=alias.conf\n"
                                                    "source=./keys.conf\n"));
    QVERIFY(!config.isEmpty());

    SourceMango source(config);
    QString error;
    const QList<Bind> binds = source.read(&error);
    QCOMPARE(binds.size(), 1);
    QVERIFY(error.isEmpty());
}

// A session that never wrote a configuration of its own runs off the one its
// package ships, and every shortcut on it works. A panel that only knew the
// file in the home directory would report that one as missing and list
// nothing at all.
void TestSources::mangoFallsBackToTheFileItsPackageShips_data() {
    QTest::addColumn<bool>("homeExists");
    QTest::addColumn<bool>("systemExists");
    QTest::addColumn<QString>("expected");

    const QString home =
        QStringLiteral("/home/somebody/.config/mango/config.conf");
    const QString system = QStringLiteral("/etc/mango/config.conf");

    QTest::newRow("its own") << true << true << home;
    QTest::newRow("its own, nothing shipped") << true << false << home;
    QTest::newRow("only what was shipped") << false << true << system;
    // Neither is there: the one somebody would write is the one named, so a
    // message about it points at the file to create.
    QTest::newRow("neither") << false << false << home;
}

void TestSources::mangoFallsBackToTheFileItsPackageShips() {
    QFETCH(bool, homeExists);
    QFETCH(bool, systemExists);
    QFETCH(QString, expected);

    const ConfigCandidates candidates{
        QStringLiteral("/home/somebody/.config/mango/config.conf"),
        QStringLiteral("/etc/mango/config.conf")};

    // The question the rule asks, answered from the table rather than by the
    // machine this runs on.
    const auto isThere = [&candidates, homeExists,
                          systemExists](const QString &path) {
        return path == candidates.ownFile ? homeExists : systemExists;
    };

    QCOMPARE(chosenConfig(candidates, isThere), expected);
}

// And the same rule asked of the pair the program actually hands it, which is
// where the two files could be swapped without anything above noticing. Asked
// with the machine's own answer replaced, so it holds wherever it runs.
void TestSources::mangoAsksTheRuleAboutTheRightTwoFiles() {
    const ConfigCandidates candidates = SourceMango::configCandidates();
    const QString packaged = QStringLiteral("/etc/mango/config.conf");

    // Only the packaged one is there. A pair handed over the wrong way round
    // answers with the file in the home directory, which is the file nobody
    // wrote.
    QCOMPARE(chosenConfig(
                 candidates,
                 [&packaged](const QString &path) { return path == packaged; }),
             packaged);

    // And with both there the session's own wins.
    QCOMPARE(chosenConfig(candidates, [](const QString &) { return true; }),
             candidates.ownFile);
}

// The rule above decides between two names; this says which name is which.
// Swapping them reads every session's configuration out of the file its
// package ships, and the rule itself would still pass.
//
// Asked of the two names alone, without touching the filesystem: whether a
// machine happens to carry a system-wide configuration is not something a
// test may depend on, and one that did would be red on exactly the machines
// this program is for.
void TestSources::mangoNamesTheTwoFilesInOrder() {
    const ConfigCandidates candidates = SourceMango::configCandidates();

    QCOMPARE(candidates.ownFile,
             QDir::homePath() + QStringLiteral("/.config/mango/config.conf"));
    QCOMPARE(candidates.packagedFile, QStringLiteral("/etc/mango/config.conf"));
}

// And the file in the home directory is the one read when it is there, which
// is the wiring those two names are put through.
void TestSources::mangoLooksInTheHomeDirectoryFirst() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // The home directory is where the compositor looks, and Qt takes it from
    // the environment, so a temporary one is enough to ask the question. Put
    // back afterwards, because the tests in this binary share it.
    const QByteArray realHome = qgetenv("HOME");
    QVERIFY(qputenv("HOME", dir.path().toLocal8Bit()));

    // And no running compositor, whatever the machine this is measured on.
    // A mango started with a file of its own answers first and rightly so,
    // which would make the answer here depend on who is logged in.
    const QByteArray realSignature = qgetenv(kMangoSignatureVar);
    qunsetenv(kMangoSignatureVar);

    const QDir home(dir.path());
    QVERIFY(home.mkpath(QStringLiteral(".config/mango")));
    const QString mine =
        home.filePath(QStringLiteral(".config/mango/config.conf"));
    QFile file(mine);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.close();

    // Read before the environment is put back and compared after: a failing
    // comparison returns on the spot, and a test that left the home directory
    // pointing at a temporary about to be deleted would take the rest of the
    // run down with it.
    const QString found = SourceMango::configPath();

    QVERIFY(qputenv("HOME", realHome));
    if (!realSignature.isEmpty()) {
        QVERIFY(qputenv(kMangoSignatureVar, realSignature));
    }

    QCOMPARE(found, mine);
}

// A named file that is not there costs its shortcuts, and the reader says so
// rather than showing a shorter list without a word.
void TestSources::mangoNamesAFileItCannotRead() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir home(dir.path());

    const QString config =
        writeFile(home, QStringLiteral("config.conf"),
                  QStringLiteral("bind=SUPER,t,spawn,ghostty\n"
                                 "source=gone.conf\n"));
    QVERIFY(!config.isEmpty());

    SourceMango source(config);
    QString error;
    const QList<Bind> binds = source.read(&error);
    QCOMPARE(binds.size(), 1);
    QVERIFY(error.contains(QStringLiteral("gone.conf")));
}

// Headings belong to the file they stand in. A file that opens with binds
// starts under the default heading rather than under whatever the file read
// before it happened to end with.
void TestSources::mangoStartsEachFileUnderItsOwnHeading() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir home(dir.path());

    const QString second =
        writeFile(home, QStringLiteral("second.conf"),
                  QStringLiteral("bind=SUPER,b,spawn,firefox\n"));
    QVERIFY(!second.isEmpty());
    const QString first =
        writeFile(home, QStringLiteral("first.conf"),
                  QStringLiteral("# --- Programs ---\n"
                                 "bind=SUPER,t,spawn,ghostty\n"
                                 "source=second.conf\n"));
    QVERIFY(!first.isEmpty());

    SourceMango source(first);
    QString error;
    const QList<Bind> binds = source.read(&error);
    QCOMPARE(groupOf(binds, QStringLiteral("SUPER+T")),
             QStringLiteral("Programs"));
    QCOMPARE(groupOf(binds, QStringLiteral("SUPER+B")), defaultGroupName());
}

void TestSources::mangoReportsMissingFile() {
    SourceMango source(sample(QStringLiteral("does-not-exist.conf")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    QVERIFY(binds.isEmpty());
    QVERIFY(!error.isEmpty());
}

// ---------------------------------------------------------------------------
// KDE
// ---------------------------------------------------------------------------

#ifdef BINDPEEK_WITH_KDE

void TestSources::kdeFiltersUnassigned() {
    SourceKde source(sample(QStringLiteral("kglobalshortcutsrc")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // 13 assigned shortcuts; every "none,none," entry is gone.
    QCOMPARE(binds.size(), 13);
    for (const Bind &bind : binds) {
        QVERIFY(bind.key.compare(QStringLiteral("none"), Qt::CaseInsensitive) !=
                0);
        QVERIFY(!bind.description.isEmpty());
    }
}

void TestSources::kdeSplitsMultipleShortcuts() {
    SourceKde source(sample(QStringLiteral("kglobalshortcutsrc")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // "Microphone Mute\tMeta+Volume Mute" are two shortcuts, both assigned.
    QCOMPARE(count(binds, QStringLiteral("Microphone Mute")), 1);
    QCOMPARE(count(binds, QStringLiteral("SUPER+Volume Mute")), 1);
    // "Screensaver\tCtrl+Alt+L" likewise.
    QCOMPARE(count(binds, QStringLiteral("Screensaver")), 1);
    QCOMPARE(count(binds, QStringLiteral("CTRL+ALT+L")), 1);
}

void TestSources::kdeUsesFriendlyGroupNames() {
    SourceKde source(sample(QStringLiteral("kglobalshortcutsrc")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // Not "[kwin]" but the translated name from _k_friendly_name.
    QCOMPARE(groupOf(binds, QStringLiteral("SUPER+CTRL+A")),
             QStringLiteral("Window Management"));
    // _k_friendly_name itself is not a shortcut.
    for (const Bind &bind : binds) {
        QVERIFY(bind.description != QStringLiteral("_k_friendly_name"));
    }
}

void TestSources::kdeKeepsCommaInsideDescription() {
    SourceKde source(sample(QStringLiteral("kglobalshortcutsrc")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // The escaped comma belongs to the description and separates no field.
    QCOMPARE(
        descriptionOf(binds, QStringLiteral("SUPER+CTRL+A")),
        QStringLiteral("Activate window demanding attention, wherever it is"));
    // Non-ASCII text survives unchanged.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+,")),
             QStringLiteral("Adjust volume by ±5 %"));
}

void TestSources::kdeFallsBackToTheKey() {
    SourceKde source(sample(QStringLiteral("kglobalshortcutsrc")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // "Lock Session=Screensaver\tCtrl+Alt+L,," carries no description.
    QCOMPARE(descriptionOf(binds, QStringLiteral("CTRL+ALT+L")),
             QStringLiteral("Lock Session"));
}

void TestSources::kdeNormalizesMeta() {
    SourceKde source(sample(QStringLiteral("kglobalshortcutsrc")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // Meta becomes SUPER everywhere, so the display matches mango.
    QVERIFY(find(binds, QStringLiteral("SUPER+ALT+L")) != nullptr);
    QCOMPARE(count(binds, QStringLiteral("Meta+Alt+L")), 0);
    // A bare "Meta" is the key itself, not a shortcut without a key.
    QVERIFY(find(binds, QStringLiteral("SUPER")) != nullptr);
    // A media key without a modifier stays a bare key.
    QVERIFY(find(binds, QStringLiteral("Volume Up")) != nullptr);
    QVERIFY(find(binds, QStringLiteral("SHIFT+Volume Down")) != nullptr);
}

void TestSources::kdeHandlesThePlusKey() {
    SourceKde source(sample(QStringLiteral("kglobalshortcutsrc")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // "Meta++" is SUPER plus the plus key, not SUPER with an empty key. It
    // is shown as the word: the key's own character is the one that joins the
    // modifiers, and "SUPER++" leaves the reader to work out which of the two
    // is which.
    const Bind *zoom = find(binds, QStringLiteral("SUPER+Plus"));
    QVERIFY(zoom != nullptr);
    QCOMPARE(zoom->key, QStringLiteral("Plus"));
    QCOMPARE(zoom->description, QStringLiteral("Zoom in"));
}

void TestSources::kdeReportsMissingFile() {
    SourceKde source(sample(QStringLiteral("does-not-exist")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    QVERIFY(binds.isEmpty());
    QVERIFY(!error.isEmpty());
}

#endif // BINDPEEK_WITH_KDE

// ---------------------------------------------------------------------------
// Hyprland
// ---------------------------------------------------------------------------

void TestSources::hyprlandCountsOnlyKeyboardBinds() {
    SourceHyprland source(sample(QStringLiteral("hyprland-binds.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // 15 of the 20 entries are keyboard shortcuts. What falls away: the
    // button, the wheel, the entry without any key, the one needing MOD5 and
    // the value that is not an object at all.
    QCOMPARE(binds.size(), 15);
    QCOMPARE(count(binds, QStringLiteral("SUPER+Mouse:272")), 0);
    QCOMPARE(count(binds, QStringLiteral("SUPER+Mouse_down")), 0);
    // The MOD5 bind would have arrived here had its modifier been dropped.
    QCOMPARE(count(binds, QStringLiteral("T")), 0);
}

void TestSources::hyprlandDecodesModmask() {
    SourceHyprland source(sample(QStringLiteral("hyprland-binds.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // 64 is Hyprland's META bit and this panel calls it SUPER.
    QVERIFY(find(binds, QStringLiteral("SUPER+T")) != nullptr);
    // 65 = META|SHIFT, and the key was written in lower case.
    QVERIFY(find(binds, QStringLiteral("SUPER+SHIFT+B")) != nullptr);
    // 76 = META|ALT|CTRL, ordered the way every backend orders them.
    QVERIFY(find(binds, QStringLiteral("SUPER+CTRL+ALT+Delete")) != nullptr);
    // 0: a media key carries no modifier and is still a shortcut.
    QVERIFY(find(binds, QStringLiteral("XF86AudioRaiseVolume")) != nullptr);
}

void TestSources::hyprlandPrefersTheWrittenDescription() {
    SourceHyprland source(sample(QStringLiteral("hyprland-binds.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // Written with bindd: the text the user gave beats anything derived.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+E")),
             QStringLiteral("Open the file manager"));
    // A description left in the field without the flag is not one. Hyprland
    // says so with has_description, so the flag decides and not the emptiness.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+Q")),
             QStringLiteral("Close window"));
}

void TestSources::hyprlandDerivesDescriptions() {
    SourceHyprland source(sample(QStringLiteral("hyprland-binds.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // exec: the argument is the command line and stands on its own.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+T")),
             QStringLiteral("ghostty"));
    // Directions are translated, and Hyprland reads only the first character,
    // so "l" and "right" are handled the same way.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+←")),
             QStringLiteral("Focus left"));
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+→")),
             QStringLiteral("Focus right"));
    // movewindow also takes "mon:", which is no direction and stays as it is.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+SHIFT+→")),
             QStringLiteral("Move window mon:DP-1"));
    // Workspace arguments are what the user wrote and are shown that way.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+⇥")),
             QStringLiteral("Workspace e+1"));
    // Without an argument the pattern would keep its placeholder.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+`")),
             QStringLiteral("workspace"));
    // No argument and no placeholder either.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+CTRL+ALT+Delete")),
             QStringLiteral("Exit Hyprland"));
    // A dispatcher from a plugin is shown raw rather than dropped.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+P")),
             QStringLiteral("myplugin:doThing x"));
}

void TestSources::hyprlandGroupsBySubmap() {
    SourceHyprland source(sample(QStringLiteral("hyprland-binds.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // A submap is a mode of its own, so its name is the heading.
    QCOMPARE(groupOf(binds, QStringLiteral("L")), QStringLiteral("resize"));
    // Everything outside a submap: Hyprland groups nothing there.
    QCOMPARE(groupOf(binds, QStringLiteral("SUPER+T")), defaultGroupName());
}

void TestSources::hyprlandNamesKeycodeAndCatchall() {
    SourceHyprland source(sample(QStringLiteral("hyprland-binds.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // "bind = SUPER, code:28, ...": Hyprland keeps no name for it, so the
    // number is shown rather than the shortcut dropped.
    const Bind *byCode = find(binds, QStringLiteral("SUPER+Code 28"));
    QVERIFY(byCode != nullptr);
    QCOMPARE(byCode->description, QStringLiteral("wofi --show drun"));

    // A catchall stands for every key not bound otherwise, which is what the
    // reader of a submap page wants to know.
    const Bind *catchAll = find(binds, QStringLiteral("any key"));
    QVERIFY(catchAll != nullptr);
    QCOMPARE(catchAll->group, QStringLiteral("resize"));
}

void TestSources::hyprlandSkipsWhatItCannotShow() {
    SourceHyprland source(sample(QStringLiteral("hyprland-binds.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    // Nothing vanishes without a word: two entries have no key at all (one of
    // them is not even an object), one needs a modifier with no name here.
    QVERIFY(!binds.isEmpty());
    QVERIFY(error.contains(QStringLiteral("2")));
    QVERIFY(error.contains(QStringLiteral("1")));
}

void TestSources::hyprlandReportsMissingFile() {
    SourceHyprland source(sample(QStringLiteral("does-not-exist.json")));
    QString error;
    const QList<Bind> binds = source.read(&error);

    QVERIFY(binds.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestSources::hyprlandReportsUnusableReplies_data() {
    QTest::addColumn<QByteArray>("payload");

    QTest::newRow("not JSON at all") << QByteArray("unknown request");
    QTest::newRow("cut short") << QByteArray("[{\"key\": \"T\"");
    QTest::newRow("an object, not a list") << QByteArray("{\"key\": \"T\"}");
    QTest::newRow("an empty list") << QByteArray("[]");
    QTest::newRow("an empty file") << QByteArray();
    QTest::newRow("nothing but keys this backend drops")
        << QByteArray("[{\"mouse\": true, \"key\": \"mouse:272\"}]");
}

void TestSources::hyprlandReportsUnusableReplies() {
    QFETCH(QByteArray, payload);

    QTemporaryFile dump;
    QVERIFY(dump.open());
    QCOMPARE(dump.write(payload), payload.size());
    dump.close();

    SourceHyprland source(dump.fileName());
    QString error;
    const QList<Bind> binds = source.read(&error);

    QVERIFY(binds.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestSources::hyprlandReportsNoInstance() {
    const Environment saved;
    qunsetenv(kHyprlandSignatureVar);

    SourceHyprland source;
    QString error;
    const QList<Bind> binds = source.read(&error);

    QVERIFY(binds.isEmpty());
    // The message has to name the variable, or there is nothing to act on.
    QVERIFY(error.contains(QLatin1String(kHyprlandSignatureVar)));
}

void TestSources::hyprlandReportsASocketThatIsNotThere() {
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());

    const Environment saved;
    qputenv(kRuntimeDirVar, runtime.path().toLocal8Bit());
    qputenv(kHyprlandSignatureVar, kSignature);

    SourceHyprland source;
    QString error;
    const QList<Bind> binds = source.read(&error);

    QVERIFY(binds.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestSources::hyprlandAsksTheRunningCompositor() {
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QString directory = runtime.path() + QLatin1Char('/') +
                              QLatin1String(kHyprlandSocketDir) +
                              QLatin1Char('/') + QLatin1String(kSignature);
    QVERIFY(QDir().mkpath(directory));

    QFile dump(sample(QStringLiteral("hyprland-binds.json")));
    QVERIFY(dump.open(QIODevice::ReadOnly));
    const QByteArray reply = dump.readAll();

    FakeCompositor compositor(directory + QLatin1Char('/') +
                                  QLatin1String(kHyprlandSocketName),
                              reply);
    QVERIFY(compositor.startAndWait());

    const Environment saved;
    qputenv(kRuntimeDirVar, runtime.path().toLocal8Bit());
    qputenv(kHyprlandSignatureVar, kSignature);

    SourceHyprland source;
    QString error;
    const QList<Bind> binds = source.read(&error);
    QVERIFY(compositor.wait(kServerWaitMs));

    // The socket path is built the way hyprctl builds it, and the request is
    // the one the compositor answers with JSON.
    QCOMPARE(compositor.request(), QByteArray("j/binds"));
    // Same list as from the saved reply: the transport changes nothing.
    QCOMPARE(binds.size(), 15);
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+T")),
             QStringLiteral("ghostty"));
}

void TestSources::hyprlandKeepsThePromiseOfADescription() {
    // Neither dispatcher nor argument, which only a plugin or a hand-edited
    // dump produces. Source.h promises a description all the same.
    const QByteArray payload =
        R"([{"modmask": 64, "key": "Y", "dispatcher": "", "arg": ""}])";
    QTemporaryFile dump;
    QVERIFY(dump.open());
    QCOMPARE(dump.write(payload), payload.size());
    dump.close();

    SourceHyprland source(dump.fileName());
    QString error;
    const QList<Bind> binds = source.read(&error);

    QCOMPARE(binds.size(), 1);
    QCOMPARE(binds.first().description, QStringLiteral("Y"));
}

// A configuration written in Lua reaches hyprctl as the handler "__lua" and a
// registry index, whatever dispatcher the user wrote. Nothing can be derived
// from that, so the entry says what it is and the message says where a name
// would come from. A description given to hl.bind is reported like any other
// and beats the placeholder.
void TestSources::hyprlandNamesWhatALuaConfigurationLeavesOut() {
    const QByteArray payload =
        R"([{"modmask": 64, "key": "T", "dispatcher": "__lua", "arg": "12"},
            {"modmask": 64, "key": "B", "dispatcher": "__lua", "arg": "13",
             "has_description": true, "description": "Browser"}])";
    QTemporaryFile dump;
    QVERIFY(dump.open());
    QCOMPARE(dump.write(payload), payload.size());
    dump.close();

    SourceHyprland source(dump.fileName());
    QString error;
    const QList<Bind> binds = source.read(&error);

    QCOMPARE(binds.size(), 2);
    // The registry index would otherwise stand there as "__lua 12".
    const Bind *unnamed = find(binds, QStringLiteral("SUPER+T"));
    // QFAIL rather than QVERIFY: its return is plain to read, for the analyser
    // as much as for anyone, and what follows dereferences this pointer.
    if (unnamed == nullptr) {
        QFAIL("SUPER+T is missing from the list");
    }
    QVERIFY(!unnamed->description.contains(QStringLiteral("12")));
    QVERIFY(!unnamed->description.contains(QStringLiteral("__lua")));
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+B")),
             QStringLiteral("Browser"));

    // One of the two is unnamed, and the message says so and how to fix it.
    QVERIFY(error.contains(QStringLiteral("1")));
    QVERIFY(error.contains(QStringLiteral("hl.bind")));
}

void TestSources::hyprlandRefusesAnUnreadableModmask_data() {
    // The whole field including its name, so the case of no field at all can
    // be told apart from a field holding something unreadable.
    QTest::addColumn<QByteArray>("field");
    QTest::addColumn<int>("kept");

    QTest::newRow("a bit this panel can name")
        << QByteArray("\"modmask\": 64,") << 1;
    QTest::newRow("no modifier at all") << QByteArray("\"modmask\": 0,") << 1;
    // Absent is not the same as unreadable: an omitted number reads as none
    // everywhere else, and a hand-written dump is where this occurs.
    QTest::newRow("no modmask field at all") << QByteArray() << 1;
    // CAPS, MOD2, MOD3 and MOD5 have no name here, and neither has anything a
    // plugin or a later release puts above them.
    QTest::newRow("MOD5") << QByteArray("\"modmask\": 128,") << 0;
    QTest::newRow("a bit nobody has seen yet")
        << QByteArray("\"modmask\": 256,") << 0;
    // Read as a number these would be 0, which would turn the bind into an
    // unmodified one and quietly put it out of reach of the overlay.
    QTest::newRow("a string, not a number")
        << QByteArray("\"modmask\": \"64\",") << 0;
    QTest::newRow("null") << QByteArray("\"modmask\": null,") << 0;
    QTest::newRow("a boolean") << QByteArray("\"modmask\": true,") << 0;
}

void TestSources::hyprlandRefusesAnUnreadableModmask() {
    QFETCH(QByteArray, field);
    QFETCH(int, kept);

    const QByteArray payload = "[{" + field +
                               " \"key\": \"Y\", \"dispatcher\": \"exec\","
                               " \"arg\": \"anything\"}]";
    QTemporaryFile dump;
    QVERIFY(dump.open());
    QCOMPARE(dump.write(payload), payload.size());
    dump.close();

    SourceHyprland source(dump.fileName());
    QString error;
    const QList<Bind> binds = source.read(&error);

    QCOMPARE(binds.size(), kept);
    // Whatever falls away is named, never dropped in silence.
    QVERIFY(kept > 0 || !error.isEmpty());
}

void TestSources::hyprlandNamesTheCountsWhenNothingRemains() {
    // Every entry skipped: saying "no keyboard shortcut" alone would tell the
    // reader there are none, when the truth is that none could be shown.
    const QByteArray payload =
        R"([{"modmask": 128, "key": "T", "dispatcher": "exec", "arg": "x"}])";
    QTemporaryFile dump;
    QVERIFY(dump.open());
    QCOMPARE(dump.write(payload), payload.size());
    dump.close();

    SourceHyprland source(dump.fileName());
    QString error;
    const QList<Bind> binds = source.read(&error);

    QVERIFY(binds.isEmpty());
    QVERIFY(error.contains(QStringLiteral("1")));
    QVERIFY(error.contains(QStringLiteral("modifier")));
}

void TestSources::hyprlandFindsTheSocketWithoutARuntimeDir() {
    // An ssh or su shell without a user session: the variable is gone, the
    // instance is not. hyprctl looks under /run/user/$UID then, and reporting
    // the signature as unset would point at the wrong variable entirely.
    const Environment saved;
    qunsetenv(kRuntimeDirVar);
    qputenv(kHyprlandSignatureVar, kSignature);

    const QString path = SourceHyprland::socketPath();
    QVERIFY(path.startsWith(QLatin1String(kHyprlandRuntimeDirFallback)));
    QVERIFY(path.endsWith(QLatin1String(kHyprlandSocketName)));
    QVERIFY(path.contains(QLatin1String(kSignature)));

    SourceHyprland source;
    QString error;
    QVERIFY(source.read(&error).isEmpty());
    // The message has to name the socket it looked for, not a variable that
    // was set all along.
    QVERIFY(!error.contains(QLatin1String(kHyprlandSignatureVar)));
}

void TestSources::hyprlandBlamesTheClockNotTheAnswer() {
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QString directory = runtime.path() + QLatin1Char('/') +
                              QLatin1String(kHyprlandSocketDir) +
                              QLatin1Char('/') + QLatin1String(kSignature);
    QVERIFY(QDir().mkpath(directory));

    // Half an array, and the connection stays open. Reading on would be the
    // reader's job, but the budget ends first.
    FakeCompositor compositor(
        directory + QLatin1Char('/') + QLatin1String(kHyprlandSocketName),
        QByteArray("[{\"modmask\": 64, \"key\": \"T"), false);
    QVERIFY(compositor.startAndWait());

    const Environment saved;
    qputenv(kRuntimeDirVar, runtime.path().toLocal8Bit());
    qputenv(kHyprlandSignatureVar, kSignature);

    SourceHyprland source;
    QString error;
    const QList<Bind> binds = source.read(&error);
    QVERIFY(compositor.wait(kServerWaitMs));

    QVERIFY(binds.isEmpty());
    // Telling the reader to go looking for broken JSON would send them after
    // the wrong thing entirely.
    QVERIFY(!error.contains(QStringLiteral("JSON")));
    QVERIFY(error.contains(QLatin1String(kHyprlandSocketName)));
}

void TestSources::hyprlandReportsAConnectionClosedWithoutAnAnswer() {
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QString directory = runtime.path() + QLatin1Char('/') +
                              QLatin1String(kHyprlandSocketDir) +
                              QLatin1Char('/') + QLatin1String(kSignature);
    QVERIFY(QDir().mkpath(directory));

    // Takes the connection and closes it again without writing. Said apart
    // from the timeout: nothing was cut short here, and apart from a refused
    // connection, because something did answer the door.
    FakeCompositor compositor(directory + QLatin1Char('/') +
                                  QLatin1String(kHyprlandSocketName),
                              QByteArray());
    QVERIFY(compositor.startAndWait());

    const Environment saved;
    qputenv(kRuntimeDirVar, runtime.path().toLocal8Bit());
    qputenv(kHyprlandSignatureVar, kSignature);

    SourceHyprland source;
    QString error;
    const QList<Bind> binds = source.read(&error);
    QVERIFY(compositor.wait(kServerWaitMs));

    QVERIFY(binds.isEmpty());
    // Named positively, or a refused connection would pass just as well: its
    // message carries neither of the two words ruled out below, and the branch
    // this test exists for could vanish unnoticed.
    QVERIFY(error.contains(QStringLiteral("closed")));
    QVERIFY(error.contains(QLatin1String(kHyprlandSocketName)));
    // And neither of the two neighbouring outcomes: the clock did not run out,
    // and there is no reply to call broken.
    QVERIFY(!error.contains(QStringLiteral("JSON")));
    QVERIFY(!error.contains(QStringLiteral("time")));
}

// GUILESS rather than APPLESS: the fake compositor drives a QLocalServer on a
// thread of its own, and the socket notifiers behind it need an application to
// belong to.

// --- sway --------------------------------------------------------------

void TestSources::swayReadsTheSample() {
    SourceSway source(sample(QStringLiteral("sway-config")));
    QString note;
    const QList<Bind> binds = source.read(&note);

    // Twenty-three bind lines in the sample, seven of which cannot be named.
    QCOMPARE(binds.size(), 16);
    // The command is what the shortcut is called, with the variable in it
    // already replaced.
    // Asked through the same normalization the panel shows: Return is drawn
    // as its symbol, and writing that symbol out here would measure this
    // test's spelling rather than the backend.
    const QString enter = QStringLiteral("SUPER") +
                          QLatin1String(kShortcutSeparator) +
                          bindpeek::normalizeKey(QStringLiteral("Return"));
    QCOMPARE(descriptionOf(binds, enter), QStringLiteral("foot"));
    // A word sway knows becomes the sentence for it, not the word itself.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+SHIFT+Q")),
             QStringLiteral("Close window"));
    // The flags say when a bind fires and are none of the combination.
    QCOMPARE(descriptionOf(binds, QStringLiteral("Print")),
             QStringLiteral("grim"));
    // The keyboard group restricts a bind but is not a key of its own.
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+X")),
             QStringLiteral("xterm"));
}

void TestSources::swayResolvesAVariableBuiltFromAnother() {
    // "set $both $mod+Shift" over "set $mod Mod4". What is stored for $both
    // already holds the keys, because its own set line went through the
    // replacement first; a bind written with it must land on them.
    SourceSway source(sample(QStringLiteral("sway-config")));
    QString note;
    const QList<Bind> binds = source.read(&note);

    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+SHIFT+R")),
             QStringLiteral("Reload configuration"));
}

void TestSources::swaySkipsWhatItCannotName() {
    SourceSway source(sample(QStringLiteral("sway-config")));
    QString note;
    const QList<Bind> binds = source.read(&note);

    // Nothing that cannot be held on a keyboard, and nothing under a
    // combination that would not trigger it.
    for (const Bind &bind : binds) {
        QVERIFY2(
            !bind.key.contains(QStringLiteral("button"), Qt::CaseInsensitive),
            qPrintable(bind.key));
        QVERIFY2(!bind.key.contains(QStringLiteral("BTN")),
                 qPrintable(bind.key));
        for (const QString &modifier : bind.modifiers) {
            QVERIFY2(modifierOrder().contains(modifier), qPrintable(modifier));
        }
    }

    // Every skip is reported rather than swallowed: two pointer buttons, one
    // keycode bind, three that need Mod3, Mod5 or Lock, one without a command.
    QVERIFY2(!note.isEmpty(), "skipped lines have to be reported");
    QVERIFY2(note.contains(QStringLiteral("2")), qPrintable(note));
    QVERIFY2(note.contains(QStringLiteral("3")), qPrintable(note));
}

void TestSources::swaySaysWhenAnIncludeIsNotFollowed() {
    // What an include line pulls in is not part of what was read, and neither
    // are the binds in it. That has to be said, or a list missing half the
    // shortcuts looks like a complete one. Over read() and the sample, which
    // is the whole way a caller takes.
    SourceSway source(sample(QStringLiteral("sway-config")));
    QString note;
    source.read(&note);

    // The line, not the file: one line may name a whole directory, and how
    // many files that is cannot be known from here. Pinned as the words it
    // says, because that is the whole of what this reports; the note is not
    // translated in a test, which loads no catalogue.
    QVERIFY2(note.contains(QStringLiteral("include line")), qPrintable(note));

    // And two lines say two. What each of them names is never opened, here
    // as little as anywhere: the count is of lines, and nothing else is
    // knowable from this side.
    QString twoLines;
    SourceSway::parseConfig(
        QStringLiteral("include /etc/sway/config.d/*\ninclude ~/extra\n"),
        &twoLines);
    QVERIFY2(twoLines.contains(QStringLiteral("2")), qPrintable(twoLines));
}

void TestSources::swaySaysWhenThereIsNothingToShow() {
    // A configuration can be read from end to end, leave nothing to show and
    // still count nothing as left out: bindswitch and bindgesture are passed
    // over uncounted on purpose, because a lid and a touchpad are not keys.
    // The caller takes an empty list for a failure and prints the note on a
    // line of its own, so without one that line is blank.
    QString note;
    const QList<Bind> binds = SourceSway::parseConfig(
        QStringLiteral("bindswitch lid:on exec lock\n"
                       "bindgesture swipe:3:right workspace next\n"),
        &note);

    QVERIFY(binds.isEmpty());
    QVERIFY2(note.contains(QStringLiteral("no keyboard shortcut")),
             qPrintable(note));

    // The same over the whole way a caller takes, with the file that started
    // this: one holding nothing at all.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path =
        writeFile(QDir(dir.path()), QStringLiteral("config"), QString());
    QVERIFY(!path.isEmpty());

    QString fromFile;
    SourceSway source(path);
    QVERIFY(source.read(&fromFile).isEmpty());
    QVERIFY2(fromFile.contains(QStringLiteral("no keyboard shortcut")),
             qPrintable(fromFile));
}

void TestSources::swayHeadsBindsWithTheirMode() {
    SourceSway source(sample(QStringLiteral("sway-config")));
    QString note;
    const QList<Bind> binds = source.read(&note);

    // What stands inside a mode block is headed by that mode, and the brace
    // that closes it puts the following binds back under the default heading.
    QString headingOfResize;
    QString headingAfterTheBlock;
    for (const Bind &bind : binds) {
        if (bind.description == QStringLiteral("Resize shrink width 10px")) {
            headingOfResize = bind.group;
        }
        if (bind.description == QStringLiteral("Mode resize")) {
            headingAfterTheBlock = bind.group;
        }
    }
    QCOMPARE(headingOfResize, QStringLiteral("resize"));
    QCOMPARE(headingAfterTheBlock, defaultGroupName());
}

void TestSources::swayAlwaysNamesSomething_data() {
    QTest::addColumn<QString>("command");

    // Every shape a configuration holds that leaves nothing to say. Source.h
    // promises a description on every bind, and each of these took a
    // different way through the naming.
    QTest::newRow("a word this knows, with nothing after it") << "exec";
    QTest::newRow("the same, with an empty argument") << "exec \"\"";
    QTest::newRow("a command of two quotes") << "\"\"";
    QTest::newRow("a command of blanks") << "\"   \"";
}

void TestSources::swayAlwaysNamesSomething() {
    QFETCH(QString, command);

    QString note;
    const QList<Bind> binds = SourceSway::parseConfig(
        QStringLiteral("set $mod Mod4\nbindsym $mod+x ") + command +
            QLatin1Char('\n'),
        &note);

    QCOMPARE(binds.size(), 1);
    QVERIFY2(
        !binds.constFirst().description.trimmed().isEmpty(),
        qPrintable(QStringLiteral("nothing to show for: %1").arg(command)));
}

void TestSources::swayReadsABindEndingInABraceAsABlock() {
    // sway looks for the brace before it looks for a handler for the first
    // word, so a bind ending in a bare brace opens a block there and binds
    // nothing. Read as a bind, the panel would show a shortcut sway never
    // registered.
    QString note;
    const QList<Bind> binds =
        SourceSway::parseConfig(QStringLiteral("mode \"resize\" {\n"
                                               "  bindsym Left exec foo {\n"
                                               "  bindsym Right resize grow\n"
                                               "}\n"
                                               "bindsym Mod4+a exec after\n"),
                                &note);

    QCOMPARE(binds.size(), 2);
    // The brace below closes the block that bind opened, so the mode is still
    // the heading on the last line, exactly as sway has it.
    for (const Bind &bind : binds) {
        QCOMPARE(bind.group, QStringLiteral("resize"));
    }
}

void TestSources::swayTakesABraceFromTheNextLine() {
    // sway takes the brace of a block from the line after it and hangs it on
    // the line it just read, so both spellings open the same block. Read as a
    // line of its own the brace names nothing, and every bind in the mode is
    // filed under the heading around it instead.
    QString note;
    const QList<Bind> binds =
        SourceSway::parseConfig(QStringLiteral("mode \"resize\"\n"
                                               "{\n"
                                               "  bindsym Left resize shrink\n"
                                               "}\n"
                                               "bindsym Mod4+a exec after\n"),
                                &note);

    QCOMPARE(binds.size(), 2);
    QCOMPARE(binds.constFirst().group, QStringLiteral("resize"));
    QCOMPARE(binds.constLast().group, defaultGroupName());

    // The lookahead steps over empty lines and stops at the first line holding
    // anything else. A comment between the two therefore leaves the brace
    // where it stands, naming nothing.
    QString commentedNote;
    const QList<Bind> commented =
        SourceSway::parseConfig(QStringLiteral("mode \"resize\"\n"
                                               "# a note\n"
                                               "{\n"
                                               "  bindsym Left resize shrink\n"
                                               "}\n"),
                                &commentedNote);

    QCOMPARE(commented.size(), 1);
    QCOMPARE(commented.constFirst().group, defaultGroupName());
}

void TestSources::swayOpensNoBlockOnAWordEndingInABrace() {
    // sway compares the last word of a line against a brace, not the last
    // character of the line, so a command ending in one opens nothing. Taken
    // for a block, the mode's own brace would close that phantom instead and
    // everything after the mode would keep its heading.
    QString note;
    const QList<Bind> binds =
        SourceSway::parseConfig(QStringLiteral("mode \"resize\" {\n"
                                               "  exec_always ~/bin/x{\n"
                                               "  bindsym Left resize shrink\n"
                                               "}\n"
                                               "bindsym Mod4+a exec after\n"),
                                &note);

    QCOMPARE(binds.size(), 2);
    QCOMPARE(binds.constFirst().group, QStringLiteral("resize"));
    QCOMPARE(binds.constLast().group, defaultGroupName());
}

void TestSources::swayReadsEveryBindingWordEndingInABraceAsABlock() {
    // The brace is asked for before the line is asked what it binds, so this
    // holds for all four binding words and not only for the one that puts a
    // key on screen. A keycode that opens a block binds nothing and is not
    // counted as left out either: nothing was left out, the line was a block.
    QString note;
    const QList<Bind> binds = SourceSway::parseConfig(
        QStringLiteral("mode \"resize\" {\n"
                       "  bindswitch lid:on exec foo {\n"
                       "  bindgesture swipe:3:right exec bar {\n"
                       "  bindcode 24 exec one {\n"
                       "  bindsym Left resize shrink\n"
                       "}\n"
                       "}\n"
                       "}\n"
                       "bindsym Mod4+a exec after\n"),
        &note);

    QCOMPARE(binds.size(), 2);
    // Three blocks opened and three closed, so the mode is still the heading
    // on the last line.
    for (const Bind &bind : binds) {
        QCOMPARE(bind.group, QStringLiteral("resize"));
    }
    QVERIFY2(note.isEmpty(), qPrintable(note));
}

void TestSources::swayEndsABlockOnTheLastWord() {
    // sway reads the end of a block from the last word of the line, the same
    // way it reads the start, so a line ending in a brace ends the block
    // around it whatever stands in front of that brace.
    QString note;
    const QList<Bind> binds =
        SourceSway::parseConfig(QStringLiteral("mode \"resize\" {\n"
                                               "  bindsym Left resize shrink\n"
                                               "  bar }\n"
                                               "bindsym Mod4+d exec menu\n"),
                                &note);

    QCOMPARE(binds.size(), 2);
    QCOMPARE(binds.constFirst().group, QStringLiteral("resize"));
    QCOMPARE(binds.constLast().group, defaultGroupName());

    // And a brace with words after it ends nothing: only a line beginning
    // with "#" is a comment to sway, so those words are arguments and the
    // last of them is not a brace.
    QString trailing;
    const QList<Bind> stillInside =
        SourceSway::parseConfig(QStringLiteral("mode \"resize\" {\n"
                                               "  bindsym Left resize shrink\n"
                                               "} # back to normal\n"
                                               "bindsym Mod4+d exec menu\n"),
                                &trailing);

    QCOMPARE(stillInside.size(), 2);
    for (const Bind &bind : stillInside) {
        QCOMPARE(bind.group, QStringLiteral("resize"));
    }
}

void TestSources::swayOpensNoBlockOnAVariableHoldingABrace() {
    // sway looks for the brace before it replaces a variable, so a line whose
    // last word is the name of a variable opens no block, whatever that
    // variable holds. Opened here, the brace below would close the phantom
    // instead of the mode and everything after it would keep the heading.
    QString note;
    const QList<Bind> binds =
        SourceSway::parseConfig(QStringLiteral("set $brace \"{\"\n"
                                               "mode \"resize\" {\n"
                                               "  bindsym Left resize shrink\n"
                                               "  bar $brace\n"
                                               "}\n"
                                               "bindsym Mod4+d exec menu\n"),
                                &note);

    QCOMPARE(binds.size(), 2);
    QCOMPARE(binds.constFirst().group, QStringLiteral("resize"));
    QCOMPARE(binds.constLast().group, defaultGroupName());
}

void TestSources::swayNamesAModeThroughAVariable() {
    // The name of a block is expanded even though the brace beside it is not:
    // sway hangs the name in front of every line inside the block and replaces
    // the variables in the two together, so the mode is headed by what the
    // variable holds rather than by the name of the variable.
    QString note;
    const QList<Bind> binds =
        SourceSway::parseConfig(QStringLiteral("set $name resize\n"
                                               "mode $name {\n"
                                               "  bindsym Left resize shrink\n"
                                               "}\n"),
                                &note);

    QCOMPARE(binds.size(), 1);
    QCOMPARE(binds.constFirst().group, QStringLiteral("resize"));
}

void TestSources::swayReadsEveryBindingWordAsOne() {
    // sway binds with four words, and only one of them puts a key on screen.
    // A keycode is counted as left out, a switch and a gesture are not.
    QString note;
    const QList<Bind> binds = SourceSway::parseConfig(
        QStringLiteral("set $mod Mod4\n"
                       "mode \"resize\" {\n"
                       "  bindswitch lid:on exec foo\n"
                       "  bindgesture swipe:3:right exec bar\n"
                       "  bindcode 24 exec one\n"
                       "  bindcode 25 exec two\n"
                       "  bindcode 26 exec three\n"
                       "  bindsym Right resize grow\n"
                       "}\n"
                       "bindsym $mod+a exec after\n"),
        &note);

    // Only the two that name a key.
    QCOMPARE(binds.size(), 2);
    // The one inside the mode is headed by it.
    QCOMPARE(binds.constFirst().group, QStringLiteral("resize"));
    // And the one after the mode stands outside it.
    QCOMPARE(binds.constLast().group, defaultGroupName());
    // One sentence, and it counts the three keycodes and nothing else. Three
    // rather than one, because a count of one is written out as a word in
    // some languages and would carry no digit to look for; and the switch and
    // the gesture would make it five if they were counted with them.
    //
    // A switch and a gesture were never keyboard shortcuts, so neither is
    // reported as missing, which a second sentence would say.
    //
    // Counting sentences by the separator holds only where no sentence
    // carries it, which is true of every sentence this backend writes and
    // measured to be so; one of Hyprland's does carry one, which is why
    // Source.h warns against the practice in general.
    QCOMPARE(note.count(QLatin1String(kNoteSeparator)), 0);
    QVERIFY2(note.contains(QStringLiteral("3")), qPrintable(note));
}

void TestSources::swayDropsAGroupWhereverItStands() {
    // sway splits the combination at every "+" and tests each part for the
    // group, so it is not always in front. Read from its own source, where
    // Mode_switch is the same thing under an older name.
    QString note;
    const QList<Bind> binds = SourceSway::parseConfig(
        QStringLiteral("set $mod Mod4\n"
                       "bindsym $mod+Group2+y exec first\n"
                       "bindsym Group2+$mod+z exec second\n"
                       "bindsym $mod+Mode_switch+w exec third\n"),
        &note);

    QCOMPARE(binds.size(), 3);
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+Y")),
             QStringLiteral("first"));
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+Z")),
             QStringLiteral("second"));
    QCOMPARE(descriptionOf(binds, QStringLiteral("SUPER+W")),
             QStringLiteral("third"));
}

void TestSources::swayKeepsAModeAcrossAnInnerBlock() {
    // Not every block is a mode. Counting the brace of a "bar" as a mode's
    // would end the heading early, and everything after it would be filed
    // under the default one.
    QString note;
    const QList<Bind> binds = SourceSway::parseConfig(
        QStringLiteral("mode \"resize\" {\n"
                       "  bindsym Left resize shrink width 10px\n"
                       "  bar {\n"
                       "    position top\n"
                       "  }\n"
                       "  bindsym Right resize grow width 10px\n"
                       "}\n"
                       "bindsym Mod4+a exec after\n"),
        &note);

    QCOMPARE(binds.size(), 3);
    for (const Bind &bind : binds) {
        const bool inside =
            bind.description.startsWith(QStringLiteral("Resize"));
        QCOMPARE(bind.group,
                 inside ? QStringLiteral("resize") : defaultGroupName());
    }
}

void TestSources::swayRefusesAnAnswerTooLargeToBeOne() {
    // The socket is named by an environment variable, so what answers is not
    // guaranteed to be sway. A length word is four bytes and can say four
    // gigabytes, which would be asked of the thread that draws before a byte
    // of it is read.
    QByteArray reply("i3-ipc", 6);
    const quint32 length = 0xFFFFFFFFU;
    const quint32 type = 9;
    reply.append(reinterpret_cast<const char *>(&length), sizeof(length));
    reply.append(reinterpret_cast<const char *>(&type), sizeof(type));

    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QString path = runtime.path() + QStringLiteral("/sway-ipc.sock");

    FakeCompositor compositor(path, reply);
    QVERIFY(compositor.startAndWait());

    const Environment saved;
    qputenv(kSwaySocketVar, path.toLocal8Bit());

    SourceSway source;
    QString note;
    const QList<Bind> binds = source.read(&note);
    QVERIFY(compositor.wait(kServerWaitMs));

    QVERIFY(binds.isEmpty());
    // The announced length has to appear in the message, or this passes just
    // as well when the read simply ran out of time and nothing was checked.
    // The number rather than a word, because the message is translated.
    QVERIFY2(note.contains(QString::number(length)), qPrintable(note));
}

void TestSources::swayAsksTheRunningCompositor() {
    QFile config(sample(QStringLiteral("sway-config")));
    QVERIFY(config.open(QIODevice::ReadOnly));
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("config"), QString::fromUtf8(config.readAll())}});
    const QByteArray payload = document.toJson(QJsonDocument::Compact);

    // The reply in the protocol's own shape: magic, length, type, payload.
    QByteArray reply("i3-ipc", 6);
    const auto length = static_cast<quint32>(payload.size());
    const quint32 type = 9;
    reply.append(reinterpret_cast<const char *>(&length), sizeof(length));
    reply.append(reinterpret_cast<const char *>(&type), sizeof(type));
    reply.append(payload);

    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QString path = runtime.path() + QStringLiteral("/sway-ipc.sock");

    FakeCompositor compositor(path, reply);
    QVERIFY(compositor.startAndWait());

    const Environment saved;
    qputenv(kSwaySocketVar, path.toLocal8Bit());

    SourceSway source;
    QString note;
    const QList<Bind> binds = source.read(&note);
    QVERIFY(compositor.wait(kServerWaitMs));

    // What went out is the request for the configuration, in the same shape:
    // the magic, no payload, and the type that asks for it.
    const QByteArray request = compositor.request();
    QCOMPARE(request.size(), 14);
    QCOMPARE(request.left(6), QByteArray("i3-ipc"));
    quint32 sentLength = 0;
    quint32 sentType = 0;
    memcpy(&sentLength, request.constData() + 6, sizeof(sentLength));
    memcpy(&sentType, request.constData() + 10, sizeof(sentType));
    QCOMPARE(sentLength, 0U);
    QCOMPARE(sentType, 9U);

    // Same list as from the file: the transport changes nothing.
    QCOMPARE(binds.size(), 16);
    // Asked through the same normalization the panel shows: Return is drawn
    // as its symbol, and writing that symbol out here would measure this
    // test's spelling rather than the backend.
    const QString enter = QStringLiteral("SUPER") +
                          QLatin1String(kShortcutSeparator) +
                          bindpeek::normalizeKey(QStringLiteral("Return"));
    QCOMPARE(descriptionOf(binds, enter), QStringLiteral("foot"));
}

QTEST_GUILESS_MAIN(TestSources)
#include "test_sources.moc"
