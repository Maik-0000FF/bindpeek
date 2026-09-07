// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AppInfo.h"
#include "Appearance.h"
#include "CommandLine.h"
#include "Compositor.h"
#include "LayerPlacement.h"
#include "OverlayController.h"
#include "Settings.h"
#include "Source.h"
#include "SourceHyprland.h"
#include "WatchClient.h"
#ifdef BINDPEEK_WITH_KDE
#include "SourceKde.h"
#endif
#include "SourceMango.h"
#include "SourceSway.h"
#include "SystemScheme.h"

#include <LayerShellQt/Window>

#include <QCommandLineParser>
#include <QDir>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLockFile>
#include <QMargins>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTranslator>

#include <memory>

using namespace bindpeek;

namespace {

// Names of the environments. They double as the accepted values of
// --environment, so detection and command line can never drift apart.
constexpr char kEnvironmentMango[] = "mango";
constexpr char kEnvironmentHyprland[] = "hyprland";
constexpr char kEnvironmentKde[] = "kde";
constexpr char kEnvironmentSway[] = "sway";

// Environment variables the running session is recognized by. The signature
// variables carry more weight than XDG_CURRENT_DESKTOP because they are only
// set when the compositor is actually running.
constexpr char kVarKdeSession[] = "KDE_FULL_SESSION";
constexpr char kVarDesktop[] = "XDG_CURRENT_DESKTOP";

// Minimum width of the shortcut column in the text output. The actual width
// follows the longest shortcut, otherwise KDE's media keys ("Microphone Volume
// Down") stick to their description.
constexpr int kColumnMinWidth = 22;

// Gap between shortcut and description.
constexpr int kColumnGap = 2;

// Indent of the shortcut lines below their group.
constexpr char kIndent[] = "  ";

// Base name of the translation catalogs (bindpeek_de.qm and friends).
constexpr char kTranslationPrefix[] = "bindpeek";

// The QML entry point, compiled into the binary as a resource.
constexpr char kOverlayQml[] = "qrc:/qml/Overlay.qml";

// Names QML addresses the C++ objects by.
constexpr char kControllerName[] = "OverlayController";
constexpr char kAppearanceName[] = "Appearance";

// layer-shell namespace of the surface. The same string in all three
// environments, otherwise every compositor would need its own rule for it
// (Hyprland needs one to skip the fade-in, for example).
constexpr char kLayerShellScope[] = "bindpeek";

// Option names. Not translated: a flag must never change with the language.
constexpr char kOptionList[] = "list";
constexpr char kOptionEnvironment[] = "environment";
constexpr char kOptionSource[] = "source";
constexpr char kOptionKeys[] = "keys";

// The environments this program can read, in the order they are offered.
//
// Every text in the program that names them is built from this rather than
// spelling it again: the same list in several places is what let one of them
// fall behind once already. The documentation carries its own copies and
// cannot read this, so adding an environment means going through the pages
// that list them as well.
//
// Written out with commas and no conjunction, which is what a joined list can
// give in every language: an "or" would have to come from the catalogue and
// would put the list back into a translated sentence.
const QStringList &knownEnvironments() {
    static const QStringList names = {
        QLatin1String(kEnvironmentMango),
        QLatin1String(kEnvironmentHyprland),
        QLatin1String(kEnvironmentSway),
        QLatin1String(kEnvironmentKde),
    };
    return names;
}

// The sentences the options are described by, and the words their values go
// by, each as a function rather than a string: a string built where the table
// below stands would be built before the translators are installed, and would
// stay English for the rest of the run.
QString listDescription() {
    return QCoreApplication::translate(
        "main", "Print the shortcuts as text instead of showing the overlay.");
}

QString keysDescription() {
    return QCoreApplication::translate(
        "main", "Print the held modifiers as they change, for checking that "
                "the keyboard service is reachable.");
}

QString environmentDescription() {
    return QCoreApplication::translate("main", "Force the environment: %1.")
        .arg(knownEnvironments().join(QStringLiteral(", ")));
}

QString environmentValueName() {
    return QCoreApplication::translate("main", "name");
}

QString sourceDescription() {
    return QCoreApplication::translate(
        "main",
        "Read the shortcuts from this file instead of the one the session "
        "uses.");
}

QString sourceValueName() {
    return QCoreApplication::translate("main", "path");
}

// One option of this program.
struct ProgramOption {
    const char *name;
    // What the value goes by in --help, or nullptr when the option takes none.
    // Which of the two it is answers both questions there are about a value:
    // whether --help shows one, and whether the next argument belongs to this
    // option rather than standing on its own.
    QString (*valueName)();
    QString (*description)();
    // True when the option prints and stops, and therefore needs no display.
    bool answeredAsText;
};

// The options this program has, in the order --help lists them.
//
// A name Qt already uses is not available here: style, session, reverse,
// platform and the rest are cut out of the command line by the GUI application
// object, and an option of this program carrying one of those names would take
// it away from Qt. Nothing catches that automatically, because by then the two
// are the same word.
//
// One table because it is read from three sides: the parser is built from it,
// the check that runs before the application object asks which options are
// answered without a display, and the same check asks which are followed by a
// value it has to step over. An option added to the parser alone would be a
// value nobody knows about, and a line carrying it would build a plain
// application object for a run that goes on to put the panel on the screen.
constexpr ProgramOption kOptions[] = {
    {kOptionList, nullptr, listDescription, true},
    {kOptionKeys, nullptr, keysDescription, true},
    {kOptionEnvironment, environmentValueName, environmentDescription, false},
    {kOptionSource, sourceValueName, sourceDescription, false},
};

// The names of the options that print and stop, and of those that are followed
// by a value. Both read out of the table, so neither can fall behind it.
QStringList textOnlyOptions() {
    QStringList names;
    for (const ProgramOption &option : kOptions) {
        if (option.answeredAsText) {
            names.append(QLatin1String(option.name));
        }
    }
    return names;
}

QStringList optionsTakingValue() {
    QStringList names;
    for (const ProgramOption &option : kOptions) {
        if (option.valueName != nullptr) {
            names.append(QLatin1String(option.name));
        }
    }
    return names;
}

// Called once the translators are in, so every sentence arrives in the
// language of the session.
void addOptions(QCommandLineParser &parser) {
    for (const ProgramOption &option : kOptions) {
        QCommandLineOption added(QLatin1String(option.name),
                                 option.description());
        if (option.valueName != nullptr) {
            added.setValueName(option.valueName());
        }
        parser.addOption(added);
    }
}

// Detects the running environment. Empty string when nothing matches: then it
// is reported instead of guessed.
QString detectEnvironment() {
    const QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    if (environment.contains(QLatin1String(kHyprlandSignatureVar))) {
        return QLatin1String(kEnvironmentHyprland);
    }
    if (environment.contains(QLatin1String(kMangoSignatureVar))) {
        return QLatin1String(kEnvironmentMango);
    }
    // Either variable names a running sway; the second one alone is what i3
    // sets, and i3 is not a Wayland session, so the panel would not come up
    // there anyway. It is accepted all the same because sway sets both.
    if (environment.contains(QLatin1String(kSwaySocketVar)) ||
        environment.contains(QLatin1String(kSwaySocketVarLegacy))) {
        return QLatin1String(kEnvironmentSway);
    }
    if (environment.contains(QLatin1String(kVarKdeSession)) ||
        environment.value(QLatin1String(kVarDesktop))
            .contains(QStringLiteral("KDE"), Qt::CaseInsensitive)) {
        return QLatin1String(kEnvironmentKde);
    }
    return {};
}

// Builds the backend for an environment. nullptr when there is none yet.
std::unique_ptr<Source> makeSource(const QString &environment,
                                   const QString &path) {
    if (environment == QLatin1String(kEnvironmentMango)) {
        return std::make_unique<SourceMango>(path);
    }
    if (environment == QLatin1String(kEnvironmentHyprland)) {
        return std::make_unique<SourceHyprland>(path);
    }
    if (environment == QLatin1String(kEnvironmentSway)) {
        return std::make_unique<SourceSway>(path);
    }
#ifdef BINDPEEK_WITH_KDE
    if (environment == QLatin1String(kEnvironmentKde)) {
        return std::make_unique<SourceKde>(path);
    }
#endif
    return nullptr;
}

void printList(QTextStream &out, const QString &sourceName,
               const QList<Bind> &binds) {
    out << QCoreApplication::translate("main", "%1: %n shortcut(s)", nullptr,
                                       static_cast<int>(binds.size()))
               .arg(sourceName)
        << '\n';

    // Determine the column width once from the longest shortcut, so the whole
    // list stays flush.
    int width = kColumnMinWidth;
    for (const Bind &bind : binds) {
        width = qMax(width,
                     static_cast<int>(shortcutText(bind).size()) + kColumnGap);
    }

    const QList<BindGroup> groups = groupBinds(binds);
    for (const BindGroup &group : groups) {
        out << '\n' << group.name << '\n';
        for (const Bind &bind : group.binds) {
            out << kIndent
                << shortcutText(bind).leftJustified(width, QLatin1Char(' '))
                << bind.description << '\n';
        }
    }
}

// Prints the held modifiers as they change, and nothing else. A way to see
// whether the keyboard service is reachable and whether the modifiers arrive,
// without a display and without a compositor in the way.
int runKeyWatch(QTextStream &out, QTextStream &err) {
    auto *watch = new WatchClient(QCoreApplication::instance());
    if (!watch->start()) {
        err << QCoreApplication::translate(
                   "main", "The keyboard service is not answering at %1. It is "
                           "started by %2.")
                   .arg(WatchClient::socketPath(), WatchClient::socketUnit())
            << Qt::endl;
        return 1;
    }

    out << QCoreApplication::translate(
               "main", "Connected to the keyboard service. Ctrl+C ends it.")
        << Qt::endl;

    QObject::connect(
        watch, &WatchClient::heldChanged, [&out](const QStringList &held) {
            out << (held.isEmpty()
                        ? QStringLiteral("-")
                        : held.join(QString::fromLatin1(kShortcutSeparator)))
                << Qt::endl;
        });
    return QCoreApplication::exec();
}

// Refuses to start when another overlay already runs.
//
// Two panels on screen look like one panel drawn twice and are impossible to
// tell apart. The lock lives in the runtime directory, which the session
// clears on logout, so a crash cannot leave a stale lock behind that keeps the
// panel from ever starting again.
std::unique_ptr<QLockFile> claimSingleInstance(QTextStream &err) {
    // The path is AppInfo's, not this file's: the settings window reads the
    // same lock to find out whether a panel is running and which process it
    // is, and two spellings of it would be two answers.
    auto lock = std::make_unique<QLockFile>(AppInfo::lockPath());
    // Without a wait the answer is immediate, which is what a second start
    // wants: tell the user and step aside.
    lock->setStaleLockTime(0);
    if (lock->tryLock(0)) {
        return lock;
    }

    qint64 pid = 0;
    QString host;
    QString application;
    if (lock->getLockInfo(&pid, &host, &application)) {
        err << QCoreApplication::translate(
                   "main", "bindpeek is already running as process %1.")
                   .arg(pid)
            << Qt::endl;
    } else {
        err << QCoreApplication::translate("main",
                                           "bindpeek is already running.")
            << Qt::endl;
    }
    return {};
}

// Shows the overlay and runs the event loop. Returns the process exit code.
int runOverlay(std::unique_ptr<Source> source, QTextStream &err) {
    // Before anything is created: on a session without wlr-layer-shell the
    // panel would come up as an ordinary window with a title bar, which looks
    // like a defect rather than an unsupported setup.
    const CompositorSupport compositor = detectCompositorSupport();
    if (!compositor.supported) {
        err << compositor.message() << Qt::endl;
        return 1;
    }

    // Held for the lifetime of the process; releasing it is the destructor's
    // job, which also covers the paths that return early below.
    const std::unique_ptr<QLockFile> lock = claimSingleInstance(err);
    if (!lock) {
        return 1;
    }

    // The keyboard is read below the compositor by a service of its own, so
    // the panel can follow a held modifier without taking the key away from
    // whoever it is bound to, and without this program ever being able to read
    // a keystroke.
    auto *watch = new WatchClient(QCoreApplication::instance());
    if (!watch->start()) {
        err << QCoreApplication::translate(
                   "main", "The keyboard service is not answering at %1. It is "
                           "started by %2.")
                   .arg(WatchClient::socketPath(), WatchClient::socketUnit())
            << QChar(QLatin1Char(0x0A));
        return 1;
    }

    // Settings first: a template is written on the very first run so the
    // options are discoverable, and anything unusable in the file is reported
    // rather than silently replaced.
    Settings::writeTemplateIfMissing();
    Settings settings;
    for (const QString &warning : settings.warnings()) {
        err << QCoreApplication::translate("main", "Note: %1").arg(warning)
            << Qt::endl;
    }

    auto controller = std::make_unique<OverlayController>(
        std::move(source), watch, settings.showDelayMs());
    controller->setShowsDeeper(settings.showsDeeper());
    controller->setIgnoreLoneShift(settings.ignoreLoneShift());
    controller->setArrangesByModifier(settings.arrangesByModifier());
    if (!controller->reload()) {
        err << controller->message() << QChar(QLatin1Char(0x0A));
        return 1;
    }

    // One scheme reader for the process; Appearance and anything else share it.
    auto *systemScheme = new SystemScheme(QCoreApplication::instance());
    auto appearance = std::make_unique<Appearance>(settings, systemScheme);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QLatin1String(kControllerName),
                                             controller.get());
    engine.rootContext()->setContextProperty(QLatin1String(kAppearanceName),
                                             appearance.get());
    engine.load(QUrl(QLatin1String(kOverlayQml)));
    if (engine.rootObjects().isEmpty()) {
        err << QCoreApplication::translate("main",
                                           "The overlay could not be loaded.")
            << '\n';
        return 1;
    }

    auto *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        err << QCoreApplication::translate("main",
                                           "The overlay could not be loaded.")
            << '\n';
        return 1;
    }

    // Configured before the surface is first mapped, and the three settings
    // below are not alike in how late they could be changed.
    //
    // The layer, the keyboard interactivity, and the anchors and margins that
    // applyPlacement sets are double-buffered state: a change to any of them
    // reaches the compositor with the next commit of the surface, which is
    // what lets the settings file be followed while the panel is up.
    //
    // The scope is not state at all. It is an argument the surface is created
    // with, and the protocol has no request to change it, so this is the only
    // place it can be set. A later change to it does not arrive late, it does
    // not arrive.
    if (auto *layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        // Never take the keyboard. The panel is a bystander: the modifiers
        // arrive from the keyboard service, and every key stays with the
        // compositor so the shortcut on screen actually fires when it is
        // pressed.
        layerWindow->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setScope(QLatin1String(kLayerShellScope));
        // Anchors and margins from the position setting. This is the only
        // surface a compositor places; the preview in the settings window is
        // an item inside that window and is laid out there.
        applyPlacement(layerWindow, settings);
    }
    // Watch the settings file so the editor's live saving reaches a running
    // panel. Everything that can change without a restart is applied here;
    // the file is re-added afterwards because an editor that replaces it
    // rather than rewriting it would otherwise be watched no more.
    auto *watcher = new QFileSystemWatcher(QCoreApplication::instance());
    watcher->addPath(Settings::defaultPath());
    QObject::connect(
        watcher, &QFileSystemWatcher::fileChanged, controller.get(),
        [watcher, &controller, &appearance, window]() {
            const Settings fresh;
            controller->setShowDelayMs(fresh.showDelayMs());
            controller->setShowsDeeper(fresh.showsDeeper());
            controller->setIgnoreLoneShift(fresh.ignoreLoneShift());
            controller->setArrangesByModifier(fresh.arrangesByModifier());
            appearance->apply(fresh);

            if (auto *layer = LayerShellQt::Window::get(window)) {
                applyPlacement(layer, fresh);
                // What was just set is double-buffered: the layer-shell
                // protocol applies anchors and margins at the next commit of
                // the surface, and Qt commits when it draws. A panel that is
                // on screen while this arrives need not be drawing anything,
                // and the distance kept at the two ends is exactly such a
                // case: along the edge the panel spans, its extent comes back
                // from the compositor rather than out of the scene, so nothing
                // here changes, no frame is drawn, and the new margin sits on
                // the wire until some later change happens to paint one.
                // Measured on the wire: without this the request goes out and
                // nothing follows it; with it the compositor answers with the
                // new width two milliseconds later.
                window->requestUpdate();
            }

            // The new placement is not demonstrated here. The panel appears
            // on the next held modifier, which is a keystroke away and shows
            // the real thing rather than a rehearsal of it.
            if (!watcher->files().contains(Settings::defaultPath())) {
                watcher->addPath(Settings::defaultPath());
            }
        });

    // No show() here: the window follows OverlayController.panelVisible, which
    // is false until a modifier has been held long enough.
    return QGuiApplication::exec();
}

// Loads the catalog for the user's locale, plus Qt's own one for the strings
// QCommandLineParser prints. A missing catalog is not an error: the source
// strings are English and stay readable.
void installTranslators() {
    static QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"),
                          QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }

    static QTranslator appTranslator;
    if (appTranslator.load(QLocale(), QLatin1String(kTranslationPrefix),
                           QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&appTranslator);
    }
}

} // namespace

int main(int argc, char **argv) {
    // Qt caches compiled QML under ~/.cache and decides the cache is still
    // current by comparing the timestamp of the executable. Every file in the
    // Nix store carries the epoch as its timestamp, so that comparison never
    // reports a change: a rebuilt panel would keep running the QML of a build
    // that is long gone, and fail against properties that no longer exist.
    // Recompiling two small files on every start costs nothing next to that.
    qputenv("QML_DISABLE_DISK_CACHE", "1");

    // Asked here so that --list, --keys and the two informational options stay
    // usable over SSH: a QGuiApplication aborts where there is no display, and
    // none of the four needs one.
    //
    // The options followed by a value are named as well, because the check has
    // to step over what follows them: a file called "-v" handed to --source is
    // a file, and reading it as a request for the version would build a plain
    // application object and then go on to put the panel on the screen with it.
    //
    // Both lists come out of the one table the parser is built from, so an
    // option cannot reach the parser without reaching this line as well.
    const bool textOnly =
        wantsTextOnly(argc, argv, textOnlyOptions(), optionsTakingValue());

    std::unique_ptr<QCoreApplication> app;
    if (textOnly) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        app = std::make_unique<QGuiApplication>(argc, argv);
    }

    setApplicationIdentity();
    installTranslators();

    QCommandLineParser parser;
    prepareParser(parser, applicationDescription());

    addOptions(parser);
    parser.process(*app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    if (parser.isSet(QLatin1String(kOptionKeys))) {
        return runKeyWatch(out, err);
    }

    const QLatin1String environmentOption(kOptionEnvironment);
    const QString environment = parser.isSet(environmentOption)
                                    ? parser.value(environmentOption).toLower()
                                    : detectEnvironment();
    if (environment.isEmpty()) {
        err << QCoreApplication::translate(
                   "main", "No supported environment detected. Force one with "
                           "--environment %1.")
                   .arg(knownEnvironments().join(QLatin1Char('|')))
            << '\n';
        return 1;
    }

    if (!knownEnvironments().contains(environment)) {
        err << QCoreApplication::translate(
                   "main", "Unknown environment \"%1\". Allowed: %2.")
                   .arg(environment,
                        knownEnvironments().join(QStringLiteral(", ")))
            << '\n';
        return 1;
    }

    auto source =
        makeSource(environment, parser.value(QLatin1String(kOptionSource)));
    if (!source) {
#ifndef BINDPEEK_WITH_KDE
        // Said apart from the sentence below, which would be a lie here: the
        // backend exists, this build simply has not got it. What it needs is
        // named, because the answer is to install one package and build again.
        if (environment == QLatin1String(kEnvironmentKde)) {
            err << QCoreApplication::translate(
                       "main",
                       "This build has no KDE backend. It was built without "
                       "KDE's KConfig framework, which is what reads the "
                       "shortcut file the way KDE writes it.")
                << '\n';
            return 1;
        }
#endif
        err << QCoreApplication::translate("main",
                                           "There is no backend for %1 yet.")
                   .arg(environment)
            << '\n';
        return 1;
    }

    if (!parser.isSet(QLatin1String(kOptionList))) {
        return runOverlay(std::move(source), err);
    }

    QString error;
    const QList<Bind> binds = source->read(&error);
    if (binds.isEmpty()) {
        err << error << '\n';
        return 1;
    }
    if (!error.isEmpty()) {
        err << QCoreApplication::translate("main", "Note: %1").arg(error)
            << '\n';
    }

    printList(out, source->name(), binds);
    return 0;
}
