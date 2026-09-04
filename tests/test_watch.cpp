// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the state the keyboard watch keeps: which modifiers count as held
// across several keyboards at once, in what order they are reported, and what
// the record on the wire looks like.
//
// No device is opened and none is needed. That is the point of the split: the
// part of the service worth measuring is handed key codes and hands back a
// record, and the part that touches descriptors holds no state to measure.

#include "Modifiers.h"
#include "Protocol.h"

#include <linux/input-event-codes.h>

#include <QObject>
#include <QTest>

#include <vector>

using namespace bindpeek::watch;

namespace {

// Two keyboards, told apart the way the service tells them apart.
constexpr int kBoard = 1;
constexpr int kOther = 2;

// A key that is not a modifier, for the cases that ask what happens to one.
constexpr int kPlainKey = KEY_T;

std::vector<std::uint8_t> reported(const Report &record) {
    return std::vector<std::uint8_t>(record.held, record.held + record.count);
}

} // namespace

class TestWatch : public QObject {
    Q_OBJECT

private slots:
    void table_answers_both_sides();
    void plain_keys_are_not_modifiers();
    void order_is_the_order_they_went_down();
    void a_repeat_is_not_a_second_press();
    void one_modifier_two_keys();
    void one_modifier_two_keyboards();
    void a_lost_keyboard_takes_its_keys();
    void reconcile_corrects_in_both_directions();
    void reconcile_leaves_the_order_of_survivors();
    void the_record_says_what_is_held();
    void all_four_fit_in_the_record();
};

// Every code the table answers to maps to a modifier, and both sides of the
// keyboard map to the same one: nobody thinks of left and right SUPER as two
// different modifiers.
void TestWatch::table_answers_both_sides() {
    QCOMPARE(Modifiers::idOf(KEY_LEFTMETA), Modifiers::idOf(KEY_RIGHTMETA));
    QCOMPARE(Modifiers::idOf(KEY_LEFTCTRL), Modifiers::idOf(KEY_RIGHTCTRL));
    QCOMPARE(Modifiers::idOf(KEY_LEFTALT), Modifiers::idOf(KEY_RIGHTALT));
    QCOMPARE(Modifiers::idOf(KEY_LEFTSHIFT), Modifiers::idOf(KEY_RIGHTSHIFT));

    // The list a caller walks and the answers it gets are built from one
    // table, so nothing in the one can be missing from the other.
    QCOMPARE(Modifiers::codes().size(), std::size_t{8});
    for (const int code : Modifiers::codes()) {
        QVERIFY2(
            Modifiers::idOf(code) != kNoModifier,
            qPrintable(
                QStringLiteral("code %1 is listed but unanswered").arg(code)));
    }
}

void TestWatch::plain_keys_are_not_modifiers() {
    QCOMPARE(Modifiers::idOf(kPlainKey), kNoModifier);

    // And they leave no trace in the state. What the service says about them
    // is a single bit that lives in the moment, not here.
    Modifiers state;
    QVERIFY(!state.press(kBoard, kPlainKey));
    QVERIFY(state.held().empty());
}

void TestWatch::order_is_the_order_they_went_down() {
    Modifiers state;
    QVERIFY(state.press(kBoard, KEY_LEFTSHIFT));
    QVERIFY(state.press(kBoard, KEY_LEFTMETA));

    const std::vector<std::uint8_t> expected{kShift, kSuper};
    QCOMPARE(state.held(), expected);
}

void TestWatch::a_repeat_is_not_a_second_press() {
    Modifiers state;
    QVERIFY(state.press(kBoard, KEY_LEFTMETA));
    // The same key again, which is what auto-repeat and a state replay both
    // look like from here.
    QVERIFY(!state.press(kBoard, KEY_LEFTMETA));

    const std::vector<std::uint8_t> once{kSuper};
    QCOMPARE(state.held(), once);

    // And one release is enough to lift it, or a held SUPER would need as many
    // releases as it sent repeats.
    QVERIFY(state.release(kBoard, KEY_LEFTMETA));
    QVERIFY(state.held().empty());
}

// Both shift keys on one keyboard. Letting go of one while the other is still
// down does not lift SHIFT.
void TestWatch::one_modifier_two_keys() {
    Modifiers state;
    QVERIFY(state.press(kBoard, KEY_LEFTSHIFT));
    QVERIFY(!state.press(kBoard, KEY_RIGHTSHIFT));
    QVERIFY(!state.release(kBoard, KEY_LEFTSHIFT));

    const std::vector<std::uint8_t> still{kShift};
    QCOMPARE(state.held(), still);

    QVERIFY(state.release(kBoard, KEY_RIGHTSHIFT));
    QVERIFY(state.held().empty());
}

// The same physical key on two keyboards, which is the case a laptop with an
// external keyboard runs into. One release must not clear what the other is
// still holding.
void TestWatch::one_modifier_two_keyboards() {
    Modifiers state;
    QVERIFY(state.press(kBoard, KEY_LEFTMETA));
    QVERIFY(!state.press(kOther, KEY_LEFTMETA));
    QVERIFY(!state.release(kBoard, KEY_LEFTMETA));

    const std::vector<std::uint8_t> still{kSuper};
    QCOMPARE(state.held(), still);

    QVERIFY(state.release(kOther, KEY_LEFTMETA));
    QVERIFY(state.held().empty());
}

void TestWatch::a_lost_keyboard_takes_its_keys() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kOther, KEY_LEFTALT);

    // Unplugged while a key was down. That key can never be released, so it
    // goes with the keyboard.
    QVERIFY(state.forget(kOther));

    const std::vector<std::uint8_t> left{kSuper};
    QCOMPARE(state.held(), left);

    // And a keyboard that held nothing changes nothing.
    QVERIFY(!state.forget(kOther));
}

void TestWatch::reconcile_corrects_in_both_directions() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);

    // A key-up that never arrived: the device says nothing is down.
    QVERIFY(state.reconcile(kBoard, {}));
    QVERIFY(state.held().empty());

    // A key-down that never arrived: the device says something is, and nobody
    // ever saw it go there.
    QVERIFY(state.reconcile(kBoard, {KEY_LEFTALT}));
    const std::vector<std::uint8_t> found{kAlt};
    QCOMPARE(state.held(), found);

    // Told the same thing twice, nothing changed and nothing is sent.
    QVERIFY(!state.reconcile(kBoard, {KEY_LEFTALT}));
}

// A correction that only takes something away must not reorder what stays: a
// modifier that survives it was pressed when it was pressed, and moving it
// would rearrange the panel for nothing.
void TestWatch::reconcile_leaves_the_order_of_survivors() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTSHIFT);
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kBoard, KEY_LEFTCTRL);

    QVERIFY(state.reconcile(kBoard, {KEY_LEFTSHIFT, KEY_LEFTCTRL}));

    const std::vector<std::uint8_t> expected{kShift, kCtrl};
    QCOMPARE(state.held(), expected);
}

void TestWatch::the_record_says_what_is_held() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kBoard, KEY_LEFTSHIFT);

    const Report quiet = state.report(false);
    QCOMPARE(quiet.version, kProtocolVersion);
    QCOMPARE(quiet.flags, std::uint8_t{0});
    QCOMPARE(quiet.count, std::uint8_t{2});

    const std::vector<std::uint8_t> expected{kSuper, kShift};
    QCOMPARE(reported(quiet), expected);

    // The one thing said about any other key: that one went down, never which.
    const Report taken = state.report(true);
    QCOMPARE(taken.flags, kFlagKeyTaken);
    QCOMPARE(reported(taken), expected);
}

// The case that decided the shape of the record. With all four modifiers down
// a terminator would have had nowhere to go, so the record carries a count
// instead and the reader is bounded by it.
void TestWatch::all_four_fit_in_the_record() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kBoard, KEY_LEFTCTRL);
    state.press(kBoard, KEY_LEFTALT);
    state.press(kBoard, KEY_LEFTSHIFT);

    const Report record = state.report(false);
    QCOMPARE(record.count, static_cast<std::uint8_t>(kMaxHeld));

    const std::vector<std::uint8_t> expected{kSuper, kCtrl, kAlt, kShift};
    QCOMPARE(reported(record), expected);
}

QTEST_APPLESS_MAIN(TestWatch)
#include "test_watch.moc"
