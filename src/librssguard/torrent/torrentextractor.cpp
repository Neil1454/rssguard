// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentextractor.h"

#include "core/message.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

bool TorrentExtractor::isTorrentUrl(const QString& value, const QString& mimeType) {
  const QString candidate = value.trimmed();
  if (candidate.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) return true;

  const QUrl url(candidate);
  if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) return false;

  if (mimeType.contains(QStringLiteral("bittorrent"), Qt::CaseInsensitive)) return true;
  if (url.path().endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive)) return true;

  const QUrlQuery query(url);
  for (const auto& item : query.queryItems()) {
    if (item.second.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive)) return true;
  }
  return false;
}

QStringList TorrentExtractor::extract(const Message& message) {
  QStringList candidates;

  for (const QSharedPointer<MessageEnclosure>& enclosure : message.m_enclosures) {
    if (!enclosure.isNull() && isTorrentUrl(enclosure->url(), enclosure->mimeType())) candidates.append(enclosure->url().trimmed());
  }

  // The normal article URL is accepted only when it identifies itself as a torrent.
  if (isTorrentUrl(message.m_url)) candidates.append(message.m_url.trimmed());

  static const QRegularExpression linkExpression(
    QStringLiteral("(?:href|src)\\s*=\\s*[\\\"']([^\\\"']+)[\\\"']|((?:magnet:\\?[^\\s<\\\"']+)|(?:https?://[^\\s<\\\"']+\\.torrent(?:\\?[^\\s<\\\"']*)?))"),
    QRegularExpression::CaseInsensitiveOption);

  const QString searchable = message.m_rawContents + QLatin1Char('\n') + message.m_contents + QLatin1Char('\n') + message.m_customData;
  auto match = linkExpression.globalMatch(searchable);
  while (match.hasNext()) {
    const QRegularExpressionMatch item = match.next();
    QString candidate = item.captured(1).isEmpty() ? item.captured(2) : item.captured(1);
    candidate.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    if (isTorrentUrl(candidate)) candidates.append(candidate.trimmed());
  }

  QSet<QString> seen;
  QStringList unique;
  for (const QString& candidate : candidates) {
    const QString key = candidate.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)
                          ? candidate.toLower()
                          : QUrl(candidate).toString(QUrl::FullyEncoded);
    if (!seen.contains(key)) {
      seen.insert(key);
      unique.append(candidate);
    }
  }
  return unique;
}

TorrentExtractionResult TorrentExtractor::extract(const QList<Message>& messages) {
  TorrentExtractionResult result;
  result.messagesExamined = messages.size();
  QSet<QString> seen;

  for (const Message& message : messages) {
    const QStringList urls = extract(message);
    if (urls.isEmpty()) ++result.messagesWithoutTorrent;
    for (const QString& url : urls) {
      const QString key = url.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)
                            ? url.toLower()
                            : QUrl(url).toString(QUrl::FullyEncoded);
      if (seen.contains(key)) ++result.duplicatesRemoved;
      else {
        seen.insert(key);
        result.urls.append(url);
      }
    }
  }
  return result;
}
