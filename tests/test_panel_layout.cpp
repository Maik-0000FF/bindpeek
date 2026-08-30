// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the panel itself, which no other test does.
//
// qmllint sees types and imports, and the contract test compares names. What
// neither can see is a column that came out a fraction too narrow for its own
// text, or a line that says what did not fit while the type is still being
// stepped down. Both were shipped once; this holds them.
//
// Nothing here compares pixels against numbers written down. The panel is set
// in whichever family the machine happens to have, and the four distributions
// this is built on do not agree on one, so every measurement is held against
// another measurement taken in the same run.

#include "Appearance.h"
#include "Settings.h"
#include "SystemScheme.h"

#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

using namespace bindpeek;

namespace {

// The size the panel is asked for, which is the largest the setting allows.
// Picked so the fitting has somewhere to come down from: at the default the
// bounds below would be met without a single step, and the test would pass
// without ever exercising what it is here for.
constexpr int kAskedFontSizePt = 48;

// How long the fitting is given before the test calls it stuck.
//
// Far over the second or so it takes here, and deliberately so: the same
// steps run again under a sanitizer, where every layout costs several times
// more, and a test that fails on a busy afternoon is worse than no test. A
// fitting that does come to rest never waits this out, only one that is
// genuinely stuck does.
constexpr int kFitBudgetMs = 60000;

// The gap between two looks at the panel. Half a round, so a round cannot pass
// unseen between two samples.
constexpr int kSampleMs = 8;

// How long a panel that is holding its size is watched.
//
// Twenty-five rounds. The walk this stands in for takes the type down a point
// a round, so a walk that ran at all would be visible many times over by the
// end of it.
constexpr int kHeldRoundsMs = 400;

// Everything under an item that carries the name, gathered down the visual
// tree rather than the object tree.
//
// A repeater hands its delegates to the item they are drawn in but not to it
// as children, so findChildren walks straight past every row of the table and
// answers with the handful of items the file itself declares.
void gather(QQuickItem *item, const QString &name, QList<QQuickItem *> &found) {
    if (item == nullptr) {
        return;
    }
    if (item->objectName() == name) {
        found.append(item);
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        gather(child, name, found);
    }
}

QList<QQuickItem *> itemsNamed(QQuickItem *root, const QString &name) {
    QList<QQuickItem *> found;
    gather(root, name, found);
    return found;
}

QVariantMap entry(const QString &shortcut, const QString &description) {
    return QVariantMap{{"shortcut", shortcut},       {"key", shortcut},
                       {"description", description}, {"deeper", false},
                       {"section", QString()},       {"caps", QVariantList()}};
}

// One group per shortcut, which is what puts each of them in the place the
// column is measured from: the widest of its own group.
QVariantList groupsOf(const QStringList &shortcuts) {
    QVariantList groups;
    for (int i = 0; i < shortcuts.size(); ++i) {
        groups.append(QVariantMap{
            {"name", QStringLiteral("Group %1").arg(i)},
            {"entries", QVariantList{entry(shortcuts.at(i),
                                           QStringLiteral("Some action"))}}});
    }
    return groups;
}

QVariantList manyGroups(int count, int rows) {
    QVariantList groups;
    for (int g = 0; g < count; ++g) {
        QVariantList entries;
        for (int r = 0; r < rows; ++r) {
            entries.append(
                entry(QStringLiteral("SUPER+SHIFT+%1").arg(QChar('A' + r % 26)),
                      QStringLiteral("Action number %1").arg(r)));
        }
        groups.append(QVariantMap{{"name", QStringLiteral("Group %1").arg(g)},
                                  {"entries", entries}});
    }
    return groups;
}

} // namespace

// What the panel is measured against, in one object.
//
// Every input reaches the panel as a binding from here rather than as a value
// written into the item afterwards, and the whole of it stands before the
// document is built. Measured, that is the difference between a panel that
// lays out its groups and one that counts them and creates nothing: what a
// panel is given after it was completed at no size does not bring it back.
class Bench : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList groups MEMBER m_groups CONSTANT)
    Q_PROPERTY(int maxWidth MEMBER m_maxWidth CONSTANT)
    Q_PROPERTY(int maxHeight MEMBER m_maxHeight CONSTANT)
    Q_PROPERTY(bool fitsToBounds MEMBER m_fitsToBounds CONSTANT)
    Q_PROPERTY(bool showing MEMBER m_showing CONSTANT)
    // The one input here that changes after the document stands, because the
    // thing it describes is a wait: it is set before anything is built and
    // dropped once the answer is in, and a test that could not drop it could
    // only ever look at the waiting half.
    Q_PROPERTY(bool awaitsItsSize READ awaitsItsSize WRITE setAwaitsItsSize
                   NOTIFY awaitsItsSizeChanged)

public:
    Bench(QVariantList groups, int maxWidth, int maxHeight, bool fitsToBounds,
          bool showing, bool awaitsItsSize = false)
        : m_groups(std::move(groups)), m_maxWidth(maxWidth),
          m_maxHeight(maxHeight), m_fitsToBounds(fitsToBounds),
          m_showing(showing), m_awaitsItsSize(awaitsItsSize) {}

    bool awaitsItsSize() const { return m_awaitsItsSize; }

    void setAwaitsItsSize(bool value) {
        if (m_awaitsItsSize == value) {
            return;
        }
        m_awaitsItsSize = value;
        emit awaitsItsSizeChanged();
    }

signals:
    void awaitsItsSizeChanged();

private:
    QVariantList m_groups;
    int m_maxWidth;
    int m_maxHeight;
    bool m_fitsToBounds;
    bool m_showing;
    bool m_awaitsItsSize;
};

namespace {

// The panel is put inside a document of its own rather than loaded as one.
//
// That is how both places that draw it do it, the overlay and the preview in
// the settings window. The host carries a size of its own, because a panel
// filling a host of nothing is a panel of nothing.
//
// One line is left open, and it is the one the two callers write differently:
// the overlay knows its bound is a display and says so outright, the preview
// hands one in. A literal is taken as the object is made and a binding only
// once it stands, so which of the two it is decides how early the panel is
// asked to measure. Both are spelled here rather than only the convenient one.
constexpr char kHost[] = R"(
import QtQuick

Item {
    width: Bench.maxWidth
    height: Bench.maxHeight

    Theme {
        id: hostTheme
    }

    PanelBody {
        objectName: "panel"
        anchors.fill: parent
        theme: hostTheme
        fitsToBounds: %1
        showing: Bench.showing
        awaitsItsSize: Bench.awaitsItsSize
        maxWidth: Bench.maxWidth
        maxHeight: Bench.maxHeight
        heldText: "SUPER"
        groups: Bench.groups
    }
}
)";

// The same panel with the probe beside it, wired the way the overlay wires
// them and in that order: the panel waits on the probe, and an answer reaches
// it through the panel's own door rather than through the test's hand.
//
// The probe reads everything that decides a size off the panel it answers for,
// so there is one wiring here and not two; see FitProbe.qml.
constexpr char kProbeHost[] = R"(
import QtQuick

Item {
    width: Bench.maxWidth
    height: Bench.maxHeight

    Theme {
        id: hostTheme
    }

    PanelBody {
        id: panel
        objectName: "panel"
        anchors.fill: parent
        theme: hostTheme
        fitsToBounds: %1
        showing: Bench.showing
        awaitsItsSize: probe.waiting
        maxWidth: Bench.maxWidth
        maxHeight: Bench.maxHeight
        heldText: "SUPER"
        groups: Bench.groups
    }

    FitProbe {
        id: probe
        objectName: "probe"
        anchors.fill: parent
        like: panel

        onFound: size => panel.takeTheSizeFound(size)
    }
}
)";

// How the two callers spell it; see kHost.
const QString kBoundAsBinding = QStringLiteral("Bench.fitsToBounds");
const QString kBoundAsLiteral = QStringLiteral("true");
// A panel that never fits itself, which is how the probe is asked about on its
// own: whatever moves in the theme then moved because of the probe.
const QString kBoundAsOff = QStringLiteral("false");

// Whether the fitting has come to rest at a size it actually looked for.
//
// Asking the panel alone is not enough. The flow answers with no size at all
// until the first layout has run, so nothing overflows yet, and a search whose
// try and confirmation both fall before that first frame comes to rest on the
// size that was configured. The panel leaves that rest as soon as the overflow
// shows itself, but a sample taken inside it reads as a fitting that is over,
// which is a race against the first frame rather than a fitting to measure.
//
// The size is what both callers go on to ask about anyway, so requiring it
// here costs nothing and cannot answer with the wrong rest.
bool cameToRest(QQuickItem *panel, QObject *theme) {
    return panel->property("fitSettled").toBool() &&
           theme->property("fontSizePt").toInt() <
               theme->property("configuredFontSizePt").toInt();
}

// The probe's answer, or zero where none that fits arrived in time.
//
// Waited for the same way the panel is, and for the same reason: the probe
// searches with the panel's own fitting, so it meets the same first frame. An
// answer reached before anything was laid out is the size it started from, and
// the overflow takes it down from there a moment later.
int waitForTheProbe(QObject *probe, int asked) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        const int size = probe->property("size").toInt();
        // Come to rest, and not let go of because the wait ran out: the size
        // standing at that moment is one of the sizes the search was trying,
        // which is not an answer to anything.
        if (!probe->property("waiting").toBool() &&
            !probe->property("gaveUp").toBool() && size > 0 && size < asked) {
            return size;
        }
    }
    return 0;
}

// Everything QML said while a panel was being made.
//
// A warning is not a failure to QtTest, so a panel that reached for something
// it did not have yet went on passing every test in this file while saying so
// on the way past. Collected rather than counted, because a test that fails
// has to be able to print what was said.
QStringList g_said;
QtMessageHandler g_saidBefore = nullptr;

void collectWhatWasSaid(QtMsgType type, const QMessageLogContext &context,
                        const QString &message) {
    g_said.append(message);
    if (g_saidBefore != nullptr) {
        g_saidBefore(type, context, message);
    }
}

} // namespace

class TestPanelLayout : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void shortcutColumnHoldsItsText();
    void warningWaitsForTheFit_data();
    void warningWaitsForTheFit();
    void aLiteralBoundStillGetsItsFit();
    void theProbeAnswersWithoutTouchingThePanel();
    void aSizeOnItsWayHoldsTheWalk();
    void theSizeFoundOnlyEverLowers();
    void theWalkDoesNotUndercutTheAnswer();
    void theWalkDoesNotUndercutALateAnswer();

private:
    // Puts a panel measured against the bench on an offscreen window. Returns
    // the panel, or nullptr with the failure already reported.
    QQuickItem *showPanel(QQuickView &view, Bench &bench,
                          const QString &boundAs = kBoundAsBinding,
                          const char *document = kHost);

    QTemporaryDir m_home;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SystemScheme> m_scheme;
    std::unique_ptr<Appearance> m_appearance;
};

void TestPanelLayout::initTestCase() {
    QVERIFY(m_home.isValid());

    // A settings file of its own, so the test measures the size it asked for
    // and never touches the one belonging to whoever runs it.
    const QString path = m_home.filePath(QStringLiteral("bindpeek.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QByteArrayLiteral("fontSizePt=") +
               QByteArray::number(kAskedFontSizePt) + "\n");
    file.close();

    m_settings = std::make_unique<Settings>(path);
    QCOMPARE(m_settings->fontSizePt(), kAskedFontSizePt);

    m_scheme = std::make_unique<SystemScheme>();
    m_appearance = std::make_unique<Appearance>(*m_settings, m_scheme.get());
}

QQuickItem *TestPanelLayout::showPanel(QQuickView &view, Bench &bench,
                                       const QString &boundAs,
                                       const char *document) {
    view.engine()->rootContext()->setContextProperty(
        QStringLiteral("Appearance"), m_appearance.get());
    view.engine()->rootContext()->setContextProperty(QStringLiteral("Bench"),
                                                     &bench);

    // Based inside the source directory, which is what lets the document below
    // name PanelBody and Theme without an import: they are its neighbours.
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(BINDPEEK_SRC) +
                                          QStringLiteral("/bench.qml"));
    auto *host = new QQmlComponent(view.engine(), &view);
    host->setData(QString::fromLatin1(document).arg(boundAs).toUtf8(), base);
    if (host->isError()) {
        QTest::qFail(qPrintable(host->errorString()), __FILE__, __LINE__);
        return nullptr;
    }

    QObject *root = host->create(view.rootContext());
    if (root == nullptr) {
        QTest::qFail("the host document made nothing", __FILE__, __LINE__);
        return nullptr;
    }
    view.setContent(base, host, root);

    // Shown, because a window that is never shown produces no frame, and
    // without a frame nothing is laid out and every width below reads zero.
    // The platform is offscreen, so this puts nothing in front of anybody.
    view.show();
    if (!QTest::qWaitForWindowExposed(&view)) {
        QTest::qFail("the window never came up", __FILE__, __LINE__);
        return nullptr;
    }

    auto *panel = root->findChild<QQuickItem *>(QStringLiteral("panel"));
    if (panel == nullptr) {
        QTest::qFail("no panel in the host document", __FILE__, __LINE__);
    }
    return panel;
}

// A shortcut narrower than the column's cap is drawn whole.
//
// It used to be measured from the ink the text puts down rather than the
// advance it lays out to, which left the column a fraction too narrow for the
// very string it was measured from, and that string then lost its last
// character to the elide.
void TestPanelLayout::shortcutColumnHoldsItsText() {
    // A spread of lengths rather than one, because whether a string lands
    // short of the cap depends on the family the machine offers.
    const QStringList shortcuts{
        QStringLiteral("SUPER+A"),         QStringLiteral("SUPER+Left"),
        QStringLiteral("SUPER+Right"),     QStringLiteral("CTRL+ALT+Del"),
        QStringLiteral("SUPER+SHIFT+A"),   QStringLiteral("SUPER+SHIFT+Up"),
        QStringLiteral("CTRL+SHIFT+Left"), QStringLiteral("SUPER+ALT+Enter")};

    // The fitting is off: this asks about the column, not about the fitting,
    // and a panel that lowered its type would measure a size nobody
    // configured. The bound is wide enough that nothing has to wrap.
    Bench bench(groupsOf(shortcuts), 3000, 2000, false, false);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench);
    QVERIFY(panel != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const double cap = theme->property("columnShortcut").toDouble();
    QVERIFY(cap > 0);

    const QList<QQuickItem *> texts =
        itemsNamed(panel, QStringLiteral("shortcutText"));
    QCOMPARE(texts.size(), shortcuts.size());

    int held = 0;
    for (QQuickItem *text : texts) {
        const QString what = text->property("text").toString();
        const double wants = text->property("implicitWidth").toDouble();
        QVERIFY2(wants > 0,
                 qPrintable(what + QStringLiteral(" measured zero")));

        // Past the cap it is elided on purpose, and that is the one case this
        // says nothing about.
        if (wants > cap) {
            continue;
        }
        ++held;
        QVERIFY2(!text->property("truncated").toBool(),
                 qPrintable(QStringLiteral("%1 fits the column (%2 of %3) and "
                                           "was cut off anyway")
                                .arg(what)
                                .arg(wants)
                                .arg(cap)));
    }

    // Every string past the cap would leave nothing to check, and a test that
    // checks nothing passes for the wrong reason.
    QVERIFY2(held > 0, "no shortcut landed short of the cap");
}

void TestPanelLayout::warningWaitsForTheFit_data() {
    QTest::addColumn<QVariantList>("groups");
    QTest::addColumn<int>("maxWidth");
    QTest::addColumn<int>("maxHeight");
    QTest::addColumn<bool>("standsAtRest");

    QTest::newRow("fits once the type has come down")
        << manyGroups(6, 14) << 1400 << 800 << false;
    // Small lists against a small bound rather than a huge list against a
    // large one: the stepping is what takes the time, one point a round from
    // the size asked for down to the floor, and every row of every group is
    // laid out again on each of them. A list eight times the size answers the
    // same question and takes eight times as long to do it.
    QTest::newRow("does not fit even at the floor")
        << manyGroups(12, 12) << 320 << 160 << true;
}

// The line at the foot waits for the fitting.
//
// While a size is being stepped towards, the rows overflow by definition, so
// the line used to stand for as long as the fitting took and ask for a
// modifier that nothing needed.
void TestPanelLayout::warningWaitsForTheFit() {
    QFETCH(QVariantList, groups);
    QFETCH(int, maxWidth);
    QFETCH(int, maxHeight);
    QFETCH(bool, standsAtRest);

    // Shown and bounded, which is what chooses stepping in plain view over
    // searching out of sight.
    Bench bench(groups, maxWidth, maxHeight, true, true);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench);
    QVERIFY(panel != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const QList<QQuickItem *> warnings =
        itemsNamed(panel, QStringLiteral("overflowWarning"));
    QCOMPARE(warnings.size(), 1);
    QQuickItem *warning = warnings.first();

    // The groups are there to be measured, so what follows is about the
    // fitting and not about an empty panel.
    QVERIFY(!itemsNamed(panel, QStringLiteral("shortcutText")).isEmpty());

    QElapsedTimer clock;
    clock.start();
    int stoodTooEarly = 0;
    bool settled = false;
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        settled = cameToRest(panel, theme);
        if (!settled && warning->property("visible").toBool()) {
            ++stoodTooEarly;
        }
        if (settled) {
            break;
        }
    }

    QVERIFY2(settled, "the fitting never came to rest at a size it looked for");
    QCOMPARE(stoodTooEarly, 0);

    // The type came down on the way, which the wait above already required:
    // the samples looked at a fitting that ran rather than at one that was
    // over before it began.
    const int asked = theme->property("configuredFontSizePt").toInt();
    const int settledAt = theme->property("fontSizePt").toInt();
    QCOMPARE(asked, kAskedFontSizePt);

    QCOMPARE(warning->property("visible").toBool(), standsAtRest);
    if (standsAtRest) {
        QCOMPARE(settledAt, theme->property("minFontSizePt").toInt());
    }
}

// A bound spelled outright still gets its fit, and gets it quietly.
//
// The overlay writes its bound as a literal, and a literal is taken while the
// object is being made, before the theme handed in has been bound to anything.
// The panel started measuring right there and read the theme through it: a
// TypeError at every start, and the round that threw it lost with it. What
// kept that out of sight for so long is that a binding further down the same
// document started the search over a moment later, so the panel came out
// right and complained on the way.
void TestPanelLayout::aLiteralBoundStillGetsItsFit() {
    // Out of sight and bounded, which is the searching path the overlay takes
    // before a panel is ever drawn.
    Bench bench(manyGroups(6, 14), 1400, 800, true, false);

    g_said.clear();
    g_saidBefore = qInstallMessageHandler(collectWhatWasSaid);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench, kBoundAsLiteral);
    qInstallMessageHandler(g_saidBefore);
    g_saidBefore = nullptr;
    QVERIFY(panel != nullptr);

    const QStringList complaints =
        g_said.filter(QStringLiteral("PanelBody.qml"));
    QVERIFY2(complaints.isEmpty(),
             qPrintable(QStringLiteral("said while the panel was made:\n") +
                        complaints.join(QLatin1Char('\n'))));

    // And it did look for a size, so the quiet above is not the quiet of a
    // panel that never measured at all: the rest waited for below is one the
    // type had to come down to reach.
    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);

    QElapsedTimer clock;
    clock.start();
    bool settled = false;
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        settled = cameToRest(panel, theme);
        if (settled) {
            break;
        }
    }
    QVERIFY2(settled, "the fitting never came to rest at a size it looked for");
}

// The size is worked out beside the panel, never in it.
//
// The panel here is told not to fit itself, so the theme it draws from stands
// at the size that was configured and stays there. The probe measures the same
// list against the same bound in a theme of its own and comes back with a
// smaller one. That number is the whole of what the panel on screen is later
// handed, in place of walking down to it.
void TestPanelLayout::theProbeAnswersWithoutTouchingThePanel() {
    // Shown, and far more than fits: the case where the panel would otherwise
    // take its type down a point at a time in front of the reader.
    Bench bench(manyGroups(6, 14), 1400, 800, false, true);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench, kBoundAsOff, kProbeHost);
    QVERIFY(panel != nullptr);

    QObject *probe =
        view.rootObject()->findChild<QObject *>(QStringLiteral("probe"));
    QVERIFY(probe != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const int asked = theme->property("configuredFontSizePt").toInt();
    QCOMPARE(asked, kAskedFontSizePt);

    // Waited for the same way the panel is; see cameToRest. The probe searches
    // with the panel's own fitting, so it meets the same first frame: an
    // answer that arrives before anything was laid out is the size it started
    // from, and the overflow takes it down from there a moment later.
    const int size = waitForTheProbe(probe, asked);
    QVERIFY2(size > 0, "the probe never answered with a size that fits");

    // And it got there without moving the panel: whatever the search wrote, it
    // wrote into a theme of its own.
    QCOMPARE(theme->property("fontSizePt").toInt(), asked);
}

// A size on its way holds the walk.
//
// While the probe measures, the panel on screen is told a size is coming. It
// then keeps the one it has: stepping towards the same number in the meantime
// is the walk the answer is there to replace, and it would be walked in front
// of somebody reading the panel.
//
// The wait is ended here without an answer, which is the case the panel has to
// come out of on its own. The walk that was held back runs then, so a size
// that never arrives costs what it always cost and nothing stands in a size
// that does not fit.
void TestPanelLayout::aSizeOnItsWayHoldsTheWalk() {
    // Shown, bounded, and waiting on a size from elsewhere.
    Bench bench(manyGroups(6, 14), 1400, 800, true, true, true);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench);
    QVERIFY(panel != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const int asked = theme->property("configuredFontSizePt").toInt();
    QCOMPARE(asked, kAskedFontSizePt);

    const QList<QQuickItem *> warnings =
        itemsNamed(panel, QStringLiteral("overflowWarning"));
    QCOMPARE(warnings.size(), 1);
    QQuickItem *warning = warnings.first();

    QTest::qWait(kHeldRoundsMs);
    QCOMPARE(theme->property("fontSizePt").toInt(), asked);
    // Nothing has come to rest either, so the line at the foot says nothing:
    // the rows overflow for as long as the wait lasts, and a verdict there
    // would be a verdict on the wait.
    QCOMPARE(panel->property("fitSettled").toBool(), false);
    QCOMPARE(warning->property("visible").toBool(), false);

    // Dropped: the walk that was held back runs.
    //
    // Waited for by what it is rather than for a length of time: the type has
    // moved and nothing has come to rest, which is the middle of a walk on any
    // machine. A fixed wait would be a guess about the font this one happens
    // to have, and on a font that fits sooner the walk would be over by then.
    bool walking = false;
    QElapsedTimer walk;
    walk.start();
    bench.setAwaitsItsSize(false);
    while (walk.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        if (theme->property("fontSizePt").toInt() < asked &&
            !panel->property("fitSettled").toBool()) {
            walking = true;
            break;
        }
    }
    QVERIFY2(walking, "the walk never ran once the wait was over");

    // Held again in the middle of that walk, which is a further modifier under
    // a panel already coming down: what it was stepping towards answers the
    // list before this one, so it stops where it is rather than walking on
    // through the wait and past the size about to arrive.
    bench.setAwaitsItsSize(true);
    const int stopped = theme->property("fontSizePt").toInt();
    QTest::qWait(kHeldRoundsMs);
    QCOMPARE(theme->property("fontSizePt").toInt(), stopped);

    bench.setAwaitsItsSize(false);

    QElapsedTimer clock;
    clock.start();
    bool settled = false;
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        settled = cameToRest(panel, theme);
        if (settled) {
            break;
        }
    }
    QVERIFY2(settled, "the walk never finished once the wait was over");
    // And it had further to go when it was stopped, so the stop above was a
    // walk held mid-stride and not one that had already ended.
    QVERIFY2(theme->property("fontSizePt").toInt() < stopped,
             "the walk had already finished when it was held");
}

// A size worked out elsewhere is taken downwards and no other way.
//
// The search behind it starts from the size that was asked for every time, so
// its answer to a list that grew shorter is larger than what stands. Taking
// that would put the type back up under the reader's hand, halfway through one
// gesture, which is the one direction the fitting never moves.
void TestPanelLayout::theSizeFoundOnlyEverLowers() {
    // A list with room to spare, so nothing here walks on its own and what the
    // handed size does is all that moves. Shown, bounded and waiting, which is
    // the state an answer lands in.
    Bench bench(
        groupsOf({QStringLiteral("SUPER+A"), QStringLiteral("SUPER+B")}), 3000,
        2000, true, true, true);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench);
    QVERIFY(panel != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const int asked = theme->property("configuredFontSizePt").toInt();
    QCOMPARE(asked, kAskedFontSizePt);
    QCOMPARE(theme->property("fontSizePt").toInt(), asked);

    // Larger than what stands: left where it is.
    QVERIFY(QMetaObject::invokeMethod(panel, "takeTheSizeFound",
                                      Q_ARG(QVariant, QVariant(asked + 6))));
    QCOMPARE(theme->property("fontSizePt").toInt(), asked);

    // Smaller: taken, which is the whole point of asking.
    QVERIFY(QMetaObject::invokeMethod(panel, "takeTheSizeFound",
                                      Q_ARG(QVariant, QVariant(asked - 6))));
    QCOMPARE(theme->property("fontSizePt").toInt(), asked - 6);

    // And larger again afterwards stays out, so the size settles one way
    // through a gesture however the lists run.
    QVERIFY(QMetaObject::invokeMethod(panel, "takeTheSizeFound",
                                      Q_ARG(QVariant, QVariant(asked))));
    QCOMPARE(theme->property("fontSizePt").toInt(), asked - 6);
}

// The walk that follows a wait does not take the answer down a point.
//
// The size lands in one frame and the rows are laid out against it in the
// next. A walk that read the overflow in between would read a verdict on the
// size before it and step down for something already fixed, and because the
// type only ever goes down, that point would be lost for the rest of the
// gesture and the one after it.
void TestPanelLayout::theWalkDoesNotUndercutTheAnswer() {
    // More than fits at the size asked for, so the rows are overflowing while
    // the panel holds its size. That is the state the answer arrives in.
    //
    // Nothing here hands the size over or ends the wait: the host wires the
    // two together the way the overlay does, so the answer and the end of the
    // wait reach the panel in the order they reach it in the program.
    Bench bench(manyGroups(6, 14), 1400, 800, true, true);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench, kBoundAsBinding, kProbeHost);
    QVERIFY(panel != nullptr);

    QObject *probe =
        view.rootObject()->findChild<QObject *>(QStringLiteral("probe"));
    QVERIFY(probe != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const int asked = theme->property("configuredFontSizePt").toInt();
    QCOMPARE(asked, kAskedFontSizePt);

    const int found = waitForTheProbe(probe, asked);
    QVERIFY2(found > 0, "the probe never answered with a size that fits");

    QElapsedTimer clock;
    clock.start();
    bool settled = false;
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        settled = cameToRest(panel, theme);
        if (settled) {
            break;
        }
    }
    QVERIFY2(settled, "the fitting never came to rest after the answer");
    QCOMPARE(theme->property("fontSizePt").toInt(), found);
}

// An answer that arrives in the middle of a walk is not walked past.
//
// Where the wait for a size runs out first, the panel is already taking itself
// down when the answer lands. A round armed before that comes back on an
// overflow measured against the size before it, and takes the answer down one
// further point for something it had already fixed. The type only ever goes
// down, so that point is gone for the rest of the gesture.
void TestPanelLayout::theWalkDoesNotUndercutALateAnswer() {
    // Shown and bounded with nothing waited for, so the panel walks its own
    // way down first and says what fits here.
    Bench bench(manyGroups(6, 14), 1400, 800, true, true);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench);
    QVERIFY(panel != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const int asked = theme->property("configuredFontSizePt").toInt();
    QCOMPARE(asked, kAskedFontSizePt);

    QElapsedTimer clock;
    clock.start();
    bool settled = false;
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        settled = cameToRest(panel, theme);
        if (settled) {
            break;
        }
    }
    QVERIFY2(settled, "the fitting never came to rest");
    const int fits = theme->property("fontSizePt").toInt();

    // Put back to the size asked for, which is what the rows overflow at, so
    // the panel starts walking down again.
    theme->setProperty("fontSizePt", asked);
    bool walking = false;
    clock.restart();
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        // The overflow shows itself a layout later, not in this frame, and it
        // is the overflow that asks for the walk.
        if (theme->property("fontSizePt").toInt() < asked) {
            walking = true;
            break;
        }
    }
    QVERIFY2(walking, "the walk never started again");

    // And the answer lands in the middle of it, a size that is known to fit.
    QVERIFY(QMetaObject::invokeMethod(panel, "takeTheSizeFound",
                                      Q_ARG(QVariant, QVariant(fits))));

    clock.restart();
    settled = false;
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        settled = cameToRest(panel, theme);
        if (settled) {
            break;
        }
    }
    QVERIFY2(settled, "the fitting never came to rest after the late answer");
    QCOMPARE(theme->property("fontSizePt").toInt(), fits);
}

QTEST_MAIN(TestPanelLayout)

#include "test_panel_layout.moc"
