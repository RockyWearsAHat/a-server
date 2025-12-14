#include "gui/ThumbnailCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace AIO {
namespace GUI {

ThumbnailCache& ThumbnailCache::instance() {
    static ThumbnailCache cache;
    return cache;
}

ThumbnailCache::ThumbnailCache(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {
}

QString ThumbnailCache::cachePathForUrl(const QString& url) const {
    const QByteArray hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(base);
    dir.mkpath("thumbs");
    return dir.filePath(QString("thumbs/%1.jpg").arg(QString::fromUtf8(hash)));
}

bool ThumbnailCache::tryGet(const QString& url, QPixmap* out) const {
    auto it = memory_.find(url);
    if (it != memory_.end()) {
        if (out) *out = it.value();
        return true;
    }

    const QString path = cachePathForUrl(url);
    if (QFile::exists(path)) {
        QPixmap px;
        if (px.load(path)) {
            memory_.insert(url, px);
            if (out) *out = px;
            return true;
        }
    }

    return false;
}

void ThumbnailCache::loadFromDiskIfPresent(const QString& url) {
    if (memory_.contains(url)) return;
    const QString path = cachePathForUrl(url);
    if (!QFile::exists(path)) return;
    QPixmap px;
    if (px.load(path)) {
        memory_.insert(url, px);
    }
}

void ThumbnailCache::request(const QString& url) {
    if (url.isEmpty()) return;
    if (memory_.contains(url)) {
        emit thumbnailReady(url);
        return;
    }

    loadFromDiskIfPresent(url);
    if (memory_.contains(url)) {
        emit thumbnailReady(url);
        return;
    }

    if (inFlight_.value(url, false)) return;
    inFlight_.insert(url, true);

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "AIOStreaming/1.0");

    auto* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        inFlight_.remove(url);
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        QPixmap px;
        if (!px.loadFromData(data)) {
            return;
        }

        // Cap memory growth a bit: keep last ~200 thumbs.
        if (memory_.size() > 200) {
            memory_.erase(memory_.begin());
        }
        memory_.insert(url, px);

        const QString path = cachePathForUrl(url);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            // Store as-is (jpg/png). QPixmap doesn't expose original format here; write raw bytes.
            f.write(data);
            f.close();
        }

        emit thumbnailReady(url);
    });
}

} // namespace GUI
} // namespace AIO
