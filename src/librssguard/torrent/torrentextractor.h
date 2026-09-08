// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTEXTRACTOR_H
#define TORRENTEXTRACTOR_H

#include "definitions/definitions.h"

#include <QList>
#include <QStringList>

class Message;

struct RSSGUARD_DLLSPEC TorrentExtractionResult {
  QStringList urls;
  int messagesExamined = 0;
  int messagesWithoutTorrent = 0;
  int duplicatesRemoved = 0;
};

class RSSGUARD_DLLSPEC TorrentExtractor {
  public:
    static TorrentExtractionResult extract(const QList<Message>& messages);
    static QStringList extract(const Message& message);

  private:
    static bool isTorrentUrl(const QString& value, const QString& mimeType = {});
};

#endif // TORRENTEXTRACTOR_H
