// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures Settings against the input table that sits next to its constructor,
// including the values a typo produces. Every case writes its own file into a
// temporary directory, so the real configuration is never touched.

#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace bindpeek;

namespace {

// The default the table below is measured against. Kept here rather than
// reaching into the implementation, so a changed default shows up as a failing
// test instead of a silently passing one.
constexpr int kExpectedDefault = 500;

// Writes a settings file with one line and returns its path.
QString writeFile(const QTemporaryDir &dir, const QString &content) {
    QString path = dir.filePath(QStringLiteral("bindpeek.conf"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream stream(&file);
    stream << content;
    return path;
}

} // namespace

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void missingFileUsesDefault();
    void readsValue_data();
    void readsValue();
    void writesTemplateOnlyOnce();
    void templateIsReadBackAsTheDefault();
    void saveKeepsComments();
    void saveRoundTripsEveryValue();
    void saveAppendsMissingKeys();
};

void TestSettings::missingFileUsesDefault() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const Settings settings(
        dir.filePath(QStringLiteral("does-not-exist.conf")));
    QCOMPARE(settings.showDelayMs(), kExpectedDefault);
    // A missing file is the normal case, not something to complain about.
    QVERIFY(settings.warnings().isEmpty());
}

void TestSettings::readsValue_data() {
    QTest::addColumn<QString>("content");
    QTest::addColumn<int>("expected");
    QTest::addColumn<bool>("warns");

    QTest::newRow("default value") << "showDelayMs=500\n" << 500 << false;
    QTest::newRow("zero shows at once") << "showDelayMs=0\n" << 0 << false;
    QTest::newRow("upper bound") << "showDelayMs=5000\n" << 5000 << false;
    QTest::newRow("in range") << "showDelayMs=1200\n" << 1200 << false;
    QTest::newRow("surrounding blanks") << "showDelayMs= 450 \n"
                                        << 450 << false;
    QTest::newRow("above range") << "showDelayMs=99999\n"
                                 << kExpectedDefault << true;
    QTest::newRow("negative") << "showDelayMs=-50\n"
                              << kExpectedDefault << true;
    QTest::newRow("not a number") << "showDelayMs=abc\n"
                                  << kExpectedDefault << true;
    QTest::newRow("empty value") << "showDelayMs=\n"
                                 << kExpectedDefault << true;
    QTest::newRow("not an integer") << "showDelayMs=300.7\n"
                                    << kExpectedDefault << true;
    // A comma is a list separator to QSettings, so this must not be mistaken
    // for an empty line; the complaint has to name the value.
    QTest::newRow("comma instead of point") << "showDelayMs=1,5\n"
                                            << kExpectedDefault << true;
    QTest::newRow("key absent") << "# nothing here\n"
                                << kExpectedDefault << false;
    QTest::newRow("unknown key") << "somethingElse=7\n"
                                 << kExpectedDefault << false;
}

void TestSettings::readsValue() {
    QFETCH(QString, content);
    QFETCH(int, expected);
    QFETCH(bool, warns);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(dir, content);
    QVERIFY(!path.isEmpty());

    const Settings settings(path);
    QCOMPARE(settings.showDelayMs(), expected);
    QCOMPARE(!settings.warnings().isEmpty(), warns);
}

void TestSettings::writesTemplateOnlyOnce() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sub/bindpeek.conf"));

    // Creates the directory as well as the file.
    QVERIFY(Settings::writeTemplateIfMissing(path));
    QVERIFY(QFile::exists(path));

    // An existing file is never touched, so a hand-edited setting survives.
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("showDelayMs=1234\n");
    file.close();

    QVERIFY(Settings::writeTemplateIfMissing(path));
    QCOMPARE(Settings(path).showDelayMs(), 1234);
}

void TestSettings::templateIsReadBackAsTheDefault() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bindpeek.conf"));

    QVERIFY(Settings::writeTemplateIfMissing(path));

    // The template must describe the behaviour it produces: reading it back
    // has to give the default, and must not trip the validation.
    const Settings settings(path);
    QCOMPARE(settings.showDelayMs(), kExpectedDefault);
    QVERIFY(settings.warnings().isEmpty());
}

// The point of the hand-rolled writer: QSettings would drop every comment, and
// the comments are what make the file editable without documentation.
void TestSettings::saveKeepsComments() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bindpeek.conf"));
    QVERIFY(Settings::writeTemplateIfMissing(path));

    const QRegularExpression commentLine(QStringLiteral("^#"));

    QFile before(path);
    QVERIFY(before.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList commentsBefore = QString::fromUtf8(before.readAll())
                                           .split(QLatin1Char('\n'))
                                           .filter(commentLine);
    before.close();
    QVERIFY(!commentsBefore.isEmpty());

    Settings settings(path);
    settings.setShowDelayMs(777);
    QVERIFY(settings.save(path));

    QFile after(path);
    QVERIFY(after.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(after.readAll());

    QCOMPARE(text.split(QLatin1Char('\n')).filter(commentLine), commentsBefore);
    QVERIFY(text.contains(QStringLiteral("showDelayMs=777")));
}

void TestSettings::saveRoundTripsEveryValue() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bindpeek.conf"));
    QVERIFY(Settings::writeTemplateIfMissing(path));

    Settings written(path);
    written.setShowDelayMs(120);
    written.setPosition(Settings::Position::Bottom);
    written.setMarginPx(40);
    written.setEdgeInsetPx(12);
    written.setOverlayEnabled(false);
    written.setDisclosure(QStringLiteral("sections"));
    written.setArrangement(QStringLiteral("modifiers"));
    written.setAlignment(QStringLiteral("end"));
    written.setIgnoreLoneShift(false);
    written.setTheme(QStringLiteral("contrast"));
    // Both of these are set away from their default on purpose: a round trip
    // that writes the default proves nothing, it holds just as well when the
    // key is not written at all.
    written.setFollowSystemScheme(true);
    written.setThemeLight(QStringLiteral("solarized-light"));
    written.setThemeDark(QStringLiteral("nord"));
    written.setFontFamily(QStringLiteral("DejaVu Sans"));
    written.setFontSizePt(20);
    written.setCornerRadiusPx(0);
    written.setBorderWidthPx(2);
    written.setOpacity(0.5);
    QVERIFY(written.save(path));

    // Everything has to come back unchanged and without a complaint: what the
    // editor writes must be exactly what the overlay accepts.
    const Settings read(path);
    QVERIFY2(read.warnings().isEmpty(),
             qPrintable(read.warnings().join(QStringLiteral("; "))));
    QCOMPARE(read.showDelayMs(), 120);
    QCOMPARE(read.position(), Settings::Position::Bottom);
    QCOMPARE(read.marginPx(), 40);
    QCOMPARE(read.edgeInsetPx(), 12);
    QCOMPARE(read.overlayEnabled(), false);
    QCOMPARE(read.disclosure(), QStringLiteral("sections"));
    QCOMPARE(read.arrangement(), QStringLiteral("modifiers"));
    // Asked as the panel asks it, for the reason given at the alignment below.
    QCOMPARE(read.arrangesByModifier(), true);
    QCOMPARE(read.alignment(), QStringLiteral("end"));
    // The two questions the word is asked as, which is how everything else
    // reads it: a word that survives the round trip and answers the wrong
    // question would still lose the setting.
    QCOMPARE(read.alignsAtStart(), false);
    QCOMPARE(read.alignsAtEnd(), true);
    QCOMPARE(read.ignoreLoneShift(), false);
    QCOMPARE(read.theme(), QStringLiteral("contrast"));
    QCOMPARE(read.followSystemScheme(), true);
    QCOMPARE(read.themeLight(), QStringLiteral("solarized-light"));
    QCOMPARE(read.themeDark(), QStringLiteral("nord"));
    QCOMPARE(read.fontFamily(), QStringLiteral("DejaVu Sans"));
    QCOMPARE(read.fontSizePt(), 20);
    QCOMPARE(read.cornerRadiusPx(), 0);
    QCOMPARE(read.borderWidthPx(), 2);
    QCOMPARE(read.opacity(), 0.5);
}

void TestSettings::saveAppendsMissingKeys() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A file someone pared down to a single line still has to survive being
    // saved from the editor, notes included.
    const QString path =
        writeFile(dir, QStringLiteral("# my notes\nshowDelayMs=200\n"));
    QVERIFY(!path.isEmpty());

    Settings settings(path);
    settings.setOpacity(0.4);
    QVERIFY(settings.save(path));

    const Settings read(path);
    QCOMPARE(read.showDelayMs(), 200);
    QCOMPARE(read.opacity(), 0.4);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(file.readAll())
                .contains(QStringLiteral("# my notes")));
}
QTEST_APPLESS_MAIN(TestSettings)
#include "test_settings.moc"
