#pragma once

#include <QObject>
#include <QTcpServer>
#include <QString>
#include <QHostAddress>
#include <QMap>

namespace AIO::NAS {

/**
 * @brief Lightweight, LAN-only NAS HTTP server.
 *
 * Security model:
 * - Connection allowlist: loopback + private IPv4 ranges + IPv6 ULA only.
 * - Optional auth: if Options::bearerToken is set, requests must include
 *   `Authorization: Bearer <token>` (case-insensitive scheme).
 * - Path sandbox: all file operations are constrained under Options::rootPath.
 *
 * Notes:
 * - This server intentionally implements only a small, predictable subset of HTTP.
 * - Large file downloads are streamed directly from disk to avoid buffering.
 */
class NASServer final : public QObject {
    Q_OBJECT

  public:
  /**
   * @brief Configuration for the NAS server instance.
   */
    struct Options {
    /** @brief Required. Root directory that the NAS server exposes. */
    QString rootPath;

    /** @brief Preferred port; may fall back to the next available port. */
    quint16 port = 8080;

    /** @brief Address to bind to; default is IPv4 LAN. */
    QHostAddress bindAddress = QHostAddress::AnyIPv4;

    /**
     * @brief Optional bearer token.
     * If set, requests must include `Authorization: Bearer <token>`.
     */
    QString bearerToken;
    };

  /**
   * @brief Create a NAS server.
   * @param options Immutable configuration for this instance.
   * @param parent QObject parent.
   */
    explicit NASServer(const Options& options, QObject* parent = nullptr);

  /**
   * @brief Start listening for connections.
   * @return true if listening (or already running), false on configuration or bind failure.
   */
    bool Start();

  /** @brief Stop listening (existing sockets will close as they drain). */
    void Stop();

  /** @brief True if the underlying TCP server is listening. */
    [[nodiscard]] bool IsRunning() const;

  /** @brief The actual bound port (may differ from Options::port). */
    [[nodiscard]] quint16 Port() const;

  private slots:
    void onNewConnection();

  private:
    struct HttpRequest {
        QString method;
        QString path;
        QString query;
        QMap<QString, QString> headers; // lower-cased keys
        QByteArray body;
    };

    struct HttpResponse {
        int status = 200;
        QByteArray body;
        QMap<QString, QString> headers;
        QString contentType;
    };

    Options options_;
    QTcpServer server_;

    QString rootCanonical_;
    QString assetsDir_;

    static bool IsClientAllowed(const QHostAddress& addr);
    bool IsAuthorized(const HttpRequest& req) const;

    void handleSocket(class QTcpSocket* socket);
    static bool tryParseHttpRequest(QByteArray& buffer, HttpRequest& outReq);

    static void writeResponse(class QTcpSocket* socket, const HttpResponse& resp);
    static QByteArray statusText(int status);

    bool resolvePathUnderRoot(const QString& userPath, QString& outCanonicalFile, QString& outDisplayPath) const;

    HttpResponse route(const HttpRequest& req);

    HttpResponse serveStaticAsset(const QString& relPath);
    HttpResponse apiList(const HttpRequest& req);
    HttpResponse apiMkdir(const HttpRequest& req);
    HttpResponse apiRename(const HttpRequest& req);
    HttpResponse apiDelete(const HttpRequest& req);
    HttpResponse apiUploadRaw(const HttpRequest& req);
    HttpResponse apiTextPreview(const HttpRequest& req);
    HttpResponse serveFile(const HttpRequest& req);

    static QString getQueryParam(const QString& query, const QString& key);
    static QByteArray guessMimeType(const QString& path);
    static bool parseRangeHeader(const QString& rangeValue, qint64 totalSize, qint64& outStart, qint64& outEndInclusive);
};

} // namespace AIO::NAS
