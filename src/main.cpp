#include <QGuiApplication>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include "backend.h"
#include "theme.h"
#include "translator.h"

static int commandLine(const QStringList &args) {
    if (args.first() == "skill") {
        if (args.size() < 2 || args[1] != "install") return 1;
        const QString directory = QDir::homePath() + "/.codex/skills/webmfix";
        if (!QDir().mkpath(directory)) return 1;
        QFile source(":/agent-skill.txt");
        QSaveFile target(directory + "/SKILL.md");
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) return 1;
        target.write(source.readAll());
        return target.commit() ? 0 : 1;
    }
    QTemporaryDir temp;
    for (const auto &name : {QString("repair.py"), QString("worker.py")})
        if (!QFile::copy(":/" + name, temp.filePath(name))) return 1;
    QStringList files;
    QString output;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--json") continue;
        if (args[i] == "--output" && i + 1 < args.size()) output = args[++i];
        else if (args[i].startsWith("--")) { fprintf(stderr, "Unknown option\n"); return 1; }
        else files << QFileInfo(args[i]).absoluteFilePath();
    }
    if (files.isEmpty()) { fprintf(stderr, "Usage: webmfix check|repair VIDEO.webm [--output FOLDER] --json\n"); return 1; }
    QJsonArray records;
    bool failed = false;
    for (const auto &file : files) {
        QProcess process;
        process.start("python3", {temp.filePath("worker.py"), args.first(), file, output});
        if (!process.waitForStarted()) { fprintf(stderr, "Could not start Python 3\n"); return 1; }
        process.waitForFinished(-1);
        const auto bytes = process.readAllStandardOutput();
        const auto result = QJsonDocument::fromJson(bytes);
        if (!result.isObject()) { fprintf(stderr, "Invalid repair engine response\n"); return 1; }
        records.append(result.object());
        if (process.exitCode() != 0) {
            failed = true;
            fprintf(stderr, "%s\n", qPrintable(result.object().value("motivo").toString()));
        }
    }
    const auto bytes = QJsonDocument(records).toJson();
    fwrite(bytes.constData(), 1, bytes.size(), stdout);
    return failed ? 1 : 0;
}

int main(int argc, char *argv[]) {
    QStringList raw;
    for (int i = 1; i < argc; ++i) raw << QString::fromLocal8Bit(argv[i]);
    const bool cli = !raw.isEmpty() && (raw.first() == "check" || raw.first() == "repair" || raw.first() == "skill");
    if (cli || raw.contains("--screenshot") || raw.contains("--help") || raw.contains("--version")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("webmfix");
    app.setApplicationVersion("1.1.0");
    app.setDesktopFileName("webmfix");
    app.setWindowIcon(QIcon(":/webmfix.svg"));
    if (cli) return commandLine(raw);
    if (raw.contains("--version")) { puts("webmfix 1.1.0"); return 0; }
    if (raw.contains("--help")) {
        puts("webmfix [open] [VIDEO.webm ...]\nwebmfix check|repair VIDEO.webm [--output FOLDER] --json\nwebmfix skill install");
        return 0;
    }
    QQuickStyle::setStyle("Material");
    QTemporaryDir localeDir;
    QString languageFile;
    const int languageIndex = raw.indexOf("--language");
    if (languageIndex >= 0 && languageIndex + 1 < raw.size()) {
        languageFile = localeDir.filePath("language");
        QFile file(languageFile);
        if (!file.open(QIODevice::WriteOnly)) return 1;
        file.write(raw[languageIndex + 1].toUtf8());
    }
    Translator translator(languageFile);
    app.installTranslator(&translator);
    Theme theme(raw.contains("--default-theme") ? localeDir.filePath("neutral") : QString());
    PortalFilePicker picker;
    Backend backend(&picker);
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    if (raw.contains("--demo")) backend.demo();
    else {
        QStringList files;
        for (const auto &arg : raw) if (QFileInfo(arg).isFile()) files << arg;
        if (!files.isEmpty()) backend.addPaths(files);
    }
    const int captureIndex = raw.indexOf("--screenshot");
    if (captureIndex >= 0 && captureIndex + 1 < raw.size()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTimer::singleShot(700, &app, [window, &app, raw, captureIndex] {
            app.exit(window->grabWindow().save(raw[captureIndex + 1]) ? 0 : 1);
        });
    }
    return app.exec();
}
