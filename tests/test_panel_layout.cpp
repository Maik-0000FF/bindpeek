// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the panel itself, which no other test does.
//
// qmllint sees types and imports, and the contract test compares names. What
// neither can see is a column that came out a fraction too narrow for its own
// text, or a line that says what did not fit while the type is still being
// stepped down. Both were shipped once; this holds them.
//
// Nothing here compares pixels against numbers written down. The panel is set
// in whichever family the machine happens to have, and the four distributions
// this is built on do not agree on one, so every measurement is held against
// another measurement taken in the same run.

#include "Appearance.h"
#include "Settings.h"
#include "SystemScheme.h"

#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

using namespace bindpeek;

namespace {

// The size the panel is asked for, which is the largest the setting allows.
// Picked so the fitting has somewhere to come down from: at the default the
// bounds below would be met without a single step, and the test would pass
// without ever exercising what it is here for.
constexpr int kAskedFontSizePt = 48;

// How long the fitting is given before the test calls it stuck.
//
// Far over the second or so it takes here, and deliberately so: the same
// steps run again under a sanitizer, where every layout costs several times
// more, and a test that fails on a busy afternoon is worse than no test. A
// fitting that does come to rest never waits this out, only one that is
// genuinely stuck does.
constexpr int kFitBudgetMs = 60000;

// The gap between two looks at the panel. Half a round, so a round cannot pass
// unseen between two samples.
constexpr int kSampleMs = 8;

// Everything under an item that carries the name, gathered down the visual
// tree rather than the object tree.
//
// A repeater hands its delegates to the item they are drawn in but not to it
// as children, so findChildren walks straight past every row of the table and
// answers with the handful of items the file itself declares.
void gather(QQuickItem *item, const QString &name, QList<QQuickItem *> &found) {
    if (item == nullptr) {
        return;
    }
    if (item->objectName() == name) {
        found.append(item);
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        gather(child, name, found);
    }
}

QList<QQuickItem *> itemsNamed(QQuickItem *root, const QString &name) {
    QList<QQuickItem *> found;
    gather(root, name, found);
    return found;
}

QVariantMap entry(const QString &shortcut, const QString &description) {
    return QVariantMap{{"shortcut", shortcut},       {"key", shortcut},
                       {"description", description}, {"deeper", false},
                       {"section", QString()},       {"caps", QVariantList()}};
}

// One group per shortcut, which is what puts each of them in the place the
// column is measured from: the widest of its own group.
QVariantList groupsOf(const QStringList &shortcuts) {
    QVariantList groups;
    for (int i = 0; i < shortcuts.size(); ++i) {
        groups.append(QVariantMap{
            {"name", QStringLiteral("Group %1").arg(i)},
            {"entries", QVariantList{entry(shortcuts.at(i),
                                           QStringLiteral("Some action"))}}});
    }
    return groups;
}

QVariantList manyGroups(int count, int rows) {
    QVariantList groups;
    for (int g = 0; g < count; ++g) {
        QVariantList entries;
        for (int r = 0; r < rows; ++r) {
            entries.append(
                entry(QStringLiteral("SUPER+SHIFT+%1").arg(QChar('A' + r % 26)),
                      QStringLiteral("Action number %1").arg(r)));
        }
        groups.append(QVariantMap{{"name", QStringLiteral("Group %1").arg(g)},
                                  {"entries", entries}});
    }
    return groups;
}

} // namespace

// What the panel is measured against, in one object.
//
// Every input reaches the panel as a binding from here rather than as a value
// written into the item afterwards, and the whole of it stands before the
// document is built. Measured, that is the difference between a panel that
// lays out its groups and one that counts them and creates nothing: what a
// panel is given after it was completed at no size does not bring it back.
class Bench : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList groups MEMBER m_groups CONSTANT)
    Q_PROPERTY(int maxWidth MEMBER m_maxWidth CONSTANT)
    Q_PROPERTY(int maxHeight MEMBER m_maxHeight CONSTANT)
    Q_PROPERTY(bool fitsToBounds MEMBER m_fitsToBounds CONSTANT)
    Q_PROPERTY(bool showing MEMBER m_showing CONSTANT)

public:
    Bench(QVariantList groups, int maxWidth, int maxHeight, bool fitsToBounds,
          bool showing)
        : m_groups(std::move(groups)), m_maxWidth(maxWidth),
          m_maxHeight(maxHeight), m_fitsToBounds(fitsToBounds),
          m_showing(showing) {}

private:
    QVariantList m_groups;
    int m_maxWidth;
    int m_maxHeight;
    bool m_fitsToBounds;
    bool m_showing;
};

namespace {

// The panel is put inside a document of its own rather than loaded as one.
//
// That is how both places that draw it do it, the overlay and the preview in
// the settings window. The host carries a size of its own, because a panel
// filling a host of nothing is a panel of nothing.
constexpr char kHost[] = R"(
import QtQuick

Item {
    width: Bench.maxWidth
    height: Bench.maxHeight

    Theme {
        id: hostTheme
    }

    PanelBody {
        objectName: "panel"
        anchors.fill: parent
        theme: hostTheme
        fitsToBounds: Bench.fitsToBounds
        showing: Bench.showing
        maxWidth: Bench.maxWidth
        maxHeight: Bench.maxHeight
        heldText: "SUPER"
        groups: Bench.groups
    }
}
)";

} // namespace

class TestPanelLayout : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void shortcutColumnHoldsItsText();
    void warningWaitsForTheFit_data();
    void warningWaitsForTheFit();

private:
    // Puts a panel measured against the bench on an offscreen window. Returns
    // the panel, or nullptr with the failure already reported.
    QQuickItem *showPanel(QQuickView &view, Bench &bench);

    QTemporaryDir m_home;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SystemScheme> m_scheme;
    std::unique_ptr<Appearance> m_appearance;
};

void TestPanelLayout::initTestCase() {
    QVERIFY(m_home.isValid());

    // A settings file of its own, so the test measures the size it asked for
    // and never touches the one belonging to whoever runs it.
    const QString path = m_home.filePath(QStringLiteral("bindpeek.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QByteArrayLiteral("fontSizePt=") +
               QByteArray::number(kAskedFontSizePt) + "\n");
    file.close();

    m_settings = std::make_unique<Settings>(path);
    QCOMPARE(m_settings->fontSizePt(), kAskedFontSizePt);

    m_scheme = std::make_unique<SystemScheme>();
    m_appearance = std::make_unique<Appearance>(*m_settings, m_scheme.get());
}

QQuickItem *TestPanelLayout::showPanel(QQuickView &view, Bench &bench) {
    view.engine()->rootContext()->setContextProperty(
        QStringLiteral("Appearance"), m_appearance.get());
    view.engine()->rootContext()->setContextProperty(QStringLiteral("Bench"),
                                                     &bench);

    // Based inside the source directory, which is what lets the document below
    // name PanelBody and Theme without an import: they are its neighbours.
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(BINDPEEK_SRC) +
                                          QStringLiteral("/bench.qml"));
    auto *host = new QQmlComponent(view.engine(), &view);
    host->setData(QByteArray(kHost), base);
    if (host->isError()) {
        QTest::qFail(qPrintable(host->errorString()), __FILE__, __LINE__);
        return nullptr;
    }

    QObject *root = host->create(view.rootContext());
    if (root == nullptr) {
        QTest::qFail("the host document made nothing", __FILE__, __LINE__);
        return nullptr;
    }
    view.setContent(base, host, root);

    // Shown, because a window that is never shown produces no frame, and
    // without a frame nothing is laid out and every width below reads zero.
    // The platform is offscreen, so this puts nothing in front of anybody.
    view.show();
    if (!QTest::qWaitForWindowExposed(&view)) {
        QTest::qFail("the window never came up", __FILE__, __LINE__);
        return nullptr;
    }

    auto *panel = root->findChild<QQuickItem *>(QStringLiteral("panel"));
    if (panel == nullptr) {
        QTest::qFail("no panel in the host document", __FILE__, __LINE__);
    }
    return panel;
}

// A shortcut narrower than the column's cap is drawn whole.
//
// It used to be measured from the ink the text puts down rather than the
// advance it lays out to, which left the column a fraction too narrow for the
// very string it was measured from, and that string then lost its last
// character to the elide.
void TestPanelLayout::shortcutColumnHoldsItsText() {
    // A spread of lengths rather than one, because whether a string lands
    // short of the cap depends on the family the machine offers.
    const QStringList shortcuts{
        QStringLiteral("SUPER+A"),         QStringLiteral("SUPER+Left"),
        QStringLiteral("SUPER+Right"),     QStringLiteral("CTRL+ALT+Del"),
        QStringLiteral("SUPER+SHIFT+A"),   QStringLiteral("SUPER+SHIFT+Up"),
        QStringLiteral("CTRL+SHIFT+Left"), QStringLiteral("SUPER+ALT+Enter")};

    // The fitting is off: this asks about the column, not about the fitting,
    // and a panel that lowered its type would measure a size nobody
    // configured. The bound is wide enough that nothing has to wrap.
    Bench bench(groupsOf(shortcuts), 3000, 2000, false, false);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench);
    QVERIFY(panel != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const double cap = theme->property("columnShortcut").toDouble();
    QVERIFY(cap > 0);

    const QList<QQuickItem *> texts =
        itemsNamed(panel, QStringLiteral("shortcutText"));
    QCOMPARE(texts.size(), shortcuts.size());

    int held = 0;
    for (QQuickItem *text : texts) {
        const QString what = text->property("text").toString();
        const double wants = text->property("implicitWidth").toDouble();
        QVERIFY2(wants > 0,
                 qPrintable(what + QStringLiteral(" measured zero")));

        // Past the cap it is elided on purpose, and that is the one case this
        // says nothing about.
        if (wants > cap) {
            continue;
        }
        ++held;
        QVERIFY2(!text->property("truncated").toBool(),
                 qPrintable(QStringLiteral("%1 fits the column (%2 of %3) and "
                                           "was cut off anyway")
                                .arg(what)
                                .arg(wants)
                                .arg(cap)));
    }

    // Every string past the cap would leave nothing to check, and a test that
    // checks nothing passes for the wrong reason.
    QVERIFY2(held > 0, "no shortcut landed short of the cap");
}

void TestPanelLayout::warningWaitsForTheFit_data() {
    QTest::addColumn<QVariantList>("groups");
    QTest::addColumn<int>("maxWidth");
    QTest::addColumn<int>("maxHeight");
    QTest::addColumn<bool>("standsAtRest");

    QTest::newRow("fits once the type has come down")
        << manyGroups(6, 14) << 1400 << 800 << false;
    // Small lists against a small bound rather than a huge list against a
    // large one: the stepping is what takes the time, one point a round from
    // the size asked for down to the floor, and every row of every group is
    // laid out again on each of them. A list eight times the size answers the
    // same question and takes eight times as long to do it.
    QTest::newRow("does not fit even at the floor")
        << manyGroups(12, 12) << 320 << 160 << true;
}

// The line at the foot waits for the fitting.
//
// While a size is being stepped towards, the rows overflow by definition, so
// the line used to stand for as long as the fitting took and ask for a
// modifier that nothing needed.
void TestPanelLayout::warningWaitsForTheFit() {
    QFETCH(QVariantList, groups);
    QFETCH(int, maxWidth);
    QFETCH(int, maxHeight);
    QFETCH(bool, standsAtRest);

    // Shown and bounded, which is what chooses stepping in plain view over
    // searching out of sight.
    Bench bench(groups, maxWidth, maxHeight, true, true);
    QQuickView view;
    QQuickItem *panel = showPanel(view, bench);
    QVERIFY(panel != nullptr);

    QObject *theme = panel->property("theme").value<QObject *>();
    QVERIFY(theme != nullptr);
    const QList<QQuickItem *> warnings =
        itemsNamed(panel, QStringLiteral("overflowWarning"));
    QCOMPARE(warnings.size(), 1);
    QQuickItem *warning = warnings.first();

    // The groups are there to be measured, so what follows is about the
    // fitting and not about an empty panel.
    QVERIFY(!itemsNamed(panel, QStringLiteral("shortcutText")).isEmpty());

    QElapsedTimer clock;
    clock.start();
    int stoodTooEarly = 0;
    bool settled = false;
    while (clock.elapsed() < kFitBudgetMs) {
        QTest::qWait(kSampleMs);
        settled = panel->property("fitSettled").toBool();
        if (!settled && warning->property("visible").toBool()) {
            ++stoodTooEarly;
        }
        if (settled) {
            break;
        }
    }

    QVERIFY2(settled, "the fitting never came to rest");
    QCOMPARE(stoodTooEarly, 0);

    // The type actually came down, so the samples above looked at a fitting
    // that ran rather than at one that was over before it began.
    const int asked = theme->property("configuredFontSizePt").toInt();
    const int settledAt = theme->property("fontSizePt").toInt();
    QCOMPARE(asked, kAskedFontSizePt);
    QVERIFY2(settledAt < asked, "nothing was stepped down");

    QCOMPARE(warning->property("visible").toBool(), standsAtRest);
    if (standsAtRest) {
        QCOMPARE(settledAt, theme->property("minFontSizePt").toInt());
    }
}

QTEST_MAIN(TestPanelLayout)

#include "test_panel_layout.moc"
