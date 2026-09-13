// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentsendhistory.h"

#include "miscellaneous/application.h"
#include "miscellaneous/settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace {
  const QString Group = QStringLiteral("TorrentClients");
  const QString Key = QStringLiteral("sendHistory");
  constexpr int MaximumRememberedArticles = 5000;
  QPointer<TorrentSendHistory> s_history;
}

TorrentSendHistory* TorrentSendHistory::instance(QObject* parent) {
  if (s_history.isNull()) s_history = new TorrentSendHistory(parent == nullptr ? qApp : parent);
  return s_history;
}

TorrentSendHistory::TorrentSendHistory(QObject* parent) : QObject(parent) { load(); }

bool TorrentSendHistory::wasSent(int messageId, const QString& clientId) const {
  return messageId > 0 && !clientId.isEmpty() && m_sentClients.value(messageId).contains(clientId);
}

bool TorrentSendHistory::wasSentToAnyClient(int messageId) const {
  return messageId > 0 && !m_sentClients.value(messageId).isEmpty();
}

void TorrentSendHistory::markSent(const QList<int>& messageIds, const QString& clientId) {
  if (clientId.isEmpty()) return;
  QList<int> changed;
  for (int messageId : messageIds) {
    if (messageId <= 0 || m_sentClients[messageId].contains(clientId)) continue;
    m_sentClients[messageId].insert(clientId);
    changed.append(messageId);
  }
  while (m_sentClients.size() > MaximumRememberedArticles) m_sentClients.erase(m_sentClients.begin());
  if (changed.isEmpty()) return;
  save();
  emit historyChanged(changed, clientId);
}

void TorrentSendHistory::load() {
  const QJsonObject root = QJsonDocument::fromJson(qApp->settings()->value(Group, Key).toByteArray()).object();
  for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
    bool ok = false;
    const int messageId = it.key().toInt(&ok);
    if (!ok || messageId <= 0) continue;
    QSet<QString> clients;
    for (const QJsonValue& value : it.value().toArray())
      if (!value.toString().isEmpty()) clients.insert(value.toString());
    if (!clients.isEmpty()) m_sentClients.insert(messageId, clients);
  }
}

void TorrentSendHistory::save() const {
  QJsonObject root;
  for (auto it = m_sentClients.constBegin(); it != m_sentClients.constEnd(); ++it) {
    QJsonArray clients;
    for (const QString& clientId : it.value()) clients.append(clientId);
    root.insert(QString::number(it.key()), clients);
  }
  qApp->settings()->setValue(Group, Key, QJsonDocument(root).toJson(QJsonDocument::Compact));
}
