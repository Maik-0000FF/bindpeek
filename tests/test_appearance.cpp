// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the rule the panel is bounded by.
//
// The panel is a layer surface, and which output the compositor puts it on is
// not something Qt is told in time, if at all. So it is measured against the
// smallest screen attached, which is the one answer that holds wherever it
// lands. Getting that wrong is not a cosmetic matter: a surface wider than its
// output is not cut off by it, the excess is drawn on whatever output sits
// next to it.
//
// No window and no session: the rule takes sizes rather than screens, and the
// sizes here include the ones a real session produces for a moment and nobody
// would think to try by hand.

#include "Appearance.h"

#include <QTest>

using namespace bindpeek;

// Settings is a plain class, so its enum carries no meta information of its
// own and the test framework cannot put it in a column without this.
Q_DECLARE_METATYPE(Settings::Position)

namespace {

// Stands in for the "no screen at all" answer. Any pair does, the test only
// has to tell it apart from a measured one.
const QSize kFallback(1234, 567);

// Two distances that cannot be mistaken for one another, so a test that swaps
// them fails on the numbers rather than passing by coincidence.
const int kMargin = 40;
const int kInset = 7;

} // namespace

class TestAppearance : public QObject {
    Q_OBJECT

private slots:
    void smallestScreenBox_data();
    void smallestScreenBox();
    void reservedPixels_data();
    void reservedPixels();
};

void TestAppearance::smallestScreenBox_data() {
    QTest::addColumn<QList<QSize>>("screens");
    QTest::addColumn<QSize>("expected");

    QTest::newRow("no screen at all") << QList<QSize>() << kFallback;
    QTest::newRow("one screen")
        << QList<QSize>{QSize(3840, 2160)} << QSize(3840, 2160);
    QTest::newRow("a large one and a portrait one")
        << QList<QSize>{QSize(3840, 2160), QSize(1200, 1920)}
        << QSize(1200, 1920);
    // Neither screen is the answer: each axis is bounded on its own, and only
    // that box fits inside both of them.
    QTest::newRow("landscape beside portrait")
        << QList<QSize>{QSize(1920, 1080), QSize(1080, 1920)}
        << QSize(1080, 1080);
    // An output that has appeared and not yet applied its mode. Counted, it
    // would empty the running minimum and let the next screen replace it,
    // which is how a panel ends up sized for the largest screen attached.
    QTest::newRow("one still coming up")
        << QList<QSize>{QSize(1920, 1080), QSize(0, 0), QSize(2560, 1440)}
        << QSize(1920, 1080);
    QTest::newRow("the one coming up is first")
        << QList<QSize>{QSize(0, 0), QSize(2560, 1440)} << QSize(2560, 1440);
    // A height of zero is as unusable as a size of zero, and the width beside
    // it is not to be believed either.
    QTest::newRow("half a size")
        << QList<QSize>{QSize(1920, 0), QSize(2560, 1440)} << QSize(2560, 1440);
    QTest::newRow("nothing measurable")
        << QList<QSize>{QSize(0, 0), QSize(0, 1080)} << kFallback;
}

void TestAppearance::smallestScreenBox() {
    QFETCH(QList<QSize>, screens);
    QFETCH(QSize, expected);

    QCOMPARE(bindpeek::smallestScreenBox(screens, kFallback), expected);
}

// The second rule the bound rests on: how much of each axis the placement
// keeps clear. Measured per position because the two distances do not apply
// alike. Along the edge a surface spans, the inset is kept at both ends and
// counts twice; across it the margin sits on the anchored side alone. Halving
// either of those hands the panel room it does not have, and a panel built
// for room it does not have is drawn onto the output next to it.
void TestAppearance::reservedPixels_data() {
    QTest::addColumn<Settings::Position>("position");
    QTest::addColumn<QSize>("expected");

    // Anchored to nothing, so neither distance is applied.
    QTest::newRow("centre") << Settings::Position::Center << QSize(0, 0);
    // Spans the height: the inset is kept above and below, the margin only on
    // the side the panel is anchored to.
    QTest::newRow("left") << Settings::Position::Left
                          << QSize(kMargin, kInset * 2);
    QTest::newRow("right") << Settings::Position::Right
                           << QSize(kMargin, kInset * 2);
    // Spans the width, so the pair is the other way round.
    QTest::newRow("top") << Settings::Position::Top
                         << QSize(kInset * 2, kMargin);
    QTest::newRow("bottom")
        << Settings::Position::Bottom << QSize(kInset * 2, kMargin);
}

void TestAppearance::reservedPixels() {
    QFETCH(Settings::Position, position);
    QFETCH(QSize, expected);

    QCOMPARE(bindpeek::reservedPixels(position, kMargin, kInset), expected);
}

QTEST_APPLESS_MAIN(TestAppearance)
#include "test_appearance.moc"
