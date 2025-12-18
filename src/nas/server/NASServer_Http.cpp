#include "nas/NASServer.h"

#include <QByteArray>
#include <QFileInfo>
#include <QList>
#include <QTcpSocket>

namespace AIO::NAS {

QByteArray NASServer::statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "";
    }
}

void NASServer::writeResponse(QTcpSocket* socket, const HttpResponse& resp) {
    QByteArray out;
    out += "HTTP/1.1 ";
    out += QByteArray::number(resp.status);
    out += " ";
    out += statusText(resp.status);
    out += "\r\n";

    QByteArray contentType = resp.contentType.toUtf8();
    if (!contentType.isEmpty()) {
        out += "Content-Type: ";
        out += contentType;
        out += "\r\n";
    }

    for (auto it = resp.headers.begin(); it != resp.headers.end(); ++it) {
        out += it.key().toUtf8();
        out += ": ";
        out += it.value().toUtf8();
        out += "\r\n";
    }

    out += "Content-Length: ";
    out += QByteArray::number(resp.body.size());
    out += "\r\n";

    out += "Connection: close\r\n";
    out += "\r\n";
    out += resp.body;

    socket->write(out);
    socket->flush();
}

bool NASServer::tryParseHttpRequest(QByteArray& buffer, HttpRequest& outReq) {
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return false;

    const QByteArray headerBytes = buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBytes.split('\n');
    if (lines.isEmpty()) return false;

    const QByteArray requestLine = lines.first().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) return false;

    outReq.method = QString::fromUtf8(parts[0]).trimmed();

    const QByteArray target = parts[1];
    const int qpos = target.indexOf('?');
    if (qpos >= 0) {
        outReq.path = QString::fromUtf8(target.left(qpos));
        outReq.query = QString::fromUtf8(target.mid(qpos + 1));
    } else {
        outReq.path = QString::fromUtf8(target);
        outReq.query.clear();
    }

    outReq.headers.clear();
    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i];
        line = line.trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QString key = QString::fromUtf8(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        outReq.headers.insert(key, value);
    }

    qint64 contentLength = 0;
    const auto it = outReq.headers.find("content-length");
    if (it != outReq.headers.end()) {
        bool ok = false;
        contentLength = it.value().toLongLong(&ok);
        if (!ok || contentLength < 0) contentLength = 0;
    }

    const qint64 totalNeeded = headerEnd + 4 + contentLength;
    if (buffer.size() < totalNeeded) return false;

    outReq.body = buffer.mid(headerEnd + 4, contentLength);

    // Remove consumed bytes (ignore pipelined requests)
    buffer = buffer.mid(totalNeeded);
    return true;
}

QByteArray NASServer::guessMimeType(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();

    if (ext == "html") return "text/html; charset=utf-8";
    if (ext == "js") return "text/javascript; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";

    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "svg") return "image/svg+xml";

    if (ext == "mp4") return "video/mp4";
    if (ext == "webm") return "video/webm";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "wav") return "audio/wav";
    if (ext == "ogg") return "audio/ogg";

    if (ext == "pdf") return "application/pdf";
    if (ext == "txt" || ext == "log" || ext == "md") return "text/plain; charset=utf-8";

    return "application/octet-stream";
}

bool NASServer::parseRangeHeader(const QString& rangeValue, qint64 totalSize, qint64& outStart, qint64& outEndInclusive) {
    // Only support "bytes=start-end" or "bytes=start-".
    QString v = rangeValue.trimmed();
    if (!v.toLower().startsWith("bytes=")) return false;
    v = v.mid(QString("bytes=").size());

    const int dash = v.indexOf('-');
    if (dash < 0) return false;
    const QString startStr = v.left(dash).trimmed();
    const QString endStr = v.mid(dash + 1).trimmed();

    bool ok1 = false;
    qint64 start = startStr.toLongLong(&ok1);
    if (!ok1 || start < 0) return false;

    qint64 end = totalSize - 1;
    if (!endStr.isEmpty()) {
        bool ok2 = false;
        end = endStr.toLongLong(&ok2);
        if (!ok2) return false;
    }

    if (start >= totalSize) return false;
    if (end < start) return false;
    if (end >= totalSize) end = totalSize - 1;

    outStart = start;
    outEndInclusive = end;
    return true;
}

} // namespace AIO::NAS
