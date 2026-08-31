// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures where the panel is put, for every position it can be given.
//
// This is the part that goes wrong on somebody else's monitor rather than on
// this one: whoever runs the program uses one position, and the other four are
// never looked at by hand. Swapping the two distances, or leaving a spanned
// axis anchored on one side only, shows as a panel in the wrong place under a
// compositor nobody here is running.
//
// No surface and no compositor: the rule takes the settings and hands back the
// anchors and margins as a value, and only applyPlacement writes them into a
// window.

#include "Appearance.h"
#include "LayerPlacement.h"
#include "Settings.h"

#include <LayerShellQt/Window>

#include <QMargins>
#include <QTemporaryDir>
#include <QTest>

#include <initializer_list>

using namespace bindpeek;

using Anchor = LayerShellQt::Window::Anchor;
using Anchors = LayerShellQt::Window::Anchors;

namespace {

// Two distances that cannot be mistaken for one another, so a rule that swaps
// them fails on the numbers rather than passing by coincidence. Both are
// non-zero, which is what makes the centre case say something: there they are
// set and have to come back as nothing.
const int kGap = 40;
const int kInset = 7;

// One anchor at a time, because the enum carries no operator for combining its
// values: written with a bar they produce an int the flags refuse.
Anchors anchorsOf(std::initializer_list<Anchor> edges) {
    Anchors anchors;
    for (const Anchor edge : edges) {
        anchors.setFlag(edge);
    }
    return anchors;
}

// The anchors as words, so a failure says which edges were set rather than
// which number the flags add up to.
QString describe(Anchors anchors) {
    if (anchors == Anchors()) {
        return QStringLiteral("nothing");
    }
    QStringList edges;
    if (anchors.testFlag(Anchor::AnchorLeft)) {
        edges << QStringLiteral("left");
    }
    if (anchors.testFlag(Anchor::AnchorRight)) {
        edges << QStringLiteral("right");
    }
    if (anchors.testFlag(Anchor::AnchorTop)) {
        edges << QStringLiteral("top");
    }
    if (anchors.testFlag(Anchor::AnchorBottom)) {
        edges << QStringLiteral("bottom");
    }
    return edges.join(QLatin1Char('+'));
}

// The spanned axes as words, for the same reason as above.
QString spanned(bool acrossTheWidth, bool downTheHeight) {
    if (acrossTheWidth && downTheHeight) {
        return QStringLiteral("both axes");
    }
    if (acrossTheWidth) {
        return QStringLiteral("the width");
    }
    if (downTheHeight) {
        return QStringLiteral("the height");
    }
    return QStringLiteral("neither axis");
}

// What each position is worth, written out rather than worked out: a table
// that asked spansHorizontally() for the spanning edges would agree with the
// rule under test by construction and measure nothing.
//
// The words are the ones the settings file writes. Every one of them that
// Settings offers has to appear here, and nothing else may, which is what
// everyPositionIsInTheTable() holds the two lists to: a position added to the
// settings later cannot arrive untested.
struct Case {
    const char *position;
    Anchors anchors;
    QMargins margins;
};

const QList<Case> &cases() {
    static const QList<Case> table = {
        // No edge, so neither distance applies and the compositor puts the
        // surface in the middle.
        {"center", Anchors(), QMargins()},
        // A side takes the full height: the edge it hangs from, and top and
        // bottom together to be stretched between them.
        {"left",
         anchorsOf(
             {Anchor::AnchorLeft, Anchor::AnchorTop, Anchor::AnchorBottom}),
         QMargins(kGap, kInset, 0, kInset)},
        {"right",
         anchorsOf(
             {Anchor::AnchorRight, Anchor::AnchorTop, Anchor::AnchorBottom}),
         QMargins(0, kInset, kGap, kInset)},
        // Top and bottom take the full width, so the spanned axis is the
        // other one and the two distances change places with it.
        {"top",
         anchorsOf(
             {Anchor::AnchorTop, Anchor::AnchorLeft, Anchor::AnchorRight}),
         QMargins(kInset, kGap, kInset, 0)},
        {"bottom",
         anchorsOf(
             {Anchor::AnchorBottom, Anchor::AnchorLeft, Anchor::AnchorRight}),
         QMargins(kInset, 0, kInset, kGap)},
    };
    return table;
}

} // namespace

class TestLayerPlacement : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void placement_data();
    void placement();
    void everyPositionIsInTheTable();
    void spannedAxisIsAnchoredOnBothSides();
    void marginsOnlyWhereThereIsAnEdge();
    void reserveIsWhatTheMarginsAddUpTo();
    void applyingToNoWindowIsHarmless();

private:
    Settings settingsFor(const QString &position) const;

    // Settings with no path reads the configuration of whoever runs the test,
    // and every answer here would then depend on the machine. A path into this
    // directory is handed in instead, and the file is never created: what comes
    // back is the defaults, with the three values this rule reads set by hand.
    QTemporaryDir m_dir;
};

// Without a usable directory filePath() hands back an empty string, and
// Settings built from one reads the real configuration of whoever runs the
// test. So the whole class stops here rather than every case measuring that
// machine's settings.
void TestLayerPlacement::initTestCase() { QVERIFY(m_dir.isValid()); }

Settings TestLayerPlacement::settingsFor(const QString &position) const {
    Settings settings(m_dir.filePath(QStringLiteral("bindpeek.conf")));
    settings.setPosition(Settings::positionFromName(position));
    settings.setMarginPx(kGap);
    settings.setEdgeInsetPx(kInset);
    return settings;
}

void TestLayerPlacement::placement_data() {
    QTest::addColumn<QString>("position");
    QTest::addColumn<Anchors>("anchors");
    QTest::addColumn<QMargins>("margins");

    for (const Case &entry : cases()) {
        QTest::newRow(entry.position) << QString::fromLatin1(entry.position)
                                      << entry.anchors << entry.margins;
    }
}

void TestLayerPlacement::placement() {
    QFETCH(QString, position);
    QFETCH(Anchors, anchors);
    QFETCH(QMargins, margins);

    const Placement placement = placementFor(settingsFor(position));

    QVERIFY2(placement.anchors == anchors,
             qPrintable(QStringLiteral("position %1 hangs from %2, expected %3")
                            .arg(position, describe(placement.anchors),
                                 describe(anchors))));
    QCOMPARE(placement.margins, margins);
}

// The table above and the positions the settings validate against, held to
// each other in both directions. A position the settings gained since would
// otherwise be placed by a rule nothing measures, and a row left behind by a
// position that went away would measure a word the program no longer reads.
void TestLayerPlacement::everyPositionIsInTheTable() {
    QStringList tabled;
    tabled.reserve(cases().size());
    for (const Case &entry : cases()) {
        tabled << QString::fromLatin1(entry.position);
    }
    QStringList known = Settings::knownPositions();

    QVERIFY(!known.isEmpty());
    tabled.sort();
    known.sort();
    QCOMPARE(tabled, known);
}

// A surface is stretched along an axis by being anchored to both of its ends,
// and the panel then leaves its extent along that axis to the compositor.
// Anchored to one end only it is not stretched at all, which is the same
// mistake seen from the other side: the panel sizes itself and sits somewhere
// along an edge it was meant to fill.
void TestLayerPlacement::spannedAxisIsAnchoredOnBothSides() {
    for (const QString &name : Settings::knownPositions()) {
        const Settings settings = settingsFor(name);
        const Anchors anchors = placementFor(settings).anchors;
        const Settings::Position position = settings.position();

        const bool acrossTheWidth = anchors.testFlag(Anchor::AnchorLeft) &&
                                    anchors.testFlag(Anchor::AnchorRight);
        const bool downTheHeight = anchors.testFlag(Anchor::AnchorTop) &&
                                   anchors.testFlag(Anchor::AnchorBottom);

        QVERIFY2(acrossTheWidth == spansHorizontally(position) &&
                     downTheHeight == spansVertically(position),
                 qPrintable(QStringLiteral(
                                "position %1 hangs from %2, which spans %3, "
                                "but the rule says it spans %4")
                                .arg(name, describe(anchors),
                                     spanned(acrossTheWidth, downTheHeight),
                                     spanned(spansHorizontally(position),
                                             spansVertically(position)))));
    }
}

// A distance is kept from an edge the surface is anchored to. Where there is
// no such edge there is nothing to keep it from, and a margin written anyway
// would say the panel stands somewhere it does not.
void TestLayerPlacement::marginsOnlyWhereThereIsAnEdge() {
    for (const QString &name : Settings::knownPositions()) {
        const Settings settings = settingsFor(name);
        const QMargins margins = placementFor(settings).margins;

        QVERIFY2(margins.isNull() == !settings.anchoredToEdge(),
                 qPrintable(QStringLiteral("position %1 keeps %2, %3, %4, %5 "
                                           "while %6 anchored to an edge")
                                .arg(name)
                                .arg(margins.left())
                                .arg(margins.top())
                                .arg(margins.right())
                                .arg(margins.bottom())
                                .arg(settings.anchoredToEdge()
                                         ? QStringLiteral("being")
                                         : QStringLiteral("not being"))));
    }
}

// The same distances, counted rather than placed. What the panel may grow to
// is worked out from the reserve, and the reserve is the two margins on each
// axis added together: nothing else keeps that pair honest, and a placement
// that gained a margin on a second side would leave the panel measured against
// a screen box wider than the room it actually has.
void TestLayerPlacement::reserveIsWhatTheMarginsAddUpTo() {
    for (const QString &name : Settings::knownPositions()) {
        const Settings settings = settingsFor(name);
        const QMargins margins = placementFor(settings).margins;
        const QSize reserve = reservedPixels(settings.position(), kGap, kInset);

        QCOMPARE(reserve, QSize(margins.left() + margins.right(),
                                margins.top() + margins.bottom()));
    }
}

// The guard in applyPlacement, which is reached whenever the window turns out
// not to be a layer-shell surface.
void TestLayerPlacement::applyingToNoWindowIsHarmless() {
    applyPlacement(nullptr, settingsFor(QStringLiteral("left")));
}

QTEST_APPLESS_MAIN(TestLayerPlacement)
#include "test_layer_placement.moc"
