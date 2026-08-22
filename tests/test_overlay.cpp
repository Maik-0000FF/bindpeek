// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the second consumer of groupBinds, the one that feeds the panel.
//
// The grouping itself has its own tests next to the function. This one exists
// because the duplicate heading that made the function necessary lived in the
// consumers, and a consumer can be put back the way it was without any of
// those tests noticing. The text output is covered by running the program;
// this covers the other side.
//
// No window, no session and no device: the keyboard watch is only constructed
// and asked to deliver one signal, which is the way the controller learns
// which modifiers are held.

#include "KeyboardWatch.h"
#include "OverlayController.h"
#include "Source.h"

#include <QTest>
#include <QVariantMap>

#include <memory>
#include <utility>

using namespace bindpeek;

namespace {

// The keys the controller puts into a group entry. Spelled out because a test
// of a contract has to state the contract; QML reads the same names.
constexpr char kName[] = "name";
constexpr char kEntries[] = "entries";
constexpr char kShortcut[] = "shortcut";
constexpr char kDeeper[] = "deeper";

// How long the controller waits before showing the panel. The value decides
// nothing here, and zero would do as well, but not because the two behave
// alike: measured, a single-shot timer set to zero fires on the first pass of
// the event loop and one set to a millisecond does not. What carries the test
// is that the visibility below is waited for with QTRY_VERIFY, which keeps
// turning the loop and sleeping until either answer arrives. Asserted outright
// instead, the panel is never up when it is looked at, and the check that it
// goes away again then holds no matter what the controller does.
constexpr int kShowDelayMs = 1;

// How much longer than the delay a test waits before deciding that a panel
// which should not appear has indeed not appeared. Generous, because the only
// thing it costs is milliseconds and the only thing it buys is not calling a
// slow machine a bug.
constexpr int kWaitFactor = 200;

// Hands out a fixed list, which is all the controller wants from a source.
class StubSource : public Source {
public:
    explicit StubSource(QList<Bind> binds) : m_binds(std::move(binds)) {}

    QString name() const override { return QStringLiteral("stub"); }

    QList<Bind> read(QString *error) const override {
        if (error != nullptr) {
            error->clear();
        }
        return m_binds;
    }

private:
    QList<Bind> m_binds;
};

Bind bind(const QStringList &modifiers, const QString &key,
          const QString &group) {
    return Bind{modifiers, key, key.toLower(), group};
}

// The heading names in the order the controller produced them.
QStringList headings(const QVariantList &groups) {
    QStringList names;
    for (const QVariant &group : groups) {
        names.append(group.toMap().value(QLatin1String(kName)).toString());
    }
    return names;
}

// The shortcuts listed under one heading.
QStringList shortcuts(const QVariantList &groups, const QString &heading) {
    QStringList keys;
    for (const QVariant &group : groups) {
        const QVariantMap block = group.toMap();
        if (block.value(QLatin1String(kName)).toString() != heading) {
            continue;
        }
        const QVariantList entries =
            block.value(QLatin1String(kEntries)).toList();
        for (const QVariant &entry : entries) {
            keys.append(
                entry.toMap().value(QLatin1String(kShortcut)).toString());
        }
    }
    return keys;
}

} // namespace

class TestOverlay : public QObject {
    Q_OBJECT

private slots:
    void headingsAppearOnceEachAndInSourceOrder();
    void onlyTheHeldCombinationIsShown();
    void sortingTheRowsLeavesTheHeadingsWhereTheyAre();
    void arrangingByModifierHeadsEachCombinationNearestFirst();
    void deeperEntriesAreMarkedAsSuch();
    void showingOnlyWhatFiresLeavesTheRestOut();
    void aLoneShiftIsNotAQuestion();
    void releasingEveryModifierEmptiesTheView();
    void theSurfaceNeverGoesDownAtTheMomentItIsNeeded();
};

void TestOverlay::headingsAppearOnceEachAndInSourceOrder() {
    const QStringList held = {QStringLiteral("SUPER")};
    // The shape a Hyprland reply has: a submap sits wherever the configuration
    // put it, so one heading is interrupted by another and picked up again.
    auto source = std::make_unique<StubSource>(QList<Bind>{
        bind(held, QStringLiteral("A"), QStringLiteral("Other")),
        bind(held, QStringLiteral("B"), QStringLiteral("resize")),
        bind(held, QStringLiteral("C"), QStringLiteral("Other")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());
    emit watch.heldChanged(held);

    const QVariantList groups = controller.groups();
    // Two headings, not three: comparing each entry with the one before it
    // would open "Other" a second time after the interruption.
    QCOMPARE(headings(groups),
             QStringList({QStringLiteral("Other"), QStringLiteral("resize")}));
    // And both of its entries are under the one heading, in their own order.
    QCOMPARE(shortcuts(groups, QStringLiteral("Other")),
             QStringList({QStringLiteral("A"), QStringLiteral("C")}));
}

// The other arrangement, where the headings the session gave are set aside and
// each combination heads a group of its own.
//
// Two things are asked of it at once, because they are the point of it: the
// groups run nearest first, so what fires on the next key comes before what
// wants one more modifier, and a combination is one group however many
// headings its shortcuts arrived under.
void TestOverlay::arrangingByModifierHeadsEachCombinationNearestFirst() {
    const QStringList held = {QStringLiteral("SUPER")};
    const QStringList superShift = {QStringLiteral("SUPER"),
                                    QStringLiteral("SHIFT")};
    const QStringList superAlt = {QStringLiteral("SUPER"),
                                  QStringLiteral("ALT")};
    const QStringList superCtrl = {QStringLiteral("SUPER"),
                                   QStringLiteral("CTRL")};
    const QStringList superCtrlShift = {QStringLiteral("SUPER"),
                                        QStringLiteral("CTRL"),
                                        QStringLiteral("SHIFT")};
    // Written deepest first and spread over two headings, so the order below
    // can only come from the arrangement and not from the source.
    //
    // ALT and CTRL are in it because they are the pair the two orders disagree
    // about: the display order names CTRL first, the alphabet names ALT first.
    auto source = std::make_unique<StubSource>(QList<Bind>{
        bind(superCtrlShift, QStringLiteral("C"), QStringLiteral("Other")),
        bind(superShift, QStringLiteral("B"), QStringLiteral("resize")),
        bind(superAlt, QStringLiteral("E"), QStringLiteral("Other")),
        bind(held, QStringLiteral("A"), QStringLiteral("Other")),
        bind(superCtrl, QStringLiteral("F"), QStringLiteral("resize")),
        bind(superShift, QStringLiteral("D"), QStringLiteral("Other")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    controller.setArrangesByModifier(true);
    QVERIFY(controller.reload());
    emit watch.heldChanged(held);

    const QVariantList groups = controller.groups();
    QCOMPARE(
        headings(groups),
        QStringList({QStringLiteral("SUPER"), QStringLiteral("SUPER+CTRL"),
                     QStringLiteral("SUPER+ALT"), QStringLiteral("SUPER+SHIFT"),
                     QStringLiteral("SUPER+CTRL+SHIFT")}));
    // Both of the SUPER+SHIFT shortcuts under the one heading, in the order
    // the source listed them, although they arrived under two of its own.
    QCOMPARE(
        shortcuts(groups, QStringLiteral("SUPER+SHIFT")),
        QStringList({QStringLiteral("SHIFT+B"), QStringLiteral("SHIFT+D")}));
}

void TestOverlay::onlyTheHeldCombinationIsShown() {
    const QStringList superOnly = {QStringLiteral("SUPER")};
    const QStringList superShift = {QStringLiteral("SUPER"),
                                    QStringLiteral("SHIFT")};
    const QStringList ctrlOnly = {QStringLiteral("CTRL")};
    auto source = std::make_unique<StubSource>(QList<Bind>{
        // Listed with the deeper one first, so the order below can only come
        // from the sorting and not from the source.
        bind(superShift, QStringLiteral("B"), QStringLiteral("Other")),
        bind(superOnly, QStringLiteral("A"), QStringLiteral("Other")),
        bind(ctrlOnly, QStringLiteral("C"), QStringLiteral("Other")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());

    // Holding SUPER answers with everything SUPER can still reach. What fires
    // on the next key comes first; the one that wants SHIFT as well says so in
    // front of its key. The CTRL shortcut is not reachable from here at all.
    emit watch.heldChanged(superOnly);
    QCOMPARE(shortcuts(controller.groups(), QStringLiteral("Other")),
             QStringList({QStringLiteral("A"), QStringLiteral("SHIFT+B")}));

    // Pressing SHIFT narrows that same list rather than replacing it: what is
    // left is what fires now, and it is no longer marked as further off.
    emit watch.heldChanged(superShift);
    QCOMPARE(shortcuts(controller.groups(), QStringLiteral("Other")),
             QStringList({QStringLiteral("B")}));

    // The order the hand took is not part of the shortcut.
    emit watch.heldChanged(
        QStringList({QStringLiteral("SHIFT"), QStringLiteral("SUPER")}));
    QCOMPARE(shortcuts(controller.groups(), QStringLiteral("Other")),
             QStringList({QStringLiteral("B")}));
}

// Sorting the rows must not sort the headings.
//
// The rows of a heading are ordered by how far off they are, but a heading
// itself sits where the configuration put it. Sorting the whole list before
// grouping does both at once, and then a section written second moves in front
// of the first merely because one of its shortcuts fires sooner.
void TestOverlay::sortingTheRowsLeavesTheHeadingsWhereTheyAre() {
    const QStringList superOnly = {QStringLiteral("SUPER")};
    const QStringList superShift = {QStringLiteral("SUPER"),
                                    QStringLiteral("SHIFT")};
    auto source = std::make_unique<StubSource>(QList<Bind>{
        // The first section's only shortcut needs a second modifier, the
        // second section's fires at once.
        bind(superShift, QStringLiteral("E"), QStringLiteral("launch")),
        bind(superOnly, QStringLiteral("H"), QStringLiteral("window")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());
    emit watch.heldChanged(superOnly);

    QCOMPARE(headings(controller.groups()),
             QStringList({QStringLiteral("launch"), QStringLiteral("window")}));
}

// The panel dims what is not about to fire, and it can only do that if it is
// told which rows those are.
void TestOverlay::deeperEntriesAreMarkedAsSuch() {
    const QStringList superOnly = {QStringLiteral("SUPER")};
    const QStringList superShift = {QStringLiteral("SUPER"),
                                    QStringLiteral("SHIFT")};
    auto source = std::make_unique<StubSource>(QList<Bind>{
        bind(superOnly, QStringLiteral("A"), QStringLiteral("Other")),
        bind(superShift, QStringLiteral("B"), QStringLiteral("Other")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());
    emit watch.heldChanged(superOnly);

    const QVariantList entries = controller.groups()
                                     .first()
                                     .toMap()
                                     .value(QLatin1String(kEntries))
                                     .toList();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).toMap().value(QLatin1String(kDeeper)).toBool(),
             false);
    QCOMPARE(entries.at(1).toMap().value(QLatin1String(kDeeper)).toBool(),
             true);
}

// The panel can be asked for what fires this instant and nothing else. What a
// further key would reach is then left out of the list rather than marked, and
// the counts of it stay, because a footer is the one thing that still says
// there is more.
void TestOverlay::showingOnlyWhatFiresLeavesTheRestOut() {
    const QStringList superOnly = {QStringLiteral("SUPER")};
    const QStringList superShift = {QStringLiteral("SUPER"),
                                    QStringLiteral("SHIFT")};
    auto source = std::make_unique<StubSource>(QList<Bind>{
        bind(superOnly, QStringLiteral("A"), QStringLiteral("Other")),
        bind(superShift, QStringLiteral("B"), QStringLiteral("Other")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());

    controller.setShowsDeeper(false);
    emit watch.heldChanged(superOnly);
    QCOMPARE(shortcuts(controller.groups(), QStringLiteral("Other")),
             QStringList({QStringLiteral("A")}));
    // Counted all the same: SHIFT still leads somewhere, and that is what the
    // footer is for.
    QCOMPARE(controller.continuations().size(), 1);

    controller.setShowsDeeper(true);
    QCOMPARE(shortcuts(controller.groups(), QStringLiteral("Other")),
             QStringList({QStringLiteral("A"), QStringLiteral("SHIFT+B")}));
}

// Shift alone is held while typing capitals, not while looking a shortcut up.
void TestOverlay::aLoneShiftIsNotAQuestion() {
    const QStringList shiftOnly = {QStringLiteral("SHIFT")};
    const QStringList superShift = {QStringLiteral("SHIFT"),
                                    QStringLiteral("SUPER")};
    auto source = std::make_unique<StubSource>(QList<Bind>{
        bind(shiftOnly, QStringLiteral("A"), QStringLiteral("Other")),
        bind({QStringLiteral("SUPER"), QStringLiteral("SHIFT")},
             QStringLiteral("B"), QStringLiteral("Other")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());
    controller.setIgnoreLoneShift(true);

    emit watch.heldChanged(shiftOnly);
    // Waited for rather than asserted outright: the panel comes up on a timer,
    // so a check taken at once would hold whatever the controller does.
    QTest::qWait(kShowDelayMs * kWaitFactor);
    QVERIFY(!controller.isPanelVisible());

    // With another modifier it is a combination like any other.
    emit watch.heldChanged(superShift);
    QTRY_VERIFY(controller.isPanelVisible());

    // And with the option off, Shift on its own is answered again.
    emit watch.heldChanged({});
    controller.setIgnoreLoneShift(false);
    emit watch.heldChanged(shiftOnly);
    QTRY_VERIFY(controller.isPanelVisible());
}

// The window is up while the panel is either being waited for or being shown,
// and each of those is read the moment it is announced. If the end of the wait
// is announced before the panel is raised, there is an instant in which
// neither holds, and the window takes its surface down and builds a new one at
// exactly the moment it is wanted. What that costs is not a flicker: the
// surface is what carries the knowledge of which output it is on, which is the
// only thing the wait was spent on.
//
// Measured the way the window reads it, by asking for both at every
// announcement of either.
void TestOverlay::theSurfaceNeverGoesDownAtTheMomentItIsNeeded() {
    auto source = std::make_unique<StubSource>(QList<Bind>{
        bind({QStringLiteral("SUPER")}, QStringLiteral("T"),
             QStringLiteral("Programs")),
    });

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());

    QList<bool> wanted;
    const auto record = [&controller, &wanted]() {
        wanted.append(controller.isPanelPending() ||
                      controller.isPanelVisible());
    };
    QObject::connect(&controller, &OverlayController::panelPendingChanged,
                     &controller, record);
    QObject::connect(&controller, &OverlayController::panelVisibleChanged,
                     &controller, record);

    emit watch.heldChanged({QStringLiteral("SUPER")});
    QTRY_VERIFY(controller.isPanelVisible());

    QVERIFY2(!wanted.isEmpty(), "the window was never told anything");
    // Every answer from the first one on is yes: the wait begins, the panel
    // follows, and nothing in between says otherwise.
    for (const bool up : std::as_const(wanted)) {
        QVERIFY2(up, "the window was told to take the surface down between "
                     "the end of the wait and the panel being raised");
    }
}

void TestOverlay::releasingEveryModifierEmptiesTheView() {
    const QStringList held = {QStringLiteral("SUPER")};
    auto source = std::make_unique<StubSource>(
        QList<Bind>{bind(held, QStringLiteral("A"), QStringLiteral("Other"))});

    KeyboardWatch watch;
    OverlayController controller(std::move(source), &watch, kShowDelayMs);
    QVERIFY(controller.reload());

    emit watch.heldChanged(held);
    QCOMPARE(controller.groups().size(), 1);
    // Waited for rather than asserted outright: the panel comes up on a timer,
    // and until it has actually come up the check below says nothing at all.
    QTRY_VERIFY(controller.isPanelVisible());

    emit watch.heldChanged({});
    QVERIFY(controller.groups().isEmpty());
    QVERIFY(!controller.isPanelVisible());
}

// GUILESS rather than APPLESS: the controller owns timers, and a QTimer wants
// an application to belong to even when nothing waits for it to fire.
QTEST_GUILESS_MAIN(TestOverlay)
#include "test_overlay.moc"
