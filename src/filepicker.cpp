// The portal request pattern follows Omacut (MIT, David Heinemeier Hansson).
#include "filepicker.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>
#include <QUuid>

void PortalFilePicker::disconnectRequest() {
    if (!m_path.isEmpty())
        QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_path,
            "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap)));
    m_path.clear();
}

void PortalFilePicker::open(const QString &purpose, const QString &title, bool directory) {
    if (!m_path.isEmpty()) return;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { emit failed(tr("The desktop file picker is unavailable.")); return; }
    const QString token = "webmfix_" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    m_purpose = purpose;
    m_path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    // Subscribe before opening so an immediate response cannot be lost.
    bus.connect("org.freedesktop.portal.Desktop", m_path, "org.freedesktop.portal.Request",
                "Response", this, SLOT(response(uint,QVariantMap)));
    QVariantMap options{{"handle_token", token}, {"multiple", !directory},
                        {"directory", directory}, {"modal", false}};
    QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                         "org.freedesktop.portal.FileChooser", bus);
    auto *watcher = new QDBusPendingCallWatcher(portal.asyncCall("OpenFile", QString(), title, options), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, bus](auto *call) mutable {
        QDBusPendingReply<QDBusObjectPath> reply = *call;
        call->deleteLater();
        if (reply.isError()) {
            disconnectRequest();
            emit failed(tr("Could not open the file picker: %1").arg(reply.error().message()));
        } else if (!m_path.isEmpty() && reply.value().path() != m_path) {
            disconnectRequest();
            m_path = reply.value().path();
            bus.connect("org.freedesktop.portal.Desktop", m_path, "org.freedesktop.portal.Request",
                        "Response", this, SLOT(response(uint,QVariantMap)));
        }
    });
}

void PortalFilePicker::response(uint code, const QVariantMap &results) {
    const QString purpose = m_purpose;
    disconnectRequest();
    if (code == 1) return;
    if (code != 0) { emit failed(tr("The file picker could not complete the request.")); return; }
    QStringList paths;
    for (const auto &uri : results.value("uris").toStringList()) {
        const QUrl url(uri);
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    emit selected(purpose, paths);
}
