// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTSENDHISTORY_H
#define TORRENTSENDHISTORY_H

#include "definitions/definitions.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

class RSSGUARD_DLLSPEC TorrentSendHistory final : public QObject {
    Q_OBJECT

  public:
    static TorrentSendHistory* instance(QObject* parent = nullptr);

    bool wasSent(int messageId, const QString& clientId) const;
    bool wasSentToAnyClient(int messageId) const;
    void markSent(const QList<int>& messageIds, const QString& clientId);

  signals:
    void historyChanged(const QList<int>& messageIds, const QString& clientId);

  private:
    explicit TorrentSendHistory(QObject* parent = nullptr);
    void load();
    void save() const;

    QHash<int, QSet<QString>> m_sentClients;
};

#endif // TORRENTSENDHISTORY_H
