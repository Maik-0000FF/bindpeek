// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Seats.h"

namespace bindpeek::watch {

Seats::Seat &Seats::seatFor(const std::string &name) {
    for (Seat &seat : m_seats) {
        if (seat.name == name) {
            return seat;
        }
    }
    m_seats.push_back(Seat{name, Modifiers{}, {}, false});
    return m_seats.back();
}

const Seats::Seat *Seats::known(const std::string &name) const {
    for (const Seat &seat : m_seats) {
        if (seat.name == name) {
            return &seat;
        }
    }
    return nullptr;
}

void Seats::press(const std::string &seat, int device, int code) {
    seatFor(seat).state.press(device, code);
}

void Seats::release(const std::string &seat, int device, int code) {
    seatFor(seat).state.release(device, code);
}

void Seats::forget(const std::string &seat, int device) {
    seatFor(seat).state.forget(device);
}

void Seats::reconcile(const std::string &seat, int device,
                      const std::vector<int> &down) {
    seatFor(seat).state.reconcile(device, down);
}

void Seats::takeKey(const std::string &seat) { seatFor(seat).keyTaken = true; }

std::size_t Seats::count() const { return m_seats.size(); }

const std::string &Seats::nameAt(std::size_t at) const {
    return m_seats[at].name;
}

bool Seats::hasNews(std::size_t at) const {
    const Seat &seat = m_seats[at];
    return seat.keyTaken || seat.state.held() != seat.sent;
}

Report Seats::report(std::size_t at) const {
    return m_seats[at].state.report(m_seats[at].keyTaken);
}

Report Seats::snapshot(const std::string &seat) const {
    if (const Seat *found = known(seat)) {
        return found->state.report(false);
    }
    // Made from a state that holds nothing rather than filled in here, so the
    // shape of the record stays written down in one place.
    const Modifiers nothing;
    return nothing.report(false);
}

void Seats::settle() {
    for (Seat &seat : m_seats) {
        seat.sent = seat.state.held();
        seat.keyTaken = false;
    }
}

} // namespace bindpeek::watch
