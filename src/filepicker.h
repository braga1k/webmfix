#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class FilePicker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void open(const QString &purpose, const QString &title, bool directory) = 0;
signals:
    void selected(const QString &purpose, const QStringList &paths);
    void failed(const QString &message);
};

class PortalFilePicker : public FilePicker {
    Q_OBJECT
public:
    using FilePicker::FilePicker;
    void open(const QString &purpose, const QString &title, bool directory) override;
private slots:
    void response(uint code, const QVariantMap &results);
private:
    QString m_path, m_purpose;
    void disconnectRequest();
};
