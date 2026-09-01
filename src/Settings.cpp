// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QPair>
#include <QSaveFile>
#include <QSettings>
#include <QTextStream>

namespace bindpeek {
namespace {

// Key names, spelled once so the reader, the template and the editor cannot
// drift apart.
constexpr char kKeyShowDelay[] = "showDelayMs";
constexpr char kKeyOverlayEnabled[] = "overlayEnabled";
constexpr char kKeyDisclosure[] = "disclosure";
constexpr char kKeyArrangement[] = "arrangement";
constexpr char kKeyAlignment[] = "alignment";
constexpr char kKeyIgnoreLoneShift[] = "ignoreLoneShift";
constexpr char kKeyPosition[] = "position";
constexpr char kKeyMargin[] = "marginPx";
constexpr char kKeyEdgeInset[] = "edgeInsetPx";
constexpr char kKeyTheme[] = "theme";
constexpr char kKeyFollowSystem[] = "followSystemScheme";
constexpr char kKeyThemeLight[] = "themeLight";
constexpr char kKeyThemeDark[] = "themeDark";
constexpr char kKeyFontFamily[] = "fontFamily";
constexpr char kKeyFontSize[] = "fontSizePt";
constexpr char kKeyRadius[] = "cornerRadiusPx";
constexpr char kKeyBorder[] = "borderWidthPx";
constexpr char kKeyOpacity[] = "opacity";

// Defaults and accepted ranges. The bounds are not technical limits but
// guards against a typo that would make the panel look broken: a delay that
// never elapses, an opacity that hides it, a border thicker than the panel.
constexpr int kShowDelayDefault = 500;
constexpr int kShowDelayMin = 0;
constexpr int kShowDelayMax = 5000;
constexpr int kShowDelayStep = 10;

constexpr int kMarginDefault = 14;
constexpr int kMarginMin = 0;
constexpr int kMarginMax = 400;

constexpr int kFontSizeDefault = 14;
constexpr int kFontSizeMin = 6;
constexpr int kFontSizeMax = 48;

constexpr int kRadiusDefault = 14;
constexpr int kRadiusMin = 0;
constexpr int kRadiusMax = 64;

constexpr int kBorderDefault = 1;
constexpr int kBorderMin = 0;
constexpr int kBorderMax = 16;

constexpr double kOpacityDefault = 0.90;
// Zero is allowed: only the panel background fades, its text and border stay
// fully opaque, so the shortcuts remain readable with no plate behind them.
constexpr double kOpacityMin = 0.0;
constexpr double kOpacityMax = 1.0;
constexpr double kOpacityStep = 0.01;

// Off at a first start: the panel comes up in the house palette, which is
// what the program looks like. Following the desktop is a choice made in the
// editor, and the dark half is that same palette, so switching it on later
// costs nothing but the light half.
constexpr bool kFollowSystemDefault = false;

// The panel is the program: it comes up with the session unless it was
// switched off, and switching it off is the deliberate act.
constexpr bool kOverlayEnabledDefault = true;

// How far a panel put against an edge stops short of that edge's two ends.
//
// Not zero: a band that runs into both corners reads as part of the screen
// rather than as something laid on top of it, and the same distance that
// holds it off its edge holds it off the ends. Zero is still available and
// means the whole edge, corner to corner.
constexpr int kEdgeInsetDefault = 14;

constexpr char kThemeDefault[] = "bindpeek";
constexpr char kThemeLightDefault[] = "light";
constexpr char kThemeDarkDefault[] = "bindpeek";

// What a panel starts with: a block for each further combination, headed by
// the keys that reach it. It is the longest of the four answers and the one
// that says the most, which is what a cheat sheet is opened for; the shorter
// ones are there for anyone who finds it too much. The words themselves are in
// Settings.h, where everything that acts on them can reach them.
constexpr const char *kDisclosureDefault = disclosure::kSections;
constexpr const char *kArrangementDefault = arrangement::kSource;

// Where the content sits along the axis the panel spans: in the middle of it.
// The panel is looked at rather than read line by line, and the middle is
// where the eye goes without being sent.
constexpr const char *kAlignmentDefault = alignment::kCenter;

// On by default: Shift alone is held while typing capitals, many times a
// minute, and a panel that answers that is in the way rather than in help.
constexpr bool kIgnoreLoneShiftDefault = true;

constexpr char kFileName[] = "bindpeek.conf";

// Position names as they appear in the file.
constexpr char kPositionCenter[] = "center";
constexpr char kPositionLeft[] = "left";
constexpr char kPositionRight[] = "right";
constexpr char kPositionTop[] = "top";
constexpr char kPositionBottom[] = "bottom";

// Where a panel sits when nothing has been set: along the bottom edge, which
// is where a cheat sheet belongs. It is read while the hands stay on the
// keyboard and the eyes are on the window above it, so the bottom is the one
// edge that answers without covering what is being worked on.
//
// The word for it is not written down a second time. positionName() turns the
// value into it, so moving this line moves everything that follows from it,
// including what a fresh configuration file says.
constexpr Settings::Position kPositionDefault = Settings::Position::Bottom;

// Reads one value and validates it. Every setting goes through here, so the
// rules are written once: an absent key is silent, an unusable value falls
// back and says so.
class Reader {
public:
    Reader(const QSettings &settings, QStringList *warnings)
        : m_settings(settings), m_warnings(warnings) {}

    int integer(const char *key, int fallback, int low, int high) const {
        QString raw;
        if (!present(key, &raw)) {
            return fallback;
        }
        bool ok = false;
        const int value = raw.toInt(&ok);
        if (!ok) {
            notANumber(key, raw, QString::number(fallback));
            return fallback;
        }
        if (value < low || value > high) {
            outOfRange(key, QString::number(value), QString::number(low),
                       QString::number(high), QString::number(fallback));
            return fallback;
        }
        return value;
    }

    double number(const char *key, double fallback, double low,
                  double high) const {
        QString raw;
        if (!present(key, &raw)) {
            return fallback;
        }
        // toDouble always reads a point as the decimal separator, never a
        // comma. That is deliberate: the same file must mean the same thing
        // on every machine, whatever the locale.
        bool ok = false;
        const double value = raw.toDouble(&ok);
        if (!ok) {
            notANumber(key, raw, QString::number(fallback));
            return fallback;
        }
        if (value < low || value > high) {
            outOfRange(key, QString::number(value), QString::number(low),
                       QString::number(high), QString::number(fallback));
            return fallback;
        }
        return value;
    }

    bool boolean(const char *key, bool fallback) const {
        QString raw;
        if (!present(key, &raw)) {
            return fallback;
        }
        const QString lowered = raw.toLower();
        if (lowered == QLatin1String("true") || lowered == QLatin1String("1") ||
            lowered == QLatin1String("yes")) {
            return true;
        }
        if (lowered == QLatin1String("false") ||
            lowered == QLatin1String("0") || lowered == QLatin1String("no")) {
            return false;
        }
        m_warnings->append(
            QCoreApplication::translate(
                "Settings", "%1: \"%2\" is not true or false, using %3")
                .arg(QLatin1String(key), raw,
                     fallback ? QStringLiteral("true")
                              : QStringLiteral("false")));
        return fallback;
    }

    // One of a fixed set of words. Anything else falls back, so a typo can
    // never leave the overlay rendering something that does not exist.
    QString word(const char *key, const QString &fallback,
                 const QStringList &allowed) const {
        QString raw;
        if (!present(key, &raw)) {
            return fallback;
        }
        const QString lowered = raw.toLower();
        for (const QString &candidate : allowed) {
            if (candidate.toLower() == lowered) {
                return candidate;
            }
        }
        m_warnings->append(
            QCoreApplication::translate(
                "Settings", "%1: \"%2\" is unknown, using \"%3\". Known: %4")
                .arg(QLatin1String(key), raw, fallback,
                     allowed.join(QStringLiteral(", "))));
        return fallback;
    }

    // Free text, taken as written. Whether a font family exists is decided
    // when rendering, where a fallback list is available.
    //
    // An empty value is accepted here rather than rejected: for a font family
    // "empty" is a deliberate answer meaning "pick one for me", which is not
    // the same as the empty value of a number, where nothing was said at all.
    QString text(const char *key, const QString &fallback) const {
        QString raw;
        if (!present(key, &raw, false)) {
            return fallback;
        }
        return raw;
    }

private:
    // warnOnEmpty is false for values where "empty" is a legitimate answer.
    bool present(const char *key, QString *raw, bool warnOnEmpty = true) const {
        if (!m_settings.contains(QLatin1String(key))) {
            return false;
        }
        // QSettings treats a comma as a list separator, so "0,75" arrives as
        // two entries and toString() on that is empty. Joining it back keeps
        // the value the user actually wrote, so the complaint names it
        // instead of claiming the line was blank.
        const QVariant stored = m_settings.value(QLatin1String(key));
        *raw = (stored.metaType().id() == QMetaType::QStringList)
                   ? stored.toStringList().join(QLatin1Char(',')).trimmed()
                   : stored.toString().trimmed();
        if (raw->isEmpty()) {
            if (warnOnEmpty) {
                m_warnings->append(
                    QCoreApplication::translate(
                        "Settings", "%1: the value is empty, using the default")
                        .arg(QLatin1String(key)));
            }
            return false;
        }
        return true;
    }

    void notANumber(const char *key, const QString &raw,
                    const QString &fallback) const {
        m_warnings->append(
            QCoreApplication::translate("Settings",
                                        "%1: \"%2\" is not a number, using %3")
                .arg(QLatin1String(key), raw, fallback));
    }

    void outOfRange(const char *key, const QString &value, const QString &low,
                    const QString &high, const QString &fallback) const {
        m_warnings->append(
            QCoreApplication::translate("Settings",
                                        "%1: %2 is outside %3 to %4, using %5")
                .arg(QLatin1String(key), value, low, high, fallback));
    }

    const QSettings &m_settings;
    QStringList *m_warnings;
};

} // namespace

QStringList Settings::knownThemes() {
    // Must match the palettes in Theme.qml, in the order the editor offers
    // them. Validating here keeps a hand-edited file from leaving the overlay
    // with a nameless palette.
    return {
        QStringLiteral("bindpeek"),
        QStringLiteral("dark"),
        QStringLiteral("light"),
        QStringLiteral("contrast"),
        QStringLiteral("catppuccin-mocha"),
        QStringLiteral("catppuccin-latte"),
        QStringLiteral("nord"),
        QStringLiteral("gruvbox-dark"),
        QStringLiteral("dracula"),
        QStringLiteral("tokyo-night"),
        QStringLiteral("rose-pine"),
        QStringLiteral("solarized-light"),
        QStringLiteral("eldritch"),
        QStringLiteral("kanagawa"),
    };
}

Settings::Range Settings::showDelayRange() {
    return {kShowDelayMin, kShowDelayMax};
}
int Settings::showDelayStepMs() { return kShowDelayStep; }
Settings::Range Settings::marginRange() { return {kMarginMin, kMarginMax}; }
Settings::Range Settings::fontSizeRange() {
    return {kFontSizeMin, kFontSizeMax};
}
Settings::Range Settings::cornerRadiusRange() {
    return {kRadiusMin, kRadiusMax};
}
Settings::Range Settings::borderWidthRange() {
    return {kBorderMin, kBorderMax};
}
double Settings::opacityLow() { return kOpacityMin; }
double Settings::opacityHigh() { return kOpacityMax; }
double Settings::opacityStep() { return kOpacityStep; }

QStringList Settings::knownAlignments() {
    return {QLatin1String(alignment::kStart), QLatin1String(alignment::kCenter),
            QLatin1String(alignment::kEnd)};
}

QStringList Settings::knownDisclosures() {
    return {QLatin1String(disclosure::kExact),
            QLatin1String(disclosure::kInline),
            QLatin1String(disclosure::kFooter),
            QLatin1String(disclosure::kSections)};
}

QStringList Settings::knownArrangements() {
    return {QLatin1String(arrangement::kSource),
            QLatin1String(arrangement::kModifiers)};
}

QStringList Settings::knownPositions() {
    return {QLatin1String(kPositionCenter), QLatin1String(kPositionLeft),
            QLatin1String(kPositionRight), QLatin1String(kPositionTop),
            QLatin1String(kPositionBottom)};
}

QString Settings::positionName(Position position) {
    switch (position) {
    case Position::Left:
        return QLatin1String(kPositionLeft);
    case Position::Right:
        return QLatin1String(kPositionRight);
    case Position::Top:
        return QLatin1String(kPositionTop);
    case Position::Bottom:
        return QLatin1String(kPositionBottom);
    case Position::Center:
        break;
    }
    return QLatin1String(kPositionCenter);
}

Settings::Position Settings::positionFromName(const QString &name) {
    const QString lowered = name.trimmed().toLower();
    if (lowered == QLatin1String(kPositionLeft)) {
        return Position::Left;
    }
    if (lowered == QLatin1String(kPositionRight)) {
        return Position::Right;
    }
    if (lowered == QLatin1String(kPositionTop)) {
        return Position::Top;
    }
    if (lowered == QLatin1String(kPositionBottom)) {
        return Position::Bottom;
    }
    return Position::Center;
}

QString Settings::defaultPath() {
    return QDir::homePath() + QStringLiteral("/.config/bindpeek/") +
           QLatin1String(kFileName);
}

// Inputs the constructor is measured against. The same shapes apply to every
// key, so they are listed once rather than per value:
//
//   file / value            | result
//   ------------------------|--------------------------------------------
//   file missing            | all defaults, no warning (the normal case)
//   key absent              | that default, no warning
//   unknown key             | ignored, no warning (forward compatible)
//   value in range          | taken
//   value at a bound        | taken, the bounds are inclusive
//   value out of range      | default + warning
//   value not a number      | default + warning
//   empty value             | default + warning, except fontFamily where an
//                           |   empty value means "pick one for me"
//   opacity=0,92            | default + warning: the decimal separator is a
//                           |   point, never a comma, or the same file would
//                           |   mean different things per locale
//   position=links          | default + warning, the words are English
//   theme=nonesuch          | default + warning, must be a known palette
//   followSystemScheme=yes  | true; true/1/yes and false/0/no are accepted
//   fontFamily=<anything>   | taken as written, resolved when rendering
Settings::Settings(QString path)
    : m_showDelayMs(kShowDelayDefault),
      m_overlayEnabled(kOverlayEnabledDefault),
      m_disclosure(QLatin1String(kDisclosureDefault)),
      m_arrangement(QLatin1String(kArrangementDefault)),
      m_alignment(QLatin1String(kAlignmentDefault)),
      m_ignoreLoneShift(kIgnoreLoneShiftDefault), m_position(kPositionDefault),
      m_marginPx(kMarginDefault), m_edgeInsetPx(kEdgeInsetDefault),
      m_theme(QLatin1String(kThemeDefault)),
      m_followSystemScheme(kFollowSystemDefault),
      m_themeLight(QLatin1String(kThemeLightDefault)),
      m_themeDark(QLatin1String(kThemeDarkDefault)),
      m_fontSizePt(kFontSizeDefault), m_cornerRadiusPx(kRadiusDefault),
      m_borderWidthPx(kBorderDefault), m_opacity(kOpacityDefault) {
    const QString file = path.isEmpty() ? defaultPath() : std::move(path);
    if (!QFileInfo::exists(file)) {
        return;
    }

    const QSettings settings(file, QSettings::IniFormat);
    const Reader read(settings, &m_warnings);

    m_showDelayMs = read.integer(kKeyShowDelay, kShowDelayDefault,
                                 kShowDelayMin, kShowDelayMax);
    m_marginPx =
        read.integer(kKeyMargin, kMarginDefault, kMarginMin, kMarginMax);
    m_fontSizePt = read.integer(kKeyFontSize, kFontSizeDefault, kFontSizeMin,
                                kFontSizeMax);
    m_cornerRadiusPx =
        read.integer(kKeyRadius, kRadiusDefault, kRadiusMin, kRadiusMax);
    m_borderWidthPx =
        read.integer(kKeyBorder, kBorderDefault, kBorderMin, kBorderMax);
    m_opacity =
        read.number(kKeyOpacity, kOpacityDefault, kOpacityMin, kOpacityMax);
    m_followSystemScheme = read.boolean(kKeyFollowSystem, kFollowSystemDefault);
    m_edgeInsetPx =
        read.integer(kKeyEdgeInset, kEdgeInsetDefault, kMarginMin, kMarginMax);
    m_overlayEnabled = read.boolean(kKeyOverlayEnabled, kOverlayEnabledDefault);

    m_disclosure = read.word(kKeyDisclosure, QLatin1String(kDisclosureDefault),
                             knownDisclosures());
    m_arrangement =
        read.word(kKeyArrangement, QLatin1String(kArrangementDefault),
                  knownArrangements());
    m_alignment = read.word(kKeyAlignment, QLatin1String(kAlignmentDefault),
                            knownAlignments());
    m_ignoreLoneShift =
        read.boolean(kKeyIgnoreLoneShift, kIgnoreLoneShiftDefault);
    m_theme = read.word(kKeyTheme, QLatin1String(kThemeDefault), knownThemes());
    m_themeLight = read.word(kKeyThemeLight, QLatin1String(kThemeLightDefault),
                             knownThemes());
    m_themeDark = read.word(kKeyThemeDark, QLatin1String(kThemeDarkDefault),
                            knownThemes());
    m_fontFamily = read.text(kKeyFontFamily, QString());

    m_position = positionFromName(read.word(
        kKeyPosition, positionName(kPositionDefault), knownPositions()));
}

int Settings::showDelayMs() const { return m_showDelayMs; }
Settings::Position Settings::position() const { return m_position; }
int Settings::marginPx() const { return m_marginPx; }
int Settings::edgeInsetPx() const { return m_edgeInsetPx; }
bool Settings::overlayEnabled() const { return m_overlayEnabled; }
QString Settings::disclosure() const { return m_disclosure; }

QString Settings::arrangement() const { return m_arrangement; }

bool Settings::arrangesByModifier() const {
    return m_arrangement == QLatin1String(arrangement::kModifiers);
}

QString Settings::alignment() const { return m_alignment; }

bool Settings::alignsAtStart() const {
    return m_alignment == QLatin1String(alignment::kStart);
}

bool Settings::alignsAtEnd() const {
    return m_alignment == QLatin1String(alignment::kEnd);
}

bool Settings::showsDeeper() const {
    // Nor with the footer: that line already says how much a further key
    // would still reach, so listing all of it as well says the same thing
    // twice and at length.
    return m_disclosure != QLatin1String(disclosure::kExact) &&
           m_disclosure != QLatin1String(disclosure::kFooter);
}
bool Settings::deeperInSections() const {
    return m_disclosure == QLatin1String(disclosure::kSections);
}
bool Settings::showsContinuations() const {
    return m_disclosure == QLatin1String(disclosure::kFooter);
}
bool Settings::ignoreLoneShift() const { return m_ignoreLoneShift; }
bool Settings::anchoredToEdge() const { return m_position != Position::Center; }
QString Settings::theme() const { return m_theme; }
bool Settings::followSystemScheme() const { return m_followSystemScheme; }
QString Settings::themeLight() const { return m_themeLight; }
QString Settings::themeDark() const { return m_themeDark; }
QString Settings::fontFamily() const { return m_fontFamily; }
int Settings::fontSizePt() const { return m_fontSizePt; }
int Settings::cornerRadiusPx() const { return m_cornerRadiusPx; }
int Settings::borderWidthPx() const { return m_borderWidthPx; }
double Settings::opacity() const { return m_opacity; }
QStringList Settings::warnings() const { return m_warnings; }

void Settings::setShowDelayMs(int value) { m_showDelayMs = value; }
void Settings::setPosition(Position value) { m_position = value; }
void Settings::setMarginPx(int value) { m_marginPx = value; }
void Settings::setEdgeInsetPx(int value) { m_edgeInsetPx = value; }
void Settings::setOverlayEnabled(bool value) { m_overlayEnabled = value; }
void Settings::setDisclosure(const QString &value) { m_disclosure = value; }
void Settings::setArrangement(const QString &value) { m_arrangement = value; }
void Settings::setAlignment(const QString &value) { m_alignment = value; }
void Settings::setIgnoreLoneShift(bool value) { m_ignoreLoneShift = value; }
void Settings::setTheme(const QString &value) { m_theme = value; }
void Settings::setFollowSystemScheme(bool value) {
    m_followSystemScheme = value;
}
void Settings::setThemeLight(const QString &value) { m_themeLight = value; }
void Settings::setThemeDark(const QString &value) { m_themeDark = value; }
void Settings::setFontFamily(const QString &value) { m_fontFamily = value; }
void Settings::setFontSizePt(int value) { m_fontSizePt = value; }
void Settings::setCornerRadiusPx(int value) { m_cornerRadiusPx = value; }
void Settings::setBorderWidthPx(int value) { m_borderWidthPx = value; }
void Settings::setOpacity(double value) { m_opacity = value; }

// Updates the file in place, one line at a time.
//
// Rewriting it through QSettings would be a line of code, and would throw away
// every comment in it. The comments are the reason the file can be edited by
// hand at all, so they are worth the extra work: each known key has its value
// replaced where it stands, anything unknown is left untouched, and a key that
// is missing gets appended.
bool Settings::save(const QString &path) const {
    const QString file = path.isEmpty() ? defaultPath() : path;

    const QFileInfo info(file);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    // The value each key should end up with. Doubles are written with a point
    // through QString::number, never through the locale, so the file means the
    // same thing on every machine.
    const QList<QPair<QString, QString>> wanted = {
        {QLatin1String(kKeyShowDelay), QString::number(m_showDelayMs)},
        {QLatin1String(kKeyPosition), positionName(m_position)},
        {QLatin1String(kKeyMargin), QString::number(m_marginPx)},
        {QLatin1String(kKeyEdgeInset), QString::number(m_edgeInsetPx)},
        {QLatin1String(kKeyOverlayEnabled),
         m_overlayEnabled ? QStringLiteral("true") : QStringLiteral("false")},
        {QLatin1String(kKeyDisclosure), m_disclosure},
        {QLatin1String(kKeyArrangement), m_arrangement},
        {QLatin1String(kKeyAlignment), m_alignment},
        {QLatin1String(kKeyIgnoreLoneShift),
         m_ignoreLoneShift ? QStringLiteral("true") : QStringLiteral("false")},
        {QLatin1String(kKeyTheme), m_theme},
        {QLatin1String(kKeyFollowSystem), m_followSystemScheme
                                              ? QStringLiteral("true")
                                              : QStringLiteral("false")},
        {QLatin1String(kKeyThemeLight), m_themeLight},
        {QLatin1String(kKeyThemeDark), m_themeDark},
        {QLatin1String(kKeyFontFamily), m_fontFamily},
        {QLatin1String(kKeyFontSize), QString::number(m_fontSizePt)},
        {QLatin1String(kKeyRadius), QString::number(m_cornerRadiusPx)},
        {QLatin1String(kKeyBorder), QString::number(m_borderWidthPx)},
        {QLatin1String(kKeyOpacity), QString::number(m_opacity, 'f', 2)},
    };

    QStringList lines;
    if (QFileInfo::exists(file)) {
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return false;
        }
        QTextStream stream(&in);
        stream.setEncoding(QStringConverter::Utf8);
        while (!stream.atEnd()) {
            lines.append(stream.readLine());
        }
    }

    for (const auto &entry : wanted) {
        bool replaced = false;
        for (QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('#'))) {
                continue;
            }
            const qsizetype equals = trimmed.indexOf(QLatin1Char('='));
            if (equals < 0 || trimmed.left(equals).trimmed() != entry.first) {
                continue;
            }
            line = entry.first + QLatin1Char('=') + entry.second;
            replaced = true;
            break;
        }
        if (!replaced) {
            lines.append(entry.first + QLatin1Char('=') + entry.second);
        }
    }

    // QSaveFile writes to a temporary file and renames it into place, so a
    // reader never sees the file half written. Truncate-and-write would leave
    // an empty file for a moment, and the overlay watches this path: it would
    // read nothing and fall back to defaults without a word.
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&out);
    stream.setEncoding(QStringConverter::Utf8);
    for (const QString &line : std::as_const(lines)) {
        stream << line << Qt::endl;
    }
    stream.flush();
    return out.commit();
}

bool Settings::writeTemplateIfMissing(const QString &path) {
    const QString file = path.isEmpty() ? defaultPath() : path;
    if (QFileInfo::exists(file)) {
        return true;
    }

    const QFileInfo info(file);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    // Written by hand rather than through QSettings, which strips comments.
    // The comments are the point: they are what makes the file usable without
    // documentation next to it.
    QTextStream stream(&out);
    stream.setEncoding(QStringConverter::Utf8);
    stream
        << "# bindpeek settings.\n"
        << "# Delete a line to fall back to its default.\n"
        << "\n"
        << "# How long a modifier has to be held before the panel appears,\n"
        << "# in milliseconds. " << kShowDelayMin << " shows it at once, "
        << kShowDelayMax << " is the maximum.\n"
        << kKeyShowDelay << "=" << kShowDelayDefault << "\n"
        << "\n"
        << "# Whether the panel is wanted at all. The tray writes this line\n"
        << "# when the panel is switched on or off, so the answer survives a\n"
        << "# logout.\n"
        << kKeyOverlayEnabled << "="
        << (kOverlayEnabledDefault ? "true" : "false") << "\n"
        << "\n"
        << "# Where the panel sits: "
        << knownPositions().join(QStringLiteral(", ")) << ".\n"
        << "# center floats in the middle, the others sit against that edge.\n"
        << kKeyPosition << "=" << positionName(kPositionDefault) << "\n"
        << "\n"
        << "# Distance to that edge in pixels. Ignored for center.\n"
        << kKeyMargin << "=" << kMarginDefault << "\n"
        << "\n"
        << "# How far a panel against an edge stops short of the two ends of\n"
        << "# it, in pixels. 0 runs it from corner to corner. Ignored for\n"
        << "# center.\n"
        << kKeyEdgeInset << "=" << kEdgeInsetDefault << "\n"
        << "\n"
        << "# How the shortcuts that need a further modifier are shown:\n"
        << "# " << knownDisclosures().join(QStringLiteral(", ")) << ".\n"
        << "# exact shows only what the held keys fire right now; inline puts\n"
        << "# the rest in the same list with the missing modifiers in front "
           "of\n"
        << "# the key; footer adds a line saying which modifier leads to how\n"
        << "# many more; sections gives each further combination a block of\n"
        << "# its own, headed by its keys.\n"
        << kKeyDisclosure << "=" << kDisclosureDefault << "\n"
        << "\n"
        << "# How the groups are arranged: "
        << knownArrangements().join(QStringLiteral(", ")) << ".\n"
        << "# source keeps the headings the session itself uses, an\n"
        << "# application under KDE, a submap under Hyprland, a mode under\n"
        << "# sway; modifiers heads a group with the combination its\n"
        << "# shortcuts want, nearest first.\n"
        << kKeyArrangement << "=" << kArrangementDefault << "\n"
        << "\n"
        << "# Where the content sits along the edge the panel spans:\n"
        << "# " << knownAlignments().join(QStringLiteral(", ")) << ".\n"
        << "# Along the top or bottom that is left, centre and right; along a\n"
        << "# side it is top, middle and bottom. Nothing to see in the centre\n"
        << "# position, which spans nothing.\n"
        << kKeyAlignment << "=" << kAlignmentDefault << "\n"
        << "\n"
        << "# Whether a Shift held on its own is ignored. Shift alone is how\n"
        << "# capitals are typed, not how a shortcut is looked up.\n"
        << kKeyIgnoreLoneShift << "="
        << (kIgnoreLoneShiftDefault ? "true" : "false") << "\n"
        << "\n"
        << "# Palette: " << knownThemes().join(QStringLiteral(", ")) << ".\n"
        << kKeyTheme << "=" << kThemeDefault << "\n"
        << "\n"
        << "# Follow the desktop's light/dark setting. When it does, the two\n"
        << "# palettes below are used and " << kKeyTheme
        << " only applies on a\n"
        << "# desktop that reports no scheme at all.\n"
        << kKeyFollowSystem << "=" << (kFollowSystemDefault ? "true" : "false")
        << "\n"
        << kKeyThemeLight << "=" << kThemeLightDefault << "\n"
        << kKeyThemeDark << "=" << kThemeDarkDefault << "\n"
        << "\n"
        << "# Font family. Empty picks the first installed from a built-in "
           "list.\n"
        << kKeyFontFamily << "=\n"
        << "# Base font size in points, " << kFontSizeMin << " to "
        << kFontSizeMax << ".\n"
        << kKeyFontSize << "=" << kFontSizeDefault << "\n"
        << "\n"
        << "# Corner radius in pixels, " << kRadiusMin << " to " << kRadiusMax
        << ". 0 gives square corners.\n"
        << kKeyRadius << "=" << kRadiusDefault << "\n"
        << "# Border width in pixels, " << kBorderMin << " to " << kBorderMax
        << ". 0 removes the border.\n"
        << kKeyBorder << "=" << kBorderDefault << "\n"
        << "# Background opacity, " << kOpacityMin << " to " << kOpacityMax
        << ". Use a point, not a comma.\n"
        << kKeyOpacity << "=" << kOpacityDefault << "\n";
    return true;
}

bool spansHorizontally(Settings::Position position) {
    return position == Settings::Position::Top ||
           position == Settings::Position::Bottom;
}

bool spansVertically(Settings::Position position) {
    return position == Settings::Position::Left ||
           position == Settings::Position::Right;
}

} // namespace bindpeek
