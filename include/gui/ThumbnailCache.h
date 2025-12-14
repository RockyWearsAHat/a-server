#pragma once

#include <QObject>
#include <QHash>
#include <QPixmap>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace AIO {
namespace GUI {

class ThumbnailCache final : public QObject {
    Q_OBJECT

public:
    static ThumbnailCache& instance();

    // Returns true if immediately available.
    bool tryGet(const QString& url, QPixmap* out) const;

    // Kicks off an async download if not cached; emits thumbnailReady when done.
    void request(const QString& url);

signals:
    void thumbnailReady(const QString& url);

private:
    explicit ThumbnailCache(QObject* parent = nullptr);
    QString cachePathForUrl(const QString& url) const;
    void loadFromDiskIfPresent(const QString& url);

    QPointer<QNetworkAccessManager> nam_;
    mutable QHash<QString, QPixmap> memory_;
    QHash<QString, bool> inFlight_;
};

} // namespace GUI
} // namespace AIO
