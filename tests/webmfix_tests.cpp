#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include "backend.h"
#include "theme.h"
#include "translator.h"

class FakePicker : public FilePicker {
    Q_OBJECT
public:
    int calls = 0;
    QString purpose;
    void open(const QString &p, const QString &, bool) override { ++calls; purpose = p; }
    void select(const QString &p, const QStringList &files) { emit selected(p, files); }
};

class WebmfixTests : public QObject {
    Q_OBJECT
    QString fixture(const QString &name) { return QDir::current().filePath("fixtures/" + name); }
    static bool write(const QString &path, const QByteArray &data) {
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
private slots:
    void initTestCase() { QQuickStyle::setStyle("Material"); QVERIFY(QFile::exists(fixture("broken.webm"))); }
    void duplicatesAndHealthyMedia() {
        FakePicker picker; Backend backend(&picker);
        backend.addPaths({fixture("healthy.webm"), fixture("healthy.webm")});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000);
        QCOMPARE(backend.count(), 1);
        QCOMPARE(backend.data(backend.index(0), Backend::StateRole).toInt(), int(Backend::Unneeded));
        backend.addUrls({QUrl::fromLocalFile(fixture("healthy.webm"))});
        QCOMPARE(backend.count(), 1);
    }
    void errorsDoNotBlockOtherFiles() {
        QTemporaryDir dir;
        const auto bad = dir.filePath("invalid.webm"); QVERIFY(write(bad, "not a video"));
        FakePicker picker; Backend backend(&picker);
        backend.addPaths({bad, fixture("broken.webm")});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000);
        QCOMPARE(backend.readyCount(), 1);
        QCOMPARE(backend.data(backend.index(0), Backend::StateRole).toInt(), int(Backend::Error));
        QVERIFY(!backend.data(backend.index(0), Backend::DetailRole).toString().isEmpty());
    }
    void closingWaitsForTheCurrentFile() {
        FakePicker picker; Backend backend(&picker); QSignalSpy close(&backend, &Backend::mayClose);
        backend.addPaths({fixture("healthy.webm"), fixture("broken.webm")});
        QVERIFY(backend.busy()); backend.closeAfterCurrent();
        QTRY_COMPARE_WITH_TIMEOUT(close.count(), 1, 5000);
        QVERIFY(!backend.busy());
        QCOMPARE(backend.data(backend.index(1), Backend::StateRole).toInt(), int(Backend::Waiting));
    }
    void portalSelectionsChooseTheCorrectAction() {
        FakePicker picker; Backend backend(&picker);
        backend.chooseDestination(); QCOMPARE(picker.purpose, QString("destination"));
        picker.select("destination", {"/tmp/output"}); QCOMPARE(backend.destination(), QString("/tmp/output"));
        backend.chooseFiles(); QCOMPARE(picker.purpose, QString("files"));
        picker.select("files", {fixture("healthy.webm")});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000);
        QCOMPARE(backend.count(), 1);
    }
    void themeAndLanguageFollowFiles() {
        QTemporaryDir dir; QDir(dir.path()).mkpath("theme");
        const auto colors = dir.filePath("theme/colors.toml");
        QVERIFY(write(colors, "accent = '#112233'\n"));
        Theme theme(dir.path()); QCOMPARE(theme.accent(), QString("#112233"));
        QVERIFY(write(colors, "accent = '#abcdef'\n"));
        QTRY_COMPARE(theme.accent(), QString("#abcdef"));
        const auto language = dir.filePath("language"); QVERIFY(write(language, "pt\n"));
        Translator translator(language);
        QCOMPARE(translator.translate("Main", "Add videos"), QString("Adicionar vídeos"));
        QVERIFY(write(language, "en\n"));
        QTRY_COMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Add videos").isNull());
    }
    void realQmlButtonsAndRepair() {
        QTemporaryDir dir;
        const auto path = dir.filePath("clip.webm"); QVERIFY(QFile::copy(fixture("broken.webm"), path));
        const auto language = dir.filePath("language"); QVERIFY(write(language, "pt\n"));
        FakePicker picker; Backend backend(&picker); Theme theme(dir.path()); Translator translator(language);
        QCoreApplication::installTranslator(&translator);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->requestActivate(); QTest::qWait(80);
        QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier);
        QTRY_COMPARE(picker.calls, 1);
        picker.select("files", {path});
        QTRY_COMPARE_WITH_TIMEOUT(backend.readyCount(), 1, 5000);
        auto *button = window->findChild<QQuickItem *>("repairButton"); QVERIFY(button);
        QVERIFY(button->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(backend.doneCount(), 1, 10000);
        QVERIFY(QFile::exists(dir.filePath("WebM corrigidos/clip.webm")));
        QFile source(path), reference(fixture("broken.webm"));
        QVERIFY(source.open(QIODevice::ReadOnly)); QVERIFY(reference.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), reference.readAll());
        QTest::keyClick(window, Qt::Key_Question);
        QTRY_VERIFY(window->property("helpVisible").toBool());
        QTest::keyClick(window, Qt::Key_Question);
        QTRY_VERIFY(!window->property("helpVisible").toBool());
        QCoreApplication::removeTranslator(&translator);
    }
};
QTEST_MAIN(WebmfixTests)
#include "webmfix_tests.moc"
