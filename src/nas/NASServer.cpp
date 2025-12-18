#include "nas/NASServer.h"

#include "common/AssetPaths.h"

#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <iostream>

namespace AIO::NAS {

static QString findAssetsDir() {
    const QString p = AIO::Common::AssetPath("nas");
    if (QDir(p).exists()) return p;
    return QString();
}

NASServer::NASServer(const Options& options, QObject* parent)
    : QObject(parent), options_(options) {

    assetsDir_ = findAssetsDir();

    // Canonicalize root
    QFileInfo rootInfo(options_.rootPath);
    if (rootInfo.exists()) {
        rootCanonical_ = QDir(rootInfo.absoluteFilePath()).canonicalPath();
    }

    QObject::connect(&server_, &QTcpServer::newConnection, this, &NASServer::onNewConnection);
}

bool NASServer::Start() {
    if (IsRunning()) return true;

    if (rootCanonical_.isEmpty()) {
        std::cerr << "[NAS] Invalid rootPath: " << options_.rootPath.toStdString() << std::endl;
        return false;
    }

    // Prefer the requested port, but fall back if it's in use.
    const quint16 requestedPort = options_.port;
    bool listening = server_.listen(options_.bindAddress, requestedPort);
    quint16 chosenPort = requestedPort;
    if (!listening) {
        // Try a small range to avoid hard-failing on common conflicts.
        for (int i = 1; i <= 20; ++i) {
            const quint16 p = static_cast<quint16>(requestedPort + i);
            if (p == 0) break;
            if (server_.listen(options_.bindAddress, p)) {
                listening = true;
                chosenPort = p;
                break;
            }
        }
    }

    if (!listening) {
        std::cerr << "[NAS] Failed to listen on port " << requestedPort
                  << ": " << server_.errorString().toStdString() << std::endl;
        return false;
    }

    std::cout << "[NAS] Serving root: " << rootCanonical_.toStdString() << std::endl;
    if (chosenPort != requestedPort) {
        std::cout << "[NAS] Port " << requestedPort << " in use; using " << chosenPort << std::endl;
    }
    std::cout << "[NAS] Listening on http://" << server_.serverAddress().toString().toStdString()
              << ":" << chosenPort << std::endl;

    if (!options_.bearerToken.isEmpty()) {
        std::cout << "[NAS] Auth: Bearer token required" << std::endl;
    } else {
        std::cout << "[NAS] Auth: disabled (LAN-only)" << std::endl;
    }

    if (assetsDir_.isEmpty()) {
        std::cout << "[NAS] Warning: assets/nas not found; UI may not load." << std::endl;
    }

    return true;
}

void NASServer::Stop() {
    server_.close();
}

bool NASServer::IsRunning() const {
    return server_.isListening();
}

quint16 NASServer::Port() const {
    return server_.serverPort();
}

bool NASServer::IsClientAllowed(const QHostAddress& addr) {
    if (addr.isLoopback()) return true;

    // Allow only private IPv4 ranges + ULA IPv6.
    // IPv4: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16 (link-local)
    const QList<QPair<QHostAddress, int>> allowed = {
        {QHostAddress("10.0.0.0"), 8},
        {QHostAddress("172.16.0.0"), 12},
        {QHostAddress("192.168.0.0"), 16},
        {QHostAddress("169.254.0.0"), 16},
        {QHostAddress("fc00::"), 7},
    };

    for (const auto& subnet : allowed) {
        if (addr.isInSubnet(subnet)) return true;
    }

    return false;
}

bool NASServer::IsAuthorized(const HttpRequest& req) const {
    if (options_.bearerToken.isEmpty()) return true;

    const auto it = req.headers.find("authorization");
    if (it == req.headers.end()) return false;

    const QString v = it.value();
    const QString prefix = "bearer ";
    if (!v.toLower().startsWith(prefix)) return false;
    const QString token = v.mid(prefix.size()).trimmed();
    return token == options_.bearerToken;
}

void NASServer::onNewConnection() {
    while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection();
        if (!socket) continue;

        const QHostAddress peer = socket->peerAddress();
        if (!IsClientAllowed(peer)) {
            HttpResponse resp;
            resp.status = 403;
            resp.contentType = "text/plain; charset=utf-8";
            resp.body = "Forbidden";
            writeResponse(socket, resp);
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        socket->setProperty("nas_buffer", QByteArray());

        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleSocket(socket);
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void NASServer::handleSocket(QTcpSocket* socket) {
    QByteArray buffer = socket->property("nas_buffer").toByteArray();
    buffer.append(socket->readAll());

    HttpRequest req;
    if (!tryParseHttpRequest(buffer, req)) {
        // Keep buffering until we have a full request; protect against abuse.
        if (buffer.size() > 10 * 1024 * 1024) {
            socket->disconnectFromHost();
        } else {
            socket->setProperty("nas_buffer", buffer);
        }
        return;
    }

    socket->setProperty("nas_buffer", QByteArray());

    if (!IsAuthorized(req)) {
        HttpResponse resp;
        resp.status = 401;
        resp.headers.insert("WWW-Authenticate", "Bearer");
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "Unauthorized";
        writeResponse(socket, resp);
        socket->disconnectFromHost();
        return;
    }

    // Stream large files without buffering in memory.
    if (req.path == "/file") {
        // Clear any previous stream state.
        if (auto* existing = socket->findChild<QFile*>("nas_stream_file")) {
            existing->close();
            existing->deleteLater();
        }

        const QString path = getQueryParam(req.query, "path");
        QString canon;
        QString display;
        if (!resolvePathUnderRoot(path, canon, display)) {
            HttpResponse resp;
            resp.status = 403;
            resp.contentType = "text/plain; charset=utf-8";
            resp.body = "Forbidden";
            writeResponse(socket, resp);
            socket->disconnectFromHost();
            return;
        }

        QFileInfo info(canon);
        if (!info.exists() || info.isDir()) {
            HttpResponse resp;
            resp.status = 404;
            resp.contentType = "text/plain; charset=utf-8";
            resp.body = "Not Found";
            writeResponse(socket, resp);
            socket->disconnectFromHost();
            return;
        }

        auto* file = new QFile(canon, socket);
        file->setObjectName("nas_stream_file");
        if (!file->open(QIODevice::ReadOnly)) {
            HttpResponse resp;
            resp.status = 500;
            resp.contentType = "text/plain; charset=utf-8";
            resp.body = "Internal Server Error";
            writeResponse(socket, resp);
            socket->disconnectFromHost();
            return;
        }

        const qint64 total = file->size();
        qint64 start = 0;
        qint64 end = total > 0 ? (total - 1) : 0;
        bool partial = false;
        const auto it = req.headers.find("range");
        if (it != req.headers.end()) {
            if (parseRangeHeader(it.value(), total, start, end)) {
                partial = true;
            }
        }

        if (start > 0) file->seek(start);
        const qint64 remaining = (end >= start) ? (end - start + 1) : 0;
        socket->setProperty("nas_stream_remaining", remaining);

        QByteArray hdr;
        hdr += "HTTP/1.1 ";
        hdr += partial ? "206 Partial Content" : "200 OK";
        hdr += "\r\n";

        const QString contentType = QString::fromUtf8(guessMimeType(canon));
        hdr += "Content-Type: ";
        hdr += contentType.toUtf8();
        hdr += "\r\n";

        hdr += "Content-Length: ";
        hdr += QByteArray::number(remaining);
        hdr += "\r\n";

        hdr += "Accept-Ranges: bytes\r\n";
        if (partial) {
            hdr += "Content-Range: ";
            hdr += QString("bytes %1-%2/%3").arg(start).arg(end).arg(total).toUtf8();
            hdr += "\r\n";
        }

        // Default security headers.
        hdr += "X-Content-Type-Options: nosniff\r\n";
        hdr += "X-Frame-Options: DENY\r\n";
        hdr += "Referrer-Policy: no-referrer\r\n";
        hdr += "Cache-Control: no-store\r\n";

        if (contentType.startsWith("application/octet-stream")) {
            hdr += "Content-Disposition: ";
            hdr += QString("attachment; filename=\"%1\"").arg(info.fileName()).toUtf8();
            hdr += "\r\n";
        }

        hdr += "Connection: close\r\n\r\n";
        socket->write(hdr);

        // Continue sending chunks as the socket drains.
        auto sendChunk = [socket]() {
            auto* f = socket->findChild<QFile*>("nas_stream_file");
            if (!f) return;

            qint64 rem = socket->property("nas_stream_remaining").toLongLong();
            if (rem <= 0) {
                f->close();
                f->deleteLater();
                socket->disconnectFromHost();
                return;
            }

            // Avoid buffering too much in Qt's outgoing queue.
            if (socket->bytesToWrite() > 512 * 1024) return;

            const qint64 chunkSize = qMin<qint64>(64 * 1024, rem);
            const QByteArray data = f->read(chunkSize);
            if (data.isEmpty()) {
                f->close();
                f->deleteLater();
                socket->disconnectFromHost();
                return;
            }

            socket->setProperty("nas_stream_remaining", rem - data.size());
            socket->write(data);
        };

        QObject::connect(socket, &QTcpSocket::bytesWritten, this, [sendChunk](qint64) {
            sendChunk();
        });

        sendChunk();
        return;
    }

    HttpResponse resp = route(req);

    // Default security headers.
    resp.headers.insert("X-Content-Type-Options", "nosniff");
    resp.headers.insert("X-Frame-Options", "DENY");
    resp.headers.insert("Referrer-Policy", "no-referrer");
    resp.headers.insert("Cache-Control", "no-store");

    writeResponse(socket, resp);
    socket->disconnectFromHost();
}

} // namespace AIO::NAS
