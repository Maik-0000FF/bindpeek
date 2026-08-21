// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the rule that stands in for a tray that never appears: that it
// waits before standing in, that it stays away when the tray turns up late,
// and that it gives up on the clock rather than after so many looks. No tray
// is involved and none is needed; the question is handed in.

#include "TrayWait.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTest>
#include <QThread>

using namespace bindpeek;

namespace {

// Short enough for a test run, long enough to hold several looks.
constexpr int kWaitMs = 200;
constexpr int kPollMs = 20;

// How long the loop is left running past an answer before the test takes it
// that nothing more is coming.
constexpr int kSettleMs = 200;

// What every answer costs in the last case. Larger than the interval, so the
// looks cannot keep up with the ticks and Qt folds the ones that pile up into
// one, which is what a session that is still coming up does to the loop.
constexpr int kAnswerCostMs = 40;

// The looks a count would have insisted on before giving up.
constexpr int kLooksIfCounted = kWaitMs / kPollMs;

} // namespace

class TestTrayWait : public QObject {
    Q_OBJECT

private slots:
    void standsInWhenNoTrayComes();
    void staysAwayWhenTheTrayArrivesLate();
    void givesUpOnTheClockNotOnTheLooks();
};

void TestTrayWait::standsInWhenNoTrayComes() {
    QObject owner;
    int stoodIn = 0;
    qint64 stoodInAt = 0;
    QElapsedTimer elapsed;
    elapsed.start();

    waitForTray(
        &owner, []() { return false; },
        [&stoodIn, &stoodInAt, &elapsed]() {
            ++stoodIn;
            stoodInAt = elapsed.elapsed();
        },
        kWaitMs, kPollMs);

    QTRY_COMPARE_WITH_TIMEOUT(stoodIn, 1, kWaitMs + kSettleMs);
    // Not one look too early: the whole point is that the first answer is not
    // the one acted on.
    QVERIFY2(stoodInAt >= kWaitMs,
             qPrintable(QStringLiteral("stood in after %1 ms").arg(stoodInAt)));
    // And once only. The timer is done as soon as it has answered.
    QTest::qWait(kSettleMs);
    QCOMPARE(stoodIn, 1);
}

void TestTrayWait::staysAwayWhenTheTrayArrivesLate() {
    QObject owner;
    int stoodIn = 0;
    int looks = 0;
    // Two looks say no, the third says the tray is there, which is the session
    // whose bar came up a moment after the program did.
    constexpr int kLooksUntilTheTray = 3;

    waitForTray(
        &owner, [&looks]() { return ++looks >= kLooksUntilTheTray; },
        [&stoodIn]() { ++stoodIn; }, kWaitMs, kPollMs);

    QTest::qWait(kWaitMs + kSettleMs);
    QCOMPARE(stoodIn, 0);
    // Nothing is asked again once a tray has been seen.
    QCOMPARE(looks, kLooksUntilTheTray);
}

void TestTrayWait::givesUpOnTheClockNotOnTheLooks() {
    QObject owner;
    int stoodIn = 0;
    int looks = 0;

    waitForTray(
        &owner,
        [&looks]() {
            ++looks;
            QThread::msleep(kAnswerCostMs);
            return false;
        },
        [&stoodIn]() { ++stoodIn; }, kWaitMs, kPollMs);

    QTRY_COMPARE_WITH_TIMEOUT(stoodIn, 1, kWaitMs * 4);
    // Counting looks would have insisted on kLooksIfCounted of them and given
    // up long after the time it promised. Against the clock the wait is over
    // after a handful, and a loop under more load than this one only makes it
    // fewer.
    QVERIFY2(looks < kLooksIfCounted,
             qPrintable(QStringLiteral("gave up after %1 looks, a count would "
                                       "have taken %2")
                            .arg(looks)
                            .arg(kLooksIfCounted)));
}

QTEST_GUILESS_MAIN(TestTrayWait)
#include "test_tray_wait.moc"
