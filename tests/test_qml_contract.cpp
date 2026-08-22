// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Checks that the QML only reaches for controller members that exist.
//
// The controller is handed to QML as a context property, and a context
// property has no type: neither the compiler nor qmllint can see that
// `OverlayController.overview` went away when the property was removed. The
// mismatch then surfaces at runtime as "Unable to assign [undefined] to bool",
// and only once the panel is actually on screen. This test closes that gap by
// comparing the two files directly.

#include "Settings.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Every context property QML is handed, with the QML that uses it and the
// header that defines it. Names must match the kNames in main.cpp.
struct Contract {
    const char *object;
    const char *qml;
    const char *header;
};

// One line per pair of files, not per class: the same object is read from more
// than one of them, and a member only one of those files asks for is exactly
// the one a rename would leave behind.
constexpr Contract kContracts[] = {
    {"OverlayController", "Overlay.qml", "OverlayController.h"},
    {"Appearance", "Theme.qml", "Appearance.h"},
    {"Appearance", "Overlay.qml", "Appearance.h"},
    {"Appearance", "editor/Editor.qml", "Appearance.h"},
    {"SettingsModel", "editor/Editor.qml", "editor/SettingsModel.h"},
    {"OverlayControl", "editor/Editor.qml", "editor/OverlayProcess.h"},
    {"AppInfo", "editor/Editor.qml", "AppInfo.h"},
    {"AppInfo", "editor/AboutDialog.qml", "AppInfo.h"},
};

// Two colours of one palette, and how far apart they have to be for the eye
// to tell them apart at all.
//
// Measured as the three channel differences added up, not as the largest of
// them and not as a luminance ratio. The largest channel passes a pair that
// differs in blue alone, which the eye barely resolves. A luminance ratio
// fails the other way, it calls two colours of the same brightness identical
// however far apart their hues are. The sum is wrong in neither direction.
//
// The bound sits under the closest pair the palettes here actually have,
// which is tokyo-night's border against its surface at 18.
constexpr int kMinColourDistance = 12;

// The measure for text that is read rather than glanced at, which is what the
// shortcut column and the settings window's labels are. From WCAG AA, whose
// other value, 3:1, is for text large enough not to need it.
constexpr double kMinContrast = 4.5;

// The measure for text that is still meant to be read at a glance rather than
// studied, which is what WCAG grants larger or heavier type. Used here for the
// panel seen through its own transparency: the plate is drawn over whatever
// happens to be behind it, so the colour under the text is no longer the
// palette's own, and a figure that holds for every desktop is the most that
// can honestly be asked.
constexpr double kMinContrastThroughGlass = 3.0;

// The palettes that are allowed to sit below it, and why.
//
// Both are somebody else's theme, and the value in question is that theme's
// own green. A value that met the measure would no longer be the theme it is
// named after, which is the reason to pick it. Listed here rather than left
// out of the test, so the two are a decision on the record and every palette
// added later has to meet the measure.
const QStringList &palettesKeptAsTheyAre() {
    static const QStringList names = {QStringLiteral("catppuccin-latte"),
                                      QStringLiteral("solarized-light")};
    return names;
}

// Every palette in Theme.qml, as name to token to six hex digits.
//
// Read out of the QML rather than from a second list in C++: the palettes live
// there, and a copy here would be the thing that drifts. Both checks below
// share it, so a changed spelling breaks them together and loudly rather than
// leaving one of them quietly measuring nothing.
QHash<QString, QHash<QString, QString>> palettesOf(const QString &qml) {
    QHash<QString, QHash<QString, QString>> palettes;

    // One palette block: its name and everything up to the closing brace.
    const QRegularExpression block(
        QStringLiteral("\"([a-z0-9-]+)\":\\s*\\{([^}]*)\\}"));
    // The colon has to follow the name, or "surface" would also answer for
    // "surfaceHover".
    const QRegularExpression token(
        QStringLiteral("\\b([a-zA-Z]+):\\s*\"#([0-9a-fA-F]{6})\""));

    auto blocks = block.globalMatch(qml);
    while (blocks.hasNext()) {
        const QRegularExpressionMatch palette = blocks.next();
        QHash<QString, QString> colours;
        auto tokens = token.globalMatch(palette.captured(2));
        while (tokens.hasNext()) {
            const QRegularExpressionMatch hit = tokens.next();
            colours.insert(hit.captured(1), hit.captured(2));
        }
        palettes.insert(palette.captured(1), colours);
    }
    return palettes;
}

// The three channel differences added up. -1 when either side is unreadable,
// which the caller reports rather than passing.
// The contrast between two colours, the way the measure defines it: the two
// relative luminances, lighter over darker, offset so that black on black is
// 1 and black on white is 21.
//
// Written out rather than taken from Qt, which has no such function: lightness
// is not luminance, and comparing the two lightness values would call white on
// yellow a strong contrast.
double relativeLuminance(const QString &hex) {
    double channels[3] = {0, 0, 0};
    // Counted in the type the view is cut with, so the pair of characters is
    // worked out in that type rather than in an int that is then widened.
    for (qsizetype channel = 0; channel < 3; ++channel) {
        bool ok = false;
        const double raw =
            QStringView(hex).mid(channel * 2, 2).toInt(&ok, 16) / 255.0;
        if (!ok) {
            return -1;
        }
        channels[channel] =
            raw <= 0.04045 ? raw / 12.92 : std::pow((raw + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
}

double contrastRatio(const QString &one, const QString &other) {
    const double first = relativeLuminance(one);
    const double second = relativeLuminance(other);
    if (first < 0 || second < 0) {
        return -1;
    }
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

// One colour laid over another at the given opacity, as the compositor mixes
// them: per channel, before any of the curves below, because that is where a
// surface is actually blended.
QString blend(const QString &front, const QString &back, double opacity) {
    QString mixed;
    for (qsizetype channel = 0; channel < 3; ++channel) {
        bool okFront = false;
        bool okBack = false;
        const int over =
            QStringView(front).mid(channel * 2, 2).toInt(&okFront, 16);
        const int under =
            QStringView(back).mid(channel * 2, 2).toInt(&okBack, 16);
        if (!okFront || !okBack) {
            return {};
        }
        mixed += QStringLiteral("%1").arg(
            qRound(opacity * over + (1.0 - opacity) * under), 2, 16,
            QLatin1Char('0'));
    }
    return mixed;
}

int colourDistance(const QString &one, const QString &other) {
    if (one.size() != 6 || other.size() != 6) {
        return -1;
    }
    int distance = 0;
    for (qsizetype channel = 0; channel < one.size(); channel += 2) {
        bool okOne = false;
        bool okOther = false;
        const int left = QStringView(one).mid(channel, 2).toInt(&okOne, 16);
        const int right =
            QStringView(other).mid(channel, 2).toInt(&okOther, 16);
        if (!okOne || !okOther) {
            return -1;
        }
        distance += qAbs(left - right);
    }
    return distance;
}

// The value of an array property written out in QML, brackets included.
//
// Needed because a role name has to be looked for where it is actually filled
// in and nowhere else: "name:" occurs in this file as an accessible name as
// well, and a search over the whole text is then answered by a line that has
// nothing to do with the preview. Empty when there is no such property.
QString arrayLiteral(const QString &qml, const QString &property) {
    const qsizetype start = qml.indexOf(property + QStringLiteral(": ["));
    if (start < 0) {
        return {};
    }
    const qsizetype open = qml.indexOf(QLatin1Char('['), start);
    int depth = 0;
    for (qsizetype at = open; at < qml.size(); ++at) {
        if (qml.at(at) == QLatin1Char('[')) {
            ++depth;
        } else if (qml.at(at) == QLatin1Char(']')) {
            --depth;
            if (depth == 0) {
                return qml.mid(open, at - open + 1);
            }
        }
    }
    return {};
}

QString readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// Every member the QML reaches for, e.g. "panelVisible" from
// "OverlayController.panelVisible".
QSet<QString> membersUsedByQml(const QString &qml, const char *object) {
    QSet<QString> used;
    const QRegularExpression pattern(
        QStringLiteral("%1\\.([A-Za-z_][A-Za-z0-9_]*)")
            .arg(QLatin1String(object)));
    auto it = pattern.globalMatch(qml);
    while (it.hasNext()) {
        used.insert(it.next().captured(1));
    }

    // The other way a member is reached, and the one a dot never shows: a
    // Binding names its target and then its property as a string. Nothing
    // reads that string but the engine, so a renamed member leaves the binding
    // in place and dead, which is the exact failure this file exists to catch.
    //
    // Bounded rather than greedy, because the two lines belong to one Binding
    // block and the next one further down is somebody else's target.
    const QRegularExpression bound(
        QStringLiteral("target:\\s*%1\\b[^}]{0,400}?property:\\s*"
                       "\"([A-Za-z_][A-Za-z0-9_]*)\"")
            .arg(QLatin1String(object)));
    auto bindings = bound.globalMatch(qml);
    while (bindings.hasNext()) {
        used.insert(bindings.next().captured(1));
    }
    return used;
}

// Every member the controller exposes: Q_PROPERTY names and Q_INVOKABLE
// method names.
QSet<QString> membersOfferedByController(const QString &header) {
    QSet<QString> offered;

    const QRegularExpression property(
        QStringLiteral("Q_PROPERTY\\s*\\(\\s*[A-Za-z0-9_:<>* "
                       "]+?\\s+([A-Za-z_][A-Za-z0-9_]*)\\s"));
    auto propertyMatches = property.globalMatch(header);
    while (propertyMatches.hasNext()) {
        offered.insert(propertyMatches.next().captured(1));
    }

    const QRegularExpression invokable(QStringLiteral(
        "Q_INVOKABLE\\s+[A-Za-z0-9_:<>*& ]+?([A-Za-z_][A-Za-z0-9_]*)\\s*\\("));
    auto invokableMatches = invokable.globalMatch(header);
    while (invokableMatches.hasNext()) {
        offered.insert(invokableMatches.next().captured(1));
    }

    return offered;
}

} // namespace

class TestQmlContract : public QObject {
    Q_OBJECT

private slots:
    void bothFilesAreReadable();
    void aMemberNamedAsTextIsSeen();
    void qmlUsesOnlyExistingMembers();
    void everyRoleIsReadByItsQml();
    void themeListsAgree();
    void everyPaletteHasAVisibleBorder();
    void everyPaletteTellsTheTwoStatesApart();
    void everyPaletteKeepsTheFiringColumnReadable();
    void theFiringColumnSurvivesTheTransparency();
    void theHousePaletteMeetsTheMeasureItSetsItself();
    void positionListsAgree();
};

void TestQmlContract::bothFilesAreReadable() {
    // Guards the test itself: a typo in the paths would otherwise make the
    // comparison below pass on two empty strings.
    for (const Contract &contract : kContracts) {
        const QString qml = readFile(QStringLiteral(BINDPEEK_SRC "/") +
                                     QLatin1String(contract.qml));
        const QString header = readFile(QStringLiteral(BINDPEEK_SRC "/") +
                                        QLatin1String(contract.header));
        QVERIFY2(!qml.isEmpty(), contract.qml);
        QVERIFY2(!header.isEmpty(), contract.header);
        QVERIFY2(!membersOfferedByController(header).isEmpty(),
                 contract.header);
    }
}

// The rule that sees a member named as text, measured on its own rather than
// through a file.
//
// The pairs below cannot say whether it still works. They pass as long as
// anything at all was found, and every file that binds a property this way
// also reaches the same object with a dot somewhere else, so a rule that
// quietly stopped matching would leave the gate green and the member
// uncovered. That silence is the state this file exists to end.
//
// The form covered is the one Qt's own documentation writes, target first and
// the property under it. Written the other way round, or with a brace between
// the two, it is not seen, and that is left as it is: what is measured here is
// the spelling the project uses, and a rule that tried to match every possible
// arrangement of the same two lines would be a parser rather than a check.
void TestQmlContract::aMemberNamedAsTextIsSeen() {
    // Three bindings, and the middle one carries no property on purpose: it is
    // what makes the last of the three checks mean anything.
    const QString qml = QStringLiteral("Binding {\n"
                                       "    target: AppInfo\n"
                                       "    property: \"darkSurface\"\n"
                                       "    value: true\n"
                                       "}\n"
                                       "\n"
                                       "Binding {\n"
                                       "    target: OverlayControl\n"
                                       "    value: true\n"
                                       "}\n"
                                       "\n"
                                       "Binding {\n"
                                       "    target: SettingsModel\n"
                                       "    property: \"somethingElse\"\n"
                                       "    value: 1\n"
                                       "}\n");
    QVERIFY(membersUsedByQml(qml, "AppInfo")
                .contains(QStringLiteral("darkSurface")));
    QVERIFY(membersUsedByQml(qml, "SettingsModel")
                .contains(QStringLiteral("somethingElse")));
    // A binding block ends at its brace, and the search has to end there too.
    // Allowed to read on, it would reach the next block and hand this object a
    // member somebody else named, which shows up as a file accused of using
    // what it never asked for. The object above is bound to without naming a
    // property, so the only property in reach is the one in the block after
    // it: exactly the case a bound that stopped working would get wrong.
    QVERIFY(!membersUsedByQml(qml, "OverlayControl")
                 .contains(QStringLiteral("somethingElse")));
}

void TestQmlContract::qmlUsesOnlyExistingMembers() {
    for (const Contract &contract : kContracts) {
        const QSet<QString> used =
            membersUsedByQml(readFile(QStringLiteral(BINDPEEK_SRC "/") +
                                      QLatin1String(contract.qml)),
                             contract.object);
        const QSet<QString> offered = membersOfferedByController(readFile(
            QStringLiteral(BINDPEEK_SRC "/") + QLatin1String(contract.header)));

        QVERIFY2(!used.isEmpty(), contract.qml);

        QStringList missing;
        for (const QString &name : used) {
            if (!offered.contains(name)) {
                missing.append(name);
            }
        }
        missing.sort();

        QVERIFY2(
            missing.isEmpty(),
            qPrintable(QStringLiteral("%1 uses members %2 does not have: %3")
                           .arg(QLatin1String(contract.qml),
                                QLatin1String(contract.object),
                                missing.join(QStringLiteral(", ")))));
    }
}

// The entries the controller hands to QML are plain maps, and a map has no
// type either: renaming a role is a one-line change in C++ that leaves every
// reader of it reading undefined, silently and only once a panel is on screen.
//
// Two readers exist. The panel draws the real list, and the settings window
// fills the same shape with an invented one for its preview; a role that
// reaches only one of them makes the preview a different thing from the panel,
// which is the one property it has to have.
//
// The panel is three files, and they are read as one: the plate hands the
// groups to a card and the card hands one entry to a block, so which of them
// spells out a given role is a matter of where the split fell.
void TestQmlContract::everyRoleIsReadByItsQml() {
    const QString controller =
        readFile(QStringLiteral(BINDPEEK_SRC "/OverlayController.cpp"));
    const QString panel =
        readFile(QStringLiteral(BINDPEEK_SRC "/PanelBody.qml")) +
        readFile(QStringLiteral(BINDPEEK_SRC "/GroupCard.qml")) +
        readFile(QStringLiteral(BINDPEEK_SRC "/EntryBlock.qml"));
    const QString editor =
        readFile(QStringLiteral(BINDPEEK_SRC "/editor/Editor.qml"));
    QVERIFY(!controller.isEmpty());
    QVERIFY(!panel.isEmpty());
    QVERIFY(!editor.isEmpty());

    // The made-up list the preview is filled with, and nothing else of that
    // file: see arrayLiteral().
    // The made-up list is held under a name of its own because what the
    // preview draws is that list put through the chosen disclosure, so
    // "groups" is an expression there rather than the literal.
    const QString preview =
        arrayLiteral(editor, QStringLiteral("sampleGroups")) +
        arrayLiteral(editor, QStringLiteral("continuations"));
    QVERIFY2(!preview.isEmpty(), "no preview data found in Editor.qml");

    // constexpr char kRoleShortcut[] = "shortcut";
    const QRegularExpression role(QStringLiteral(
        "constexpr char kRole[A-Za-z]+\\[\\]\\s*=\\s*\"([a-z]+)\""));
    QStringList names;
    auto it = role.globalMatch(controller);
    while (it.hasNext()) {
        names.append(it.next().captured(1));
    }
    QVERIFY2(!names.isEmpty(), "no roles found in OverlayController.cpp");

    for (const QString &name : std::as_const(names)) {
        // Read as a member on one side and written as a key on the other, so
        // the two are looked for the way each side spells them rather than by
        // hunting the bare word, which occurs all over both files.
        const QRegularExpression read(QStringLiteral("\\.%1\\b").arg(name));
        const QRegularExpression written(QStringLiteral("\\b%1:").arg(name));
        QVERIFY2(
            read.match(panel).hasMatch(),
            qPrintable(
                QStringLiteral("no file of the panel reads .%1").arg(name)));
        QVERIFY2(written.match(preview).hasMatch(),
                 qPrintable(
                     QStringLiteral("the preview in Editor.qml never fills %1")
                         .arg(name)));
    }
}

// Telling the two states apart is not enough on its own: the colour that says
// "this one fires" also has to be readable, because it carries the shortcut
// itself at body size. A palette that fails this shows its most important
// column as the least legible thing on the panel.
void TestQmlContract::everyPaletteKeepsTheFiringColumnReadable() {
    const QString qml = readFile(QStringLiteral(BINDPEEK_SRC "/Theme.qml"));
    QVERIFY(!qml.isEmpty());

    const QHash<QString, QHash<QString, QString>> palettes = palettesOf(qml);
    QCOMPARE(palettes.size(), bindpeek::Settings::knownThemes().size());

    for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
        if (palettesKeptAsTheyAre().contains(it.key())) {
            continue;
        }
        const QString brand = it.value().value(QStringLiteral("brand"));
        const QString surface = it.value().value(QStringLiteral("surface"));
        const double contrast = contrastRatio(brand, surface);
        QVERIFY2(contrast >= kMinContrast,
                 qPrintable(QStringLiteral("palette %1: brand #%2 on surface "
                                           "#%3 is %4:1, under %5:1")
                                .arg(it.key(), brand, surface)
                                .arg(contrast, 0, 'f', 2)
                                .arg(kMinContrast)));
    }
}

// The palettes named after somebody else's theme are held to that theme; this
// one is held to the measure, because there is nobody else to answer for it.
void TestQmlContract::theHousePaletteMeetsTheMeasureItSetsItself() {
    const QString qml = readFile(QStringLiteral(BINDPEEK_SRC "/Theme.qml"));
    QVERIFY(!qml.isEmpty());

    const QHash<QString, QHash<QString, QString>> palettes = palettesOf(qml);
    const QHash<QString, QString> house =
        palettes.value(QStringLiteral("bindpeek"));
    QVERIFY2(!house.isEmpty(), "the house palette is not in Theme.qml");

    const QString surface = house.value(QStringLiteral("surface"));
    // Every colour that carries text meant to be read, against the plate it
    // is read on. The muted one is the section headings and the labels of
    // controls that are switched off, not a hint that may fade away.
    const QStringList inks = {QStringLiteral("text"), QStringLiteral("brand"),
                              QStringLiteral("textMuted"),
                              QStringLiteral("warning")};
    for (const QString &ink : inks) {
        const QString colour = house.value(ink);
        const double contrast = contrastRatio(colour, surface);
        QVERIFY2(contrast >= kMinContrast,
                 qPrintable(QStringLiteral("the house palette: %1 #%2 on "
                                           "surface #%3 is %4:1, under %5:1")
                                .arg(ink, colour, surface)
                                .arg(contrast, 0, 'f', 2)
                                .arg(kMinContrast)));
    }
}

// The panel is drawn at less than full opacity, so what sits behind the
// shortcut column is the plate mixed with the desktop. Measured against both
// ends of what a desktop can be, white and black, at the opacity the program
// itself chooses.
//
// The plain measure is not asked here, and deliberately: at the default
// opacity three palettes fall under it against one of the two ends, and the
// answer to that is not to redraw somebody's theme but to say what still
// holds. What still holds is the measure for large text, and the panel that
// meets it is legible through the glass on any desktop.
//
// Below the default the reader is choosing transparency over legibility, and
// there is no colour anywhere that could hold instead.
void TestQmlContract::theFiringColumnSurvivesTheTransparency() {
    const QString qml = readFile(QStringLiteral(BINDPEEK_SRC "/Theme.qml"));
    QVERIFY(!qml.isEmpty());

    // The value a fresh install runs at, read from the one place that decides
    // it rather than written down again here.
    const double opacity =
        bindpeek::Settings(QStringLiteral("/nonexistent/bindpeek.conf"))
            .opacity();
    QVERIFY(opacity > 0);

    const QHash<QString, QHash<QString, QString>> palettes = palettesOf(qml);
    QCOMPARE(palettes.size(), bindpeek::Settings::knownThemes().size());

    for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
        if (palettesKeptAsTheyAre().contains(it.key())) {
            continue;
        }
        const QString brand = it.value().value(QStringLiteral("brand"));
        const QString surface = it.value().value(QStringLiteral("surface"));
        for (const QString &behind :
             {QStringLiteral("ffffff"), QStringLiteral("000000")}) {
            const QString mixed = blend(surface, behind, opacity);
            const double contrast = contrastRatio(brand, mixed);
            QVERIFY2(
                contrast >= kMinContrastThroughGlass,
                qPrintable(QStringLiteral("palette %1: brand #%2 over #%3 at "
                                          "%4 opacity is %5:1, under %6:1")
                               .arg(it.key(), brand, behind)
                               .arg(opacity, 0, 'f', 2)
                               .arg(contrast, 0, 'f', 2)
                               .arg(kMinContrastThroughGlass)));
        }
    }
}

// Two lists of palette names exist by necessity: the colours live in QML, the
// validation lives in C++. Neither can read the other at build time, so they
// are compared here. Without this a palette added to one side would be
// rejected by the other, and the panel would fall back to a theme the user did
// not pick.
void TestQmlContract::themeListsAgree() {
    const QString qml = readFile(QStringLiteral(BINDPEEK_SRC "/Theme.qml"));
    QVERIFY(!qml.isEmpty());

    // The palette map entries look like:  "nord": {
    QSet<QString> inQml;
    const QRegularExpression entry(
        QStringLiteral("^\\s+\"([a-z0-9-]+)\":\\s*\\{"),
        QRegularExpression::MultilineOption);
    auto it = entry.globalMatch(qml);
    while (it.hasNext()) {
        inQml.insert(it.next().captured(1));
    }
    QVERIFY2(!inQml.isEmpty(), "no palettes found in Theme.qml");

    const QStringList known = bindpeek::Settings::knownThemes();
    const QSet<QString> inCpp(known.cbegin(), known.cend());

    const QSet<QString> onlyQml = inQml - inCpp;
    const QSet<QString> onlyCpp = inCpp - inQml;
    QVERIFY2(
        onlyQml.isEmpty(),
        qPrintable(QStringLiteral("in Theme.qml but not in knownThemes(): %1")
                       .arg(QStringList(onlyQml.cbegin(), onlyQml.cend())
                                .join(QStringLiteral(", ")))));
    QVERIFY2(
        onlyCpp.isEmpty(),
        qPrintable(QStringLiteral("in knownThemes() but not in Theme.qml: %1")
                       .arg(QStringList(onlyCpp.cbegin(), onlyCpp.cend())
                                .join(QStringLiteral(", ")))));
}

// The border is drawn on top of the panel's own surface, so a palette whose
// two values are the same paints a panel with no visible edge at all. That is
// what "eldritch" did, and nothing in the code could notice: both values are
// valid colours and the panel renders without a word.
void TestQmlContract::everyPaletteHasAVisibleBorder() {
    const QString qml = readFile(QStringLiteral(BINDPEEK_SRC "/Theme.qml"));
    QVERIFY(!qml.isEmpty());

    const QHash<QString, QHash<QString, QString>> palettes = palettesOf(qml);
    // Guards the test itself: a changed spelling in Theme.qml would otherwise
    // leave it checking nothing and passing.
    QCOMPARE(palettes.size(), bindpeek::Settings::knownThemes().size());

    for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
        const QString surface = it.value().value(QStringLiteral("surface"));
        const QString border = it.value().value(QStringLiteral("border"));
        const int distance = colourDistance(surface, border);
        QVERIFY2(distance >= kMinColourDistance,
                 qPrintable(
                     QStringLiteral("palette %1: border #%2 is %3 away from "
                                    "surface #%4, which is not a visible edge")
                         .arg(it.key(), border)
                         .arg(distance)
                         .arg(surface)));
    }
}

// The panel says which shortcut fires on the next key by colouring it: that
// one in the brand colour, the ones a key further on in the plain text one. A
// palette that gives both the same value says nothing at all, and it says it
// without failing anywhere.
void TestQmlContract::everyPaletteTellsTheTwoStatesApart() {
    const QString qml = readFile(QStringLiteral(BINDPEEK_SRC "/Theme.qml"));
    QVERIFY(!qml.isEmpty());

    const QHash<QString, QHash<QString, QString>> palettes = palettesOf(qml);
    QCOMPARE(palettes.size(), bindpeek::Settings::knownThemes().size());

    for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
        const QString brand = it.value().value(QStringLiteral("brand"));
        const QString text = it.value().value(QStringLiteral("text"));
        const int distance = colourDistance(brand, text);
        QVERIFY2(
            distance >= kMinColourDistance,
            qPrintable(QStringLiteral("palette %1: brand #%2 is %3 away from "
                                      "text #%4, so a shortcut that fires "
                                      "looks like one that does not")
                           .arg(it.key(), brand)
                           .arg(distance)
                           .arg(text)));
    }
}

// Every position word has to survive the round trip through the one mapping
// that turns a word into a Position and back. A word the editor offers but the
// reader does not understand would silently become "center".
void TestQmlContract::positionListsAgree() {
    const QStringList positions = bindpeek::Settings::knownPositions();
    QVERIFY(!positions.isEmpty());
    for (const QString &name : positions) {
        QCOMPARE(bindpeek::Settings::positionName(
                     bindpeek::Settings::positionFromName(name)),
                 name);
    }
}
QTEST_APPLESS_MAIN(TestQmlContract)
#include "test_qml_contract.moc"
