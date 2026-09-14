// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the state the keyboard watch keeps: which modifiers count as held
// across several keyboards at once, in what order they are reported, and what
// the record on the wire looks like.
//
// And it measures the door: who is let in, who is turned away, who is dropped
// again, and who hears which seat. That half runs over a real socket, made
// here the way the unit makes it, because the thing worth measuring about it
// is the behaviour of accept, poll and close rather than a calculation. What
// cannot be made here is the answer logind gives: there is no session at a
// seat in a test run, so the two questions asked at the door are handed in and
// each case answers them itself.
//
// No device is opened and none is needed. That is the point of the split: the
// part of the service worth measuring is handed key codes and hands back a
// record, and the part that touches descriptors holds no state to measure.
//
// One thing is deliberately not measured here. Which seat a keyboard belongs
// to, and what the service does while udev has not said, is decided where the
// descriptors are: it needs a real device node and a udev database in a state
// that 21 attempts failed to produce. It is reasoned in the comments there
// rather than pretended to be covered by a case here.

#include "Modifiers.h"
#include "Protocol.h"
#include "Seats.h"
#include "Server.h"
#include "WatchClient.h"

#include <linux/input-event-codes.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

using namespace bindpeek::watch;

namespace {

// Two keyboards, told apart the way the service tells them apart.
constexpr int kBoard = 1;
constexpr int kOther = 2;

// The second seat, the one somebody has been attached to. The first is the
// service's own kDefaultSeat, taken from there rather than written again.
constexpr char kOtherSeat[] = "seat1";

// A key that is not a modifier, for the cases that ask what happens to one.
constexpr int kPlainKey = KEY_T;

std::vector<std::uint8_t> reported(const Report &record) {
    return std::vector<std::uint8_t>(record.held, record.held + record.count);
}

// Where a seat sits in the list, because the caller walks that list by number
// and a seat takes its place there the first time it is named.
std::size_t placeOf(const Seats &seats, const char *name) {
    for (std::size_t at = 0; at < seats.count(); ++at) {
        if (seats.nameAt(at) == name) {
            return at;
        }
    }
    return seats.count();
}

// Two people, told apart the way the service tells them apart. The numbers say
// nothing and are not meant to: no account is looked up, because the door is
// answered here rather than by the machine.
constexpr uid_t kSomebody = 4200;
constexpr uid_t kSomebodyElse = 4300;

// What the door answers while a case runs. A door is a pair of plain function
// pointers and a pointer has nowhere to keep anything, so the answers live
// here and the two functions below read them.
//
// Set by each case before it knocks. Cleared by every case that starts a
// bench, so nothing carries over from the one before.
struct Answers {
    // Whether the kernel can name the peer at all. False is a descriptor that
    // is no connected socket, which is the one way the real question fails.
    bool namesThePeer = true;
    // Who the next connection turns out to belong to.
    uid_t peer = kSomebody;
    // Where each of them is sitting. Somebody who is in nobody's seat is not
    // at this machine, and that is what an absent entry means.
    std::map<uid_t, std::string> seats;
};

Answers g_answers;

bool answerWhoIs(int /*fd*/, uid_t *uid) {
    if (!g_answers.namesThePeer) {
        return false;
    }
    *uid = g_answers.peer;
    return true;
}

bool answerWhereIs(uid_t uid, std::string *seat) {
    const auto found = g_answers.seats.find(uid);
    if (found == g_answers.seats.end()) {
        return false;
    }
    *seat = found->second;
    return true;
}

Door answeredHere() { return Door{answerWhoIs, answerWhereIs}; }

// How many connections a socket may hold waiting to be accepted. Above the
// limit the service keeps, so that the case which walks past that limit is
// measuring the service and not the backlog.
constexpr int kBacklog = static_cast<int>(kMaxClientsPerUser) + 4;

// The descriptors the service polls before the ones the server asks for: its
// signals and its two timers. Written here so that every case reads the poll
// answers back at an offset, which is where the arithmetic in dispatch goes
// wrong if it goes wrong at all.
constexpr std::size_t kAhead = 3;

// A service with a socket of its own. The service manager is not here to pass
// one, so it is made the way the unit makes it and handed over.
//
// Everything it opens is given back when it goes: the connections it knocked
// with, and the listening socket, which the server deliberately does not close
// because in the real run it belongs to the service manager.
class Bench {
public:
    explicit Bench(Answers answers) {
        g_answers = std::move(answers);
        m_ok = m_dir.isValid() && stand();
    }

    ~Bench() {
        for (const int fd : m_knocks) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        if (m_listen >= 0) {
            ::close(m_listen);
        }
    }

    Bench(const Bench &) = delete;
    Bench &operator=(const Bench &) = delete;

    bool ok() const { return m_ok; }
    Server &server() { return m_server; }

    // A connection from outside, as a panel makes one. The descriptor stays
    // ours: whether it was let in or shut is what most of the cases read.
    int knock() {
        const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return -1;
        }
        sockaddr_un at{};
        at.sun_family = AF_UNIX;
        std::memcpy(at.sun_path, m_path.c_str(), m_path.size());
        if (::connect(fd, reinterpret_cast<sockaddr *>(&at), sizeof at) < 0) {
            ::close(fd);
            return -1;
        }
        m_knocks.push_back(fd);
        return fd;
    }

    // One turn of the loop the service runs, with the answers read back where
    // they were asked for.
    void turn() {
        std::vector<pollfd> fds(kAhead, pollfd{-1, 0, 0});
        m_server.appendPollFds(fds);
        // No waiting. A connection made on this machine is in the queue by the
        // time connect has returned, and a descriptor that was closed has hung
        // up by the time close has.
        ::poll(fds.data(), fds.size(), 0);
        m_server.dispatch(fds, kAhead);
    }

private:
    bool stand() {
        m_path = m_dir.filePath("watch").toStdString();
        if (m_path.size() >= sizeof(sockaddr_un::sun_path)) {
            return false;
        }
        m_listen = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (m_listen < 0) {
            return false;
        }
        sockaddr_un at{};
        at.sun_family = AF_UNIX;
        std::memcpy(at.sun_path, m_path.c_str(), m_path.size());
        if (::bind(m_listen, reinterpret_cast<sockaddr *>(&at), sizeof at) <
            0) {
            return false;
        }
        if (::listen(m_listen, kBacklog) < 0) {
            return false;
        }
        return m_server.adopt(m_listen);
    }

    QTemporaryDir m_dir;
    std::string m_path;
    int m_listen = -1;
    std::vector<int> m_knocks;
    Server m_server{answeredHere()};
    bool m_ok = false;
};

// What a connection was told, if anything. The three answers a case asks about
// are a record, nothing yet, and the door shut.
enum class Told { Nothing, Record, Shut };

Told heard(int fd, Report *record) {
    // A connection that was never made was told nothing, which is the truth
    // about it and the case has already failed on the knock that returned it.
    // Written out because recv is a system call and the checker is right that
    // it deserves a descriptor.
    if (fd < 0) {
        return Told::Nothing;
    }
    const ssize_t got = ::recv(fd, record, sizeof *record, MSG_DONTWAIT);
    if (got == 0) {
        return Told::Shut;
    }
    if (got < 0) {
        return Told::Nothing;
    }
    return got == static_cast<ssize_t>(sizeof *record) ? Told::Record
                                                       : Told::Nothing;
}

Told heard(int fd) {
    Report ignored{};
    return heard(fd, &ignored);
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
    void a_release_that_changes_nothing_moves_nothing();
    void a_lost_keyboard_takes_its_keys();
    void reconcile_corrects_in_both_directions();
    void reconcile_leaves_the_order_of_survivors();
    void the_record_says_what_is_held();
    void all_four_fit_in_the_record();
    void a_good_record_is_read();
    void the_taken_flag_is_read();
    void a_record_of_the_wrong_size_is_refused();
    void another_version_is_named_as_such();
    void a_count_past_the_end_is_refused();
    void an_unknown_modifier_refuses_the_record();
    void one_seat_hears_nothing_of_the_other();
    void a_taken_key_stays_at_its_seat();
    void news_is_what_has_not_gone_out();
    void a_seat_nobody_opened_holds_nothing();
    void a_lost_keyboard_takes_its_keys_from_its_seat();

    void the_kernel_names_the_peer_of_a_connection();
    void a_descriptor_that_is_no_connection_names_nobody();
    void a_socket_of_another_kind_is_not_adopted();
    void a_peer_the_door_cannot_name_is_shut_out();
    void somebody_at_no_seat_is_shut_out();
    void somebody_at_a_seat_waits_before_they_are_let_in();
    void what_they_are_let_in_on_is_their_own_seat();
    void one_seat_hears_nothing_of_the_other_over_the_socket();
    void the_fifth_connection_of_one_person_is_shut_out();
    void the_limit_is_counted_per_person();
    void leaving_the_seat_ends_the_connection();
    void losing_the_session_ends_the_connection();
    void staying_put_keeps_the_connection();
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
    state.press(kBoard, kPlainKey);
    QVERIFY(state.held().empty());
}

void TestWatch::order_is_the_order_they_went_down() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTSHIFT);
    state.press(kBoard, KEY_LEFTMETA);

    const std::vector<std::uint8_t> expected{kShift, kSuper};
    QCOMPARE(state.held(), expected);
}

void TestWatch::a_repeat_is_not_a_second_press() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);
    // The same key again, which is what auto-repeat and a state replay both
    // look like from here.
    state.press(kBoard, KEY_LEFTMETA);

    const std::vector<std::uint8_t> once{kSuper};
    QCOMPARE(state.held(), once);

    // And one release is enough to lift it, or a held SUPER would need as many
    // releases as it sent repeats. That is what the repeat not being taken
    // comes to: the key stands here once, so one release takes it away.
    state.release(kBoard, KEY_LEFTMETA);
    QVERIFY(state.held().empty());
}

// Both shift keys on one keyboard. Letting go of one while the other is still
// down does not lift SHIFT.
void TestWatch::one_modifier_two_keys() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTSHIFT);
    state.press(kBoard, KEY_RIGHTSHIFT);
    state.release(kBoard, KEY_LEFTSHIFT);

    const std::vector<std::uint8_t> still{kShift};
    QCOMPARE(state.held(), still);

    state.release(kBoard, KEY_RIGHTSHIFT);
    QVERIFY(state.held().empty());
}

// The same physical key on two keyboards, which is the case a laptop with an
// external keyboard runs into. One release must not clear what the other is
// still holding.
void TestWatch::one_modifier_two_keyboards() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kOther, KEY_LEFTMETA);
    state.release(kBoard, KEY_LEFTMETA);

    const std::vector<std::uint8_t> still{kSuper};
    QCOMPARE(state.held(), still);

    state.release(kOther, KEY_LEFTMETA);
    QVERIFY(state.held().empty());
}

// Two keyboards, and letting go of one SUPER while the other still holds it.
// The modifier does not lift, and it must not move either: the panel sorts by
// what is held, and shuffling that under somebody's fingers is a change they
// did not make.
void TestWatch::a_release_that_changes_nothing_moves_nothing() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kBoard, KEY_LEFTSHIFT);
    state.press(kOther, KEY_LEFTMETA);

    const std::vector<std::uint8_t> before{kSuper, kShift};
    QCOMPARE(state.held(), before);

    state.release(kBoard, KEY_LEFTMETA);
    QCOMPARE(state.held(), before);
}

void TestWatch::a_lost_keyboard_takes_its_keys() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kOther, KEY_LEFTALT);

    // Unplugged while a key was down. That key can never be released, so it
    // goes with the keyboard.
    state.forget(kOther);

    const std::vector<std::uint8_t> left{kSuper};
    QCOMPARE(state.held(), left);

    // And a keyboard that held nothing changes nothing, which is the same
    // keyboard asked a second time.
    state.forget(kOther);
    QCOMPARE(state.held(), left);
}

void TestWatch::reconcile_corrects_in_both_directions() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTMETA);

    // A key-up that never arrived: the device says nothing is down.
    state.reconcile(kBoard, {});
    QVERIFY(state.held().empty());

    // A key-down that never arrived: the device says something is, and nobody
    // ever saw it go there.
    state.reconcile(kBoard, {KEY_LEFTALT});
    const std::vector<std::uint8_t> found{kAlt};
    QCOMPARE(state.held(), found);

    // Told the same thing twice, nothing changes.
    state.reconcile(kBoard, {KEY_LEFTALT});
    QCOMPARE(state.held(), found);
}

// A correction that only takes something away must not reorder what stays: a
// modifier that survives it was pressed when it was pressed, and moving it
// would rearrange the panel for nothing.
void TestWatch::reconcile_leaves_the_order_of_survivors() {
    Modifiers state;
    state.press(kBoard, KEY_LEFTSHIFT);
    state.press(kBoard, KEY_LEFTMETA);
    state.press(kBoard, KEY_LEFTCTRL);

    state.reconcile(kBoard, {KEY_LEFTSHIFT, KEY_LEFTCTRL});

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

// --- The record as the panel reads it ---------------------------------------
//
// The other half of the same eight bytes. Measured here rather than in a test
// of its own because a record is one agreement, and an agreement is best read
// with both ends of it on the same page.

namespace {

// A record as the service would send it.
Report sent(std::initializer_list<std::uint8_t> held, bool keyTaken) {
    Report out{};
    out.version = kProtocolVersion;
    out.flags = keyTaken ? kFlagKeyTaken : static_cast<std::uint8_t>(0);
    out.count = static_cast<std::uint8_t>(held.size());
    std::size_t at = 0;
    for (const std::uint8_t id : held) {
        out.held[at++] = id;
    }
    return out;
}

} // namespace

void TestWatch::a_good_record_is_read() {
    const Report record = sent({kSuper, kShift}, false);
    const bindpeek::Heard heard = bindpeek::hear(&record, sizeof record);

    QVERIFY(heard.understood);
    QVERIFY(!heard.wrongVersion);
    QVERIFY(!heard.keyTaken);
    QCOMPARE(heard.held,
             QStringList({QStringLiteral("SUPER"), QStringLiteral("SHIFT")}));
}

void TestWatch::the_taken_flag_is_read() {
    const Report record = sent({kSuper}, true);
    const bindpeek::Heard heard = bindpeek::hear(&record, sizeof record);

    QVERIFY(heard.understood);
    QVERIFY(heard.keyTaken);
}

// A datagram of the wrong length is somebody else's idea of the record, not a
// short read: the socket keeps each one whole.
void TestWatch::a_record_of_the_wrong_size_is_refused() {
    const Report record = sent({kSuper}, false);
    QVERIFY(!bindpeek::hear(&record, sizeof record - 1).understood);
    QVERIFY(!bindpeek::hear(&record, sizeof record + 1).understood);
    QVERIFY(!bindpeek::hear(&record, 0).understood);
}

// A service that was replaced under a running panel. Told apart from a
// malformed record, because the panel has something to say about it.
void TestWatch::another_version_is_named_as_such() {
    Report record = sent({kSuper}, false);
    record.version = kProtocolVersion + 1;
    const bindpeek::Heard heard = bindpeek::hear(&record, sizeof record);

    QVERIFY(!heard.understood);
    QVERIFY(heard.wrongVersion);
}

// A count larger than the field it counts. What is being kept out is the
// reading walking past the four entries, so the byte just past them is given a
// value that would read perfectly well: without the guard the answer comes
// back understood, with five names in it. A record refused for holding an
// unknown number would not have said anything about the guard.
void TestWatch::a_count_past_the_end_is_refused() {
    Report record = sent({kSuper, kCtrl, kAlt, kShift}, false);
    record.count = static_cast<std::uint8_t>(kMaxHeld + 1);
    record.reserved = kSuper;
    const bindpeek::Heard heard = bindpeek::hear(&record, sizeof record);

    QVERIFY(!heard.understood);
    QVERIFY(!heard.wrongVersion);
}

// A number with no name behind it, in a record whose version says there should
// be one. Refused whole rather than shown with a gap in it.
void TestWatch::an_unknown_modifier_refuses_the_record() {
    const Report record = sent({kSuper, 200}, false);
    const bindpeek::Heard heard = bindpeek::hear(&record, sizeof record);

    QVERIFY(!heard.understood);
    QVERIFY(heard.held.isEmpty());
}

// Two people at one machine. What is held at one seat is not in the record of
// the other, which is the whole of what the split is for.
void TestWatch::one_seat_hears_nothing_of_the_other() {
    Seats seats;
    seats.press(kDefaultSeat, kBoard, KEY_LEFTMETA);
    seats.press(kOtherSeat, kOther, KEY_LEFTSHIFT);

    const std::vector<std::uint8_t> here{kSuper};
    const std::vector<std::uint8_t> there{kShift};
    QCOMPARE(reported(seats.snapshot(kDefaultSeat)), here);
    QCOMPARE(reported(seats.snapshot(kOtherSeat)), there);
}

// The bare fact that some other key went down is the one thing the service
// says about a key it does not name, and it is said at one seat only. Told at
// both, the panel of the person who pressed nothing would go off the screen
// under their hands.
void TestWatch::a_taken_key_stays_at_its_seat() {
    Seats seats;
    seats.press(kDefaultSeat, kBoard, KEY_LEFTMETA);
    seats.press(kOtherSeat, kOther, KEY_LEFTMETA);
    seats.settle();

    seats.takeKey(kOtherSeat);
    const std::size_t here = placeOf(seats, kDefaultSeat);
    const std::size_t there = placeOf(seats, kOtherSeat);

    QVERIFY(!seats.hasNews(here));
    QVERIFY(seats.hasNews(there));
    QCOMPARE(seats.report(here).flags & kFlagKeyTaken, 0);
    QCOMPARE(seats.report(there).flags & kFlagKeyTaken, kFlagKeyTaken);

    // A fact about the round and not about the state, so the next round starts
    // without it.
    seats.settle();
    QVERIFY(!seats.hasNews(there));
}

// What has to go out is worked out by comparing the record with the one that
// last went out, rather than threaded back from the reading. So a press that
// changes nothing has nothing to say, which is what auto-repeat is.
void TestWatch::news_is_what_has_not_gone_out() {
    Seats seats;
    seats.press(kDefaultSeat, kBoard, KEY_LEFTMETA);
    const std::size_t here = placeOf(seats, kDefaultSeat);
    QVERIFY(seats.hasNews(here));

    seats.settle();
    QVERIFY(!seats.hasNews(here));

    seats.press(kDefaultSeat, kBoard, KEY_LEFTSHIFT);
    QVERIFY(seats.hasNews(here));

    seats.settle();
    seats.press(kDefaultSeat, kBoard, KEY_LEFTSHIFT);
    QVERIFY(!seats.hasNews(here));
}

// Somebody let in at a seat no keyboard was ever opened for. They are told
// that nothing is held, which is true of that seat, rather than refused or
// told about somebody else's.
void TestWatch::a_seat_nobody_opened_holds_nothing() {
    Seats seats;
    const Report record = seats.snapshot(kOtherSeat);

    QCOMPARE(record.version, kProtocolVersion);
    QCOMPARE(record.count, std::uint8_t{0});
    QCOMPARE(record.flags, std::uint8_t{0});

    // And asking did not bring the seat into being. Only a keyboard does that.
    QCOMPARE(seats.count(), std::size_t{0});
}

// A keyboard that was unplugged takes what it was holding with it, and takes
// it out of its own seat alone.
void TestWatch::a_lost_keyboard_takes_its_keys_from_its_seat() {
    Seats seats;
    seats.press(kDefaultSeat, kBoard, KEY_LEFTMETA);
    seats.press(kOtherSeat, kOther, KEY_LEFTMETA);

    seats.forget(kOtherSeat, kOther);

    QVERIFY(reported(seats.snapshot(kOtherSeat)).empty());
    const std::vector<std::uint8_t> still{kSuper};
    QCOMPARE(reported(seats.snapshot(kDefaultSeat)), still);
}

// The half of the door that needs nothing arranged: the kernel attaches the
// credentials when the connection is made, with no session and no seat
// anywhere in it, so the real function is measured here rather than replaced.
void TestWatch::the_kernel_names_the_peer_of_a_connection() {
    int pair[2] = {-1, -1};
    QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);

    uid_t uid = 0;
    QVERIFY(peerUid(pair[0], &uid));
    QCOMPARE(uid, ::getuid());

    ::close(pair[0]);
    ::close(pair[1]);
}

// And when it cannot be asked, nobody is named. The answer has to be a no
// rather than a leftover: the value it would have written is the one the
// caller goes on to look up a seat for.
void TestWatch::a_descriptor_that_is_no_connection_names_nobody() {
    int ends[2] = {-1, -1};
    QVERIFY(::pipe(ends) == 0);

    uid_t uid = kSomebody;
    QVERIFY(!peerUid(ends[0], &uid));
    QCOMPARE(uid, kSomebody);

    ::close(ends[0]);
    ::close(ends[1]);
}

// A listening socket of the wrong kind is refused, and refused whole: nothing
// is polled and nothing is accepted on a start that failed. The service speaks
// in records the kernel draws the boundary around, and a stream socket would
// hand it a byte count instead.
void TestWatch::a_socket_of_another_kind_is_not_adopted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath("stream").toStdString();
    QVERIFY(path.size() < sizeof(sockaddr_un::sun_path));

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    // QFAIL rather than QVERIFY, because what follows is a system call that
    // wants a descriptor: this way the case leaves before it, in the reading
    // of the checker as well as in the running.
    if (fd < 0) {
        QFAIL("cannot make a socket of the kind this case turns away");
    }
    sockaddr_un at{};
    at.sun_family = AF_UNIX;
    std::memcpy(at.sun_path, path.c_str(), path.size());
    QVERIFY(::bind(fd, reinterpret_cast<sockaddr *>(&at), sizeof at) == 0);
    QVERIFY(::listen(fd, kBacklog) == 0);

    Server server{answeredHere()};
    QVERIFY(!server.adopt(fd));

    std::vector<pollfd> fds;
    server.appendPollFds(fds);
    QCOMPARE(fds.size(), std::size_t{1});
    QCOMPARE(fds[0].fd, -1);

    ::close(fd);
}

// The socket is open to everyone, so anybody local can reach the accept. A
// connection the kernel will not name is shut there and then, before a seat is
// looked up for a number nobody stands behind.
void TestWatch::a_peer_the_door_cannot_name_is_shut_out() {
    Bench bench(Answers{false, kSomebody, {{kSomebody, kDefaultSeat}}});
    QVERIFY(bench.ok());

    const int panel = bench.knock();
    QVERIFY(panel >= 0);
    bench.turn();

    QCOMPARE(bench.server().waiting(), std::size_t{0});
    QCOMPARE(bench.server().clients(), std::size_t{0});
    QVERIFY(heard(panel) == Told::Shut);
}

// Somebody who is not sitting at this machine. A system account with a service
// running, an account that has logged out, somebody at the other end of an
// ssh connection: none of them is at a seat, and the records say when
// somebody at a keyboard held a modifier and when they took a key.
void TestWatch::somebody_at_no_seat_is_shut_out() {
    Bench bench(Answers{true, kSomebody, {}});
    QVERIFY(bench.ok());

    const int panel = bench.knock();
    QVERIFY(panel >= 0);
    bench.turn();

    QCOMPARE(bench.server().waiting(), std::size_t{0});
    QCOMPARE(bench.server().clients(), std::size_t{0});
    QVERIFY(heard(panel) == Told::Shut);
}

// Passing the check is not being let in. Between the two the caller opens the
// keyboards, which it does for somebody who has passed and for nobody else, so
// there is a round in which the connection stands and has been told nothing.
void TestWatch::somebody_at_a_seat_waits_before_they_are_let_in() {
    Bench bench(Answers{true, kSomebody, {{kSomebody, kDefaultSeat}}});
    QVERIFY(bench.ok());

    const int panel = bench.knock();
    QVERIFY(panel >= 0);
    bench.turn();

    QCOMPARE(bench.server().waiting(), std::size_t{1});
    QCOMPARE(bench.server().clients(), std::size_t{0});
    QVERIFY(heard(panel) == Told::Nothing);

    const Seats state;
    bench.server().admit(state);

    QCOMPARE(bench.server().waiting(), std::size_t{0});
    QCOMPARE(bench.server().clients(), std::size_t{1});
    QVERIFY(heard(panel) == Told::Record);
}

// What the record on the way in says is the state of their own seat. A panel
// starts with a modifier already down often enough to be the ordinary case,
// and the modifier held at the other seat is not theirs to hear about.
void TestWatch::what_they_are_let_in_on_is_their_own_seat() {
    Bench bench(Answers{true, kSomebodyElse, {{kSomebodyElse, kOtherSeat}}});
    QVERIFY(bench.ok());

    Seats state;
    state.press(kDefaultSeat, kBoard, KEY_LEFTCTRL);
    state.press(kOtherSeat, kOther, KEY_LEFTMETA);

    const int panel = bench.knock();
    QVERIFY(panel >= 0);
    bench.turn();
    bench.server().admit(state);

    Report record{};
    QVERIFY(heard(panel, &record) == Told::Record);
    const std::vector<std::uint8_t> theirs{kSuper};
    QCOMPARE(reported(record), theirs);
}

// The seat is carried on the connection, and the broadcast goes by it. Measured
// over the socket rather than in the state alone: the state keeps the two
// apart, and this is the line where that separation is either used or lost.
void TestWatch::one_seat_hears_nothing_of_the_other_over_the_socket() {
    Bench bench(
        Answers{true,
                kSomebody,
                {{kSomebody, kDefaultSeat}, {kSomebodyElse, kOtherSeat}}});
    QVERIFY(bench.ok());

    const int here = bench.knock();
    QVERIFY(here >= 0);
    bench.turn();

    g_answers.peer = kSomebodyElse;
    const int there = bench.knock();
    QVERIFY(there >= 0);
    bench.turn();

    Seats state;
    bench.server().admit(state);
    QCOMPARE(bench.server().clients(), std::size_t{2});
    // The record each was let in on, taken out of the way so that what is read
    // below is the broadcast and nothing else.
    QVERIFY(heard(here) == Told::Record);
    QVERIFY(heard(there) == Told::Record);

    state.press(kDefaultSeat, kBoard, KEY_LEFTMETA);
    bench.server().broadcast(kDefaultSeat, state.snapshot(kDefaultSeat));

    Report record{};
    QVERIFY(heard(here, &record) == Told::Record);
    const std::vector<std::uint8_t> held{kSuper};
    QCOMPARE(reported(record), held);
    QVERIFY(heard(there) == Told::Nothing);
}

// A panel is one connection, and a second is the moment during a restart when
// the old one has not let go yet. Past that the socket is being used for
// something else, and the room is not there to be taken.
//
// All of them inside one round, which is the case the counting is written for:
// none of them is in the list proper yet, and a limit that looked only there
// would let every one of them through.
void TestWatch::the_fifth_connection_of_one_person_is_shut_out() {
    Bench bench(Answers{true, kSomebody, {{kSomebody, kDefaultSeat}}});
    QVERIFY(bench.ok());

    std::vector<int> panels;
    for (std::size_t at = 0; at <= kMaxClientsPerUser; ++at) {
        const int panel = bench.knock();
        QVERIFY(panel >= 0);
        panels.push_back(panel);
    }
    bench.turn();

    QCOMPARE(bench.server().waiting(), kMaxClientsPerUser);
    QVERIFY(heard(panels.back()) == Told::Shut);
    for (std::size_t at = 0; at < kMaxClientsPerUser; ++at) {
        QVERIFY(heard(panels[at]) == Told::Nothing);
    }

    const Seats state;
    bench.server().admit(state);
    QCOMPARE(bench.server().clients(), kMaxClientsPerUser);
}

// And it is counted per person, so that one account cannot use up the room
// another one needs. The same socket, the same moment, somebody else: they are
// let in on their own count.
void TestWatch::the_limit_is_counted_per_person() {
    Bench bench(
        Answers{true,
                kSomebody,
                {{kSomebody, kDefaultSeat}, {kSomebodyElse, kDefaultSeat}}});
    QVERIFY(bench.ok());

    for (std::size_t at = 0; at < kMaxClientsPerUser; ++at) {
        QVERIFY(bench.knock() >= 0);
    }
    bench.turn();
    const Seats state;
    bench.server().admit(state);
    QCOMPARE(bench.server().clients(), kMaxClientsPerUser);

    const int overTheLimit = bench.knock();
    QVERIFY(overTheLimit >= 0);
    bench.turn();
    QVERIFY(heard(overTheLimit) == Told::Shut);
    QCOMPARE(bench.server().waiting(), std::size_t{0});

    g_answers.peer = kSomebodyElse;
    const int other = bench.knock();
    QVERIFY(other >= 0);
    bench.turn();
    QCOMPARE(bench.server().waiting(), std::size_t{1});

    bench.server().admit(state);
    QCOMPARE(bench.server().clients(), kMaxClientsPerUser + 1);
    QVERIFY(heard(other) == Told::Record);
}

// The seat is read again while they stay, because a session can be switched
// away from long after it connected. Somebody who is now at another seat is
// not to be told what is held at this one.
void TestWatch::leaving_the_seat_ends_the_connection() {
    Bench bench(Answers{true, kSomebody, {{kSomebody, kDefaultSeat}}});
    QVERIFY(bench.ok());

    const int panel = bench.knock();
    QVERIFY(panel >= 0);
    bench.turn();
    const Seats state;
    bench.server().admit(state);
    QVERIFY(heard(panel) == Told::Record);

    g_answers.seats[kSomebody] = kOtherSeat;
    bench.server().dropStrangers();

    QCOMPARE(bench.server().clients(), std::size_t{0});
    QVERIFY(heard(panel) == Told::Shut);
}

// The same for a session that has gone: logged out, or switched away from with
// nothing of theirs in the foreground. The records would otherwise keep going
// to a screen nobody is looking at.
void TestWatch::losing_the_session_ends_the_connection() {
    Bench bench(Answers{true, kSomebody, {{kSomebody, kDefaultSeat}}});
    QVERIFY(bench.ok());

    const int panel = bench.knock();
    QVERIFY(panel >= 0);
    bench.turn();
    const Seats state;
    bench.server().admit(state);
    QVERIFY(heard(panel) == Told::Record);

    g_answers.seats.clear();
    bench.server().dropStrangers();

    QCOMPARE(bench.server().clients(), std::size_t{0});
    QVERIFY(heard(panel) == Told::Shut);
}

// And somebody who has not moved stays, told nothing by the checking itself.
// The check runs on every correction beat, so a check that cost a connection
// or a record would cost it a second and a half later as well.
void TestWatch::staying_put_keeps_the_connection() {
    Bench bench(Answers{true, kSomebody, {{kSomebody, kDefaultSeat}}});
    QVERIFY(bench.ok());

    const int panel = bench.knock();
    QVERIFY(panel >= 0);
    bench.turn();
    const Seats state;
    bench.server().admit(state);
    QVERIFY(heard(panel) == Told::Record);

    bench.server().dropStrangers();

    QCOMPARE(bench.server().clients(), std::size_t{1});
    QVERIFY(heard(panel) == Told::Nothing);
}

QTEST_APPLESS_MAIN(TestWatch)
#include "test_watch.moc"
