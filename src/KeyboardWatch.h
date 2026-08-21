// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Source.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstddef>
#include <vector>

class QFileSystemWatcher;
class QSocketNotifier;

struct libevdev;

namespace bindpeek {

// Reads the keyboards under /dev/input and reports which modifiers are held.
//
// Why below the compositor: the panel has to know that SUPER is down while
// SUPER still belongs to the compositor. A layer surface with keyboard focus
// would learn the same thing but take the keys away, and then the very
// shortcut the user is looking at would no longer fire. Reading the event
// devices watches the same keys without consuming them.
//
// Nothing is grabbed here. EVIOCGRAB would make this an interceptor and break
// every other consumer of the keyboard; the whole point is to stay passive.
class KeyboardWatch : public QObject {
    Q_OBJECT

public:
    explicit KeyboardWatch(QObject *parent = nullptr);
    ~KeyboardWatch() override;

    // Opens every device that looks like a keyboard and starts watching for
    // new ones. Returns how many were opened; zero means there is nothing to
    // watch, in practice a missing membership in the "input" group.
    int start();

    // The modifiers held right now, canonical and in the order they went down.
    QStringList held() const;

signals:
    // The held modifiers changed, in what is down or in what was pressed
    // first.
    void heldChanged(const QStringList &held);

    // A key that is not a modifier went down. The user has just pressed the
    // shortcut they were looking for, so the panel has done its job.
    void shortcutTaken();

private:
    struct Device {
        QString path;
        int fd = -1;
        libevdev *dev = nullptr;
        QSocketNotifier *notifier = nullptr;
        // Set once the device stops delivering: unplugged, or a Bluetooth
        // keyboard that went away. Kept in the list rather than erased so the
        // indices the notifiers were created with stay valid.
        bool dead = false;
    };

    // Opens one device if it looks like a keyboard and is not already open.
    // Returns true when it was added.
    bool openDevice(const QString &path);

    void readFrom(std::size_t index);
    void retireDevice(Device &device);
    void handleKey(const Device &device, int code, int value);

    // Asks the devices what is really pressed and corrects the held set.
    void resync();

    // A device node appeared: try to open it. The kernel creates the node
    // before it grants permissions, so a first attempt can fail and is
    // retried on the following change.
    void onInputDirChanged();

    std::vector<Device> m_devices;
    HeldModifiers m_held;

    // Watches /dev/input so a keyboard plugged in later is picked up. Without
    // it a Bluetooth keyboard that connects after start would stay invisible
    // for the whole session.
    QFileSystemWatcher *m_dirWatcher = nullptr;

    // Safety net against a lost release. A key-up can go missing when a device
    // is taken away, when the kernel drops events under load, or when a
    // compositor grabs the keyboard mid-press. The panel would then stand
    // there with a modifier that is long since up, and nothing in the event
    // stream would ever correct it.
    QTimer m_resync;

    // Set in the destructor: retireDevice must not emit anything then.
    bool m_shuttingDown = false;
};

} // namespace bindpeek
