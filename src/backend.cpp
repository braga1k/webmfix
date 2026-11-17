#include "backend.h"
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

Backend::Backend(FilePicker *picker, QObject *parent) : QAbstractListModel(parent), m_picker(picker) {
    m_python = QStandardPaths::findExecutable("python3");
    if (m_python.isEmpty()) m_message = tr("Python 3 is not installed.");
    if (m_temp.isValid()) {
        for (const QString &file : {QString("repair.py"), QString("worker.py")})
            if (!QFile::copy(":/" + file, m_temp.filePath(file)))
                m_message = tr("Could not prepare the repair engine.");
        m_worker = m_temp.filePath("worker.py");
    } else m_message = tr("Could not create a temporary folder.");
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    m_process.setProcessEnvironment(environment);
    connect(&m_process, &QProcess::finished, this, &Backend::finishProcess);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_current >= 0)
            finishRow(Error, tr("Could not start the repair engine: %1").arg(m_process.errorString()));
    });
    if (picker) {
        connect(picker, &FilePicker::selected, this, [this](const QString &purpose, const QStringList &paths) {
            if (busy()) return;
            if (purpose == "destination") { if (!paths.isEmpty()) setDestination(paths.first()); }
            else addPaths(paths);
        });
        connect(picker, &FilePicker::failed, this, [this](const QString &error) { m_message = error; emit changed(); });
    }
}

Backend::~Backend() {
    // Normal closing waits for the current file; this handles abnormal teardown.
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(3000)) { m_process.kill(); m_process.waitForFinished(1000); }
    }
}

int Backend::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }
QVariant Backend::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const Row &row = m_rows[index.row()];
    switch (role) {
    case NameRole: return QFileInfo(row.path).fileName();
    case PathRole: return row.path;
    case StateRole: return row.state;
    case DetailRole: return row.detail;
    case OutputRole: return row.output;
    default: return {};
    }
}
QHash<int, QByteArray> Backend::roleNames() const {
    return {{NameRole, "fileName"}, {PathRole, "filePath"}, {StateRole, "statusCode"},
            {DetailRole, "detail"}, {OutputRole, "outputPath"}};
}
int Backend::readyCount() const { int n = 0; for (const auto &r : m_rows) n += r.state == Ready; return n; }
int Backend::doneCount() const { int n = 0; for (const auto &r : m_rows) n += r.state == Done; return n; }
double Backend::progress() const { return m_total ? double(m_completed) / m_total : 0; }
void Backend::publish(int row) { emit dataChanged(index(row), index(row)); emit changed(); }
void Backend::setDestination(const QString &value) {
    if (busy()) return;
    m_destination = value;
    emit changed();
}
QString Backend::statusText(int state) const {
    switch (state) {
    case Waiting: return tr("Waiting");
    case Checking: return tr("Checking…");
    case Ready: return tr("Needs repair");
    case Unneeded: return tr("No repair needed");
    case Repairing: return tr("Repairing…");
    case Done: return tr("Repaired");
    default: return tr("Could not repair");
    }
}

void Backend::chooseFiles() { if (!busy() && m_picker) m_picker->open("files", tr("Choose WebM videos"), false); }
void Backend::chooseFolder() { if (!busy() && m_picker) m_picker->open("folder", tr("Choose a video folder"), true); }
void Backend::chooseDestination() { if (!busy() && m_picker) m_picker->open("destination", tr("Choose the output folder"), true); }
void Backend::addUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const auto &url : urls) if (url.isLocalFile()) paths << url.toLocalFile();
    addPaths(paths);
}

void Backend::addPaths(const QStringList &paths) {
    if (busy()) return;
    QStringList files;
    for (const auto &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QDir::Files, m_recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                const QString file = it.next();
                const auto relative = QDir(path).relativeFilePath(file).split('/');
                if (QFileInfo(file).suffix().compare("webm", Qt::CaseInsensitive) == 0 &&
                    !relative.contains("WebM corrigidos") &&
                    !std::any_of(relative.begin(), relative.end(), [](const QString &part) { return part.startsWith(".reparar-webm-"); }))
                    files << file;
            }
        } else if (info.isFile() && info.suffix().compare("webm", Qt::CaseInsensitive) == 0) files << path;
    }
    QList<int> added;
    for (const auto &file : files) {
        const QString canonical = QFileInfo(file).canonicalFilePath();
        bool exists = false;
        for (const auto &row : m_rows) if (row.path == canonical) { exists = true; break; }
        if (exists) continue;
        beginInsertRows({}, count(), count());
        m_rows.append({canonical, Waiting, {}, {}});
        endInsertRows();
        added << count() - 1;
    }
    if (added.isEmpty()) {
        m_message = files.isEmpty() ? tr("No WebM videos were found in this selection.") : tr("These videos are already in the list.");
        emit changed();
    } else begin(added, false);
}

void Backend::checkAll() {
    if (busy()) return;
    QList<int> rows;
    for (int i = 0; i < count(); ++i) if (m_rows[i].state != Done) rows << i;
    begin(rows, false);
}
void Backend::repairAll() {
    if (busy()) return;
    QList<int> rows;
    for (int i = 0; i < count(); ++i) if (m_rows[i].state == Ready) rows << i;
    begin(rows, true);
}
void Backend::begin(const QList<int> &rows, bool repairing) {
    if (rows.isEmpty()) return;
    m_queue.clear();
    for (int row : rows) m_queue.enqueue(row);
    m_total = rows.size(); m_completed = 0; m_repairing = repairing; m_running = true;
    m_message.clear(); emit changed();
    next();
}
void Backend::next() {
    if (m_queue.isEmpty()) {
        m_current = -1; m_running = false;
        m_message = m_repairing ? tr("Finished. The original videos are unchanged.") : tr("Check complete. Videos that need repair are marked below.");
        emit changed();
        if (m_closePending) emit mayClose();
        return;
    }
    m_current = m_queue.dequeue();
    auto &row = m_rows[m_current];
    row.state = m_repairing ? Repairing : Checking; row.detail.clear();
    m_message = m_repairing ? tr("Repairing %1 of %2…").arg(m_completed + 1).arg(m_total)
                           : tr("Checking %1 of %2…").arg(m_completed + 1).arg(m_total);
    publish(m_current);
    if (m_python.isEmpty() || !QFileInfo::exists(m_worker)) {
        finishRow(Error, tr("The repair engine is unavailable.")); return;
    }
    m_process.start(m_python, {m_worker, m_repairing ? "repair" : "check", row.path, m_destination});
}
void Backend::finishProcess(int code, QProcess::ExitStatus status) {
    if (m_current < 0) return;
    const auto bytes = m_process.readAllStandardOutput();
    const auto errors = m_process.readAllStandardError();
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (status != QProcess::NormalExit || parse.error != QJsonParseError::NoError || !document.isObject()) {
        finishRow(Error, tr("The repair engine stopped unexpectedly. %1").arg(QString::fromUtf8(errors).right(1200)));
        return;
    }
    const auto result = document.object();
    const auto state = result.value("estado").toString();
    if (state == "corrigido" && code == 0)
        finishRow(Done, tr("Copy verified. Audio and video were preserved."), result.value("destino").toString());
    else if (state == "reparavel" && code == 0) finishRow(Ready, tr("This video has the missing audio header problem."));
    else if (state == "sem_cabecalho_em_falta" && code == 0) finishRow(Unneeded, tr("The missing audio header problem was not found."));
    else finishRow(Error, result.value("motivo").toString(tr("Could not repair this video.")));
}
void Backend::finishRow(State state, const QString &detail, const QString &output) {
    if (m_current < 0) return;
    auto &row = m_rows[m_current]; row.state = state; row.detail = detail; row.output = output;
    publish(m_current); m_current = -1; ++m_completed;
    QTimer::singleShot(0, this, &Backend::next);
}
void Backend::stopAfterCurrent() {
    m_queue.clear();
    m_message = tr("Stopping after the current video…");
    emit changed();
}
void Backend::closeAfterCurrent() { m_closePending = true; stopAfterCurrent(); }
void Backend::clear() {
    if (busy()) return;
    beginResetModel(); m_rows.clear(); endResetModel();
    m_message.clear(); m_total = m_completed = 0; emit changed();
}
void Backend::remove(int row) {
    if (busy() || row < 0 || row >= count()) return;
    beginRemoveRows({}, row, row); m_rows.removeAt(row); endRemoveRows(); emit changed();
}
void Backend::openOutput(int row) {
    QString path;
    if (row >= 0 && row < count()) path = m_rows[row].output;
    else for (const auto &r : m_rows) if (!r.output.isEmpty()) { path = r.output; break; }
    if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}
void Backend::demo() {
    beginResetModel();
    m_rows = {{"/Vídeos/sessao-skate-01.webm", Ready, tr("This video has the missing audio header problem."), {}},
              {"/Vídeos/sessao-skate-02.webm", Done, tr("Copy verified. Audio and video were preserved."), "/Vídeos/WebM corrigidos/sessao-skate-02.webm"},
              {"/Vídeos/sessao-skate-03.webm", Unneeded, tr("The missing audio header problem was not found."), {}}};
    endResetModel(); emit changed();
}
