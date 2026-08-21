// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Source.h"

namespace bindpeek {

// KDE keeps its shortcuts in kglobalshortcutsrc, the file kglobalaccel itself
// reads. It covers KWin and every application, and it already carries the
// descriptions as translated plain text.
class SourceKde : public Source {
public:
    // Without a path the default location is used (see defaultPath()).
    explicit SourceKde(QString path = QString());

    QString name() const override;
    QList<Bind> read(QString *error) const override;

    // ~/.config/kglobalshortcutsrc
    static QString defaultPath();

private:
    QString m_path;
};

} // namespace bindpeek
