// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TrayWait.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <memory>
#include <utility>

namespace bindpeek {

void waitForTray(QObject *owner, std::function<bool()> isPresent,
                 std::function<void()> whenMissing, int waitMs, int pollMs) {
    auto *poll = new QTimer(owner);
    poll->setInterval(pollMs);

    // Running from here rather than from the first tick: what is being waited
    // out is the session coming up, and that began now, not one interval from
    // now. Held by the callback so it lives exactly as long as the wait does.
    auto deadline = std::make_shared<QElapsedTimer>();
    deadline->start();

    QObject::connect(poll, &QTimer::timeout, poll,
                     [poll, deadline, isPresent = std::move(isPresent),
                      whenMissing = std::move(whenMissing), waitMs]() {
                         const bool present = isPresent();
                         if (!present && deadline->elapsed() < waitMs) {
                             return;
                         }
                         // Stopped before the answer is acted on, so nothing is
                         // asked a second time while whatever follows is still
                         // happening.
                         poll->stop();
                         poll->deleteLater();
                         if (!present) {
                             whenMissing();
                         }
                     });
    poll->start();
}

} // namespace bindpeek
