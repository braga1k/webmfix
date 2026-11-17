#pragma once
#include <QAbstractListModel>
#include <QProcess>
#include <QQueue>
#include <QTemporaryDir>
#include <QUrl>
#include "filepicker.h"

class Backend : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int readyCount READ readyCount NOTIFY changed)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString destination READ destination WRITE setDestination NOTIFY changed)
    Q_PROPERTY(bool recursive MEMBER m_recursive NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    enum State { Waiting, Checking, Ready, Unneeded, Repairing, Done, Error };
    Q_ENUM(State)
    enum Roles { NameRole = Qt::UserRole + 1, PathRole, StateRole, DetailRole, OutputRole };
    explicit Backend(FilePicker *picker, QObject *parent = nullptr);
    ~Backend() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_rows.size(); }
    int readyCount() const;
    int doneCount() const;
    bool busy() const { return m_running; }
    QString message() const { return m_message; }
    QString destination() const { return m_destination; }
    double progress() const;
    void setDestination(const QString &value);
    void addPaths(const QStringList &paths);
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void chooseFiles();
    Q_INVOKABLE void chooseFolder();
    Q_INVOKABLE void chooseDestination();
    Q_INVOKABLE void checkAll();
    Q_INVOKABLE void repairAll();
    Q_INVOKABLE void stopAfterCurrent();
    Q_INVOKABLE void closeAfterCurrent();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void openOutput(int row = -1);
    Q_INVOKABLE QString statusText(int state) const;
    void demo();
signals:
    void changed();
    void mayClose();
private:
    struct Row { QString path; State state = Waiting; QString detail, output; };
    QVector<Row> m_rows;
    FilePicker *m_picker;
    QProcess m_process;
    QTemporaryDir m_temp;
    QString m_python, m_worker, m_message, m_destination;
    QQueue<int> m_queue;
    int m_current = -1, m_total = 0, m_completed = 0;
    bool m_running = false, m_repairing = false, m_closePending = false, m_recursive = false;
    void begin(const QList<int> &rows, bool repairing);
    void next();
    void finishProcess(int code, QProcess::ExitStatus status);
    void finishRow(State state, const QString &detail = {}, const QString &output = {});
    void publish(int row);
};
