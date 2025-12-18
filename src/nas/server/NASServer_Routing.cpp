#include "nas/NASServer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace AIO::NAS {

QString NASServer::getQueryParam(const QString& query, const QString& key) {
    QUrlQuery q(query);
    return q.queryItemValue(key);
}

bool NASServer::resolvePathUnderRoot(const QString& userPath, QString& outCanonicalFile, QString& outDisplayPath) const {
    // userPath is a POSIX-ish path like /foo/bar or foo/bar.
    QString rel = userPath;
    if (rel.startsWith('/')) rel = rel.mid(1);
    rel = QDir::cleanPath(rel);
    if (rel == ".") rel.clear();

    const QString joined = QDir(rootCanonical_).absoluteFilePath(rel);
    QFileInfo info(joined);

    // For non-existing paths (uploads / mkdir), canonicalFilePath() may be empty.
    // Canonicalize parent and append filename.
    if (info.exists()) {
        outCanonicalFile = info.canonicalFilePath();
    } else {
        const QFileInfo parentInfo(info.absolutePath());
        const QString parentCanon = QDir(parentInfo.absoluteFilePath()).canonicalPath();
        if (parentCanon.isEmpty()) return false;
        outCanonicalFile = QDir(parentCanon).absoluteFilePath(info.fileName());
    }

    outDisplayPath = "/" + rel;

    const QString rootPrefix = rootCanonical_.endsWith('/') ? rootCanonical_ : (rootCanonical_ + "/");
    if (outCanonicalFile == rootCanonical_) return true;
    if (!outCanonicalFile.startsWith(rootPrefix)) return false;
    return true;
}

NASServer::HttpResponse NASServer::route(const HttpRequest& req) {
    if (req.path == "/" || req.path.isEmpty()) {
        return serveStaticAsset("index.html");
    }

    if (req.path == "/app.js") return serveStaticAsset("app.js");
    if (req.path == "/style.css") return serveStaticAsset("style.css");

    if (req.path == "/api/list") return apiList(req);
    if (req.path == "/api/mkdir") return apiMkdir(req);
    if (req.path == "/api/rename") return apiRename(req);
    if (req.path == "/api/delete") return apiDelete(req);
    if (req.path == "/api/upload") return apiUploadRaw(req);
    if (req.path == "/api/text") return apiTextPreview(req);
    if (req.path == "/file") return serveFile(req);

    HttpResponse resp;
    resp.status = 404;
    resp.contentType = "text/plain; charset=utf-8";
    resp.body = "Not Found";
    return resp;
}

NASServer::HttpResponse NASServer::serveStaticAsset(const QString& relPath) {
    HttpResponse resp;
    if (assetsDir_.isEmpty()) {
        resp.status = 500;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "NAS UI assets not found";
        return resp;
    }

    const QString full = QDir(assetsDir_).absoluteFilePath(relPath);
    QFile f(full);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        resp.status = 404;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Not Found";
        return resp;
    }

    resp.status = 200;
    resp.body = f.readAll();
    resp.contentType = QString::fromUtf8(guessMimeType(full));
    return resp;
}

NASServer::HttpResponse NASServer::apiList(const HttpRequest& req) {
    if (req.method.toUpper() != "GET") {
        HttpResponse resp;
        resp.status = 405;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Method Not Allowed";
        return resp;
    }

    const QString path = getQueryParam(req.query, "path");
    QString canon;
    QString display;
    if (!resolvePathUnderRoot(path, canon, display)) {
        HttpResponse resp;
        resp.status = 403;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Forbidden";
        return resp;
    }

    QFileInfo info(canon);
    if (!info.exists() || !info.isDir()) {
        HttpResponse resp;
        resp.status = 404;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Not Found";
        return resp;
    }

    QDir dir(canon);
    dir.setFilter(QDir::NoDotAndDotDot | QDir::AllEntries);
    dir.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QJsonArray items;
    const QFileInfoList entries = dir.entryInfoList();
    for (const QFileInfo& e : entries) {
        QJsonObject o;
        o["name"] = e.fileName();
        o["isDir"] = e.isDir();
        o["size"] = static_cast<qint64>(e.size());
        o["mtime"] = static_cast<qint64>(e.lastModified().toSecsSinceEpoch());
        items.append(o);
    }

    QJsonObject root;
    root["path"] = display;
    root["root"] = "/";
    root["items"] = items;

    HttpResponse resp;
    resp.status = 200;
    resp.contentType = "application/json; charset=utf-8";
    resp.body = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return resp;
}

NASServer::HttpResponse NASServer::apiMkdir(const HttpRequest& req) {
    if (req.method.toUpper() != "POST") {
        HttpResponse resp;
        resp.status = 405;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Method Not Allowed";
        return resp;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        HttpResponse resp;
        resp.status = 400;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Bad Request";
        return resp;
    }

    const QJsonObject o = doc.object();
    const QString parent = o.value("path").toString();
    const QString name = o.value("name").toString();
    if (name.isEmpty() || name.contains('/') || name.contains('\\')) {
        HttpResponse resp;
        resp.status = 400;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Bad Request";
        return resp;
    }

    QString canonParent;
    QString display;
    if (!resolvePathUnderRoot(parent, canonParent, display)) {
        HttpResponse resp;
        resp.status = 403;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Forbidden";
        return resp;
    }

    QDir d(canonParent);
    if (!d.exists()) {
        HttpResponse resp;
        resp.status = 404;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Not Found";
        return resp;
    }

    if (!d.mkdir(name)) {
        HttpResponse resp;
        resp.status = 500;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Internal Server Error";
        return resp;
    }

    HttpResponse resp;
    resp.status = 201;
    resp.contentType = "application/json; charset=utf-8";
    resp.body = "{}";
    return resp;
}

NASServer::HttpResponse NASServer::apiRename(const HttpRequest& req) {
    if (req.method.toUpper() != "POST") {
        HttpResponse resp;
        resp.status = 405;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Method Not Allowed";
        return resp;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        HttpResponse resp;
        resp.status = 400;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Bad Request";
        return resp;
    }

    const QJsonObject o = doc.object();
    const QString path = o.value("path").toString();
    const QString newName = o.value("newName").toString();
    if (newName.isEmpty() || newName.contains('/') || newName.contains('\\')) {
        HttpResponse resp;
        resp.status = 400;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Bad Request";
        return resp;
    }

    QString canon;
    QString display;
    if (!resolvePathUnderRoot(path, canon, display)) {
        HttpResponse resp;
        resp.status = 403;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Forbidden";
        return resp;
    }

    QFileInfo info(canon);
    if (!info.exists()) {
        HttpResponse resp;
        resp.status = 404;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Not Found";
        return resp;
    }

    const QString dest = QDir(info.absolutePath()).absoluteFilePath(newName);

    bool ok = false;
    if (info.isDir()) {
        QDir d;
        ok = d.rename(canon, dest);
    } else {
        ok = QFile::rename(canon, dest);
    }

    if (!ok) {
        HttpResponse resp;
        resp.status = 500;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Internal Server Error";
        return resp;
    }

    HttpResponse resp;
    resp.status = 200;
    resp.contentType = "application/json; charset=utf-8";
    resp.body = "{}";
    return resp;
}

NASServer::HttpResponse NASServer::apiDelete(const HttpRequest& req) {
    if (req.method.toUpper() != "POST") {
        HttpResponse resp;
        resp.status = 405;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Method Not Allowed";
        return resp;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        HttpResponse resp;
        resp.status = 400;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Bad Request";
        return resp;
    }

    const QJsonObject o = doc.object();
    const QString path = o.value("path").toString();

    QString canon;
    QString display;
    if (!resolvePathUnderRoot(path, canon, display)) {
        HttpResponse resp;
        resp.status = 403;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Forbidden";
        return resp;
    }

    QFileInfo info(canon);
    if (!info.exists()) {
        HttpResponse resp;
        resp.status = 404;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Not Found";
        return resp;
    }

    bool ok = false;
    if (info.isDir()) {
        QDir d(canon);
        ok = d.removeRecursively();
    } else {
        ok = QFile::remove(canon);
    }

    if (!ok) {
        HttpResponse resp;
        resp.status = 500;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Internal Server Error";
        return resp;
    }

    HttpResponse resp;
    resp.status = 204;
    resp.contentType = "text/plain; charset=utf-8";
    resp.body.clear();
    return resp;
}

NASServer::HttpResponse NASServer::apiUploadRaw(const HttpRequest& req) {
    // Upload via: POST /api/upload?dir=/some/path&name=filename
    if (req.method.toUpper() != "POST") {
        HttpResponse resp;
        resp.status = 405;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Method Not Allowed";
        return resp;
    }

    const auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        bool ok = false;
        const qint64 len = it.value().toLongLong(&ok);
        if (ok && len > 1024LL * 1024LL * 1024LL) {
            HttpResponse resp;
            resp.status = 413;
            resp.contentType = "text/plain; charset=utf-8";
            resp.body = "Payload Too Large";
            return resp;
        }
    }

    const QString dirPath = getQueryParam(req.query, "dir");
    const QString name = getQueryParam(req.query, "name");
    if (name.isEmpty() || name.contains('/') || name.contains('\\')) {
        HttpResponse resp;
        resp.status = 400;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Bad Request";
        return resp;
    }

    QString canonDir;
    QString display;
    if (!resolvePathUnderRoot(dirPath, canonDir, display)) {
        HttpResponse resp;
        resp.status = 403;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Forbidden";
        return resp;
    }

    QFileInfo dirInfo(canonDir);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        HttpResponse resp;
        resp.status = 404;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Not Found";
        return resp;
    }

    const QString dest = QDir(canonDir).absoluteFilePath(name);

    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        HttpResponse resp;
        resp.status = 500;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Internal Server Error";
        return resp;
    }

    f.write(req.body);
    f.close();

    HttpResponse resp;
    resp.status = 201;
    resp.contentType = "application/json; charset=utf-8";
    resp.body = "{}";
    return resp;
}

NASServer::HttpResponse NASServer::apiTextPreview(const HttpRequest& req) {
    if (req.method.toUpper() != "GET") {
        HttpResponse resp;
        resp.status = 405;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Method Not Allowed";
        return resp;
    }

    const QString path = getQueryParam(req.query, "path");
    const QString maxStr = getQueryParam(req.query, "max");

    qint64 maxBytes = 128 * 1024;
    if (!maxStr.isEmpty()) {
        bool ok = false;
        const qint64 v = maxStr.toLongLong(&ok);
        if (ok && v > 0 && v <= (1024 * 1024)) maxBytes = v;
    }

    QString canon;
    QString display;
    if (!resolvePathUnderRoot(path, canon, display)) {
        HttpResponse resp;
        resp.status = 403;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Forbidden";
        return resp;
    }

    QFileInfo info(canon);
    if (!info.exists() || info.isDir()) {
        HttpResponse resp;
        resp.status = 404;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Not Found";
        return resp;
    }

    QFile f(canon);
    if (!f.open(QIODevice::ReadOnly)) {
        HttpResponse resp;
        resp.status = 500;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Internal Server Error";
        return resp;
    }

    QByteArray data = f.read(maxBytes);
    f.close();

    HttpResponse resp;
    resp.status = 200;
    resp.contentType = "text/plain; charset=utf-8";
    resp.body = data;
    return resp;
}

NASServer::HttpResponse NASServer::serveFile(const HttpRequest& req) {
    // NOTE: /file is streamed directly in handleSocket() to avoid buffering large files.
    HttpResponse resp;
    resp.status = 500;
    resp.contentType = "text/plain; charset=utf-8";
    resp.body = "Internal Server Error";
    return resp;
}

} // namespace AIO::NAS
