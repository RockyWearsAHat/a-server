#include "gui/NASPage.h"

#include "common/CssVars.h"
#include "common/AssetPaths.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLocale>
#include <QRegularExpression>
#include <QUrlQuery>

namespace AIO::GUI {

static bool isProbablyTextFile(const QString& name)
{
    const QString lower = name.toLower();
    return lower.endsWith(".txt") || lower.endsWith(".md") || lower.endsWith(".log") || lower.endsWith(".json") ||
           lower.endsWith(".xml") || lower.endsWith(".ini") || lower.endsWith(".cfg") || lower.endsWith(".csv") ||
           lower.endsWith(".cpp") || lower.endsWith(".h") || lower.endsWith(".hpp") || lower.endsWith(".c") ||
           lower.endsWith(".py") || lower.endsWith(".js") || lower.endsWith(".ts") || lower.endsWith(".qml") ||
           lower.endsWith(".qss") || lower.endsWith(".css");
}

static bool isProbablyImageFile(const QString& name)
{
    const QString lower = name.toLower();
    return lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".gif") ||
           lower.endsWith(".bmp") || lower.endsWith(".webp");
}

NASPage::NASPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("nasPageRoot");
    setFocusPolicy(Qt::StrongFocus);

    net_ = new QNetworkAccessManager(this);

    // Base URL from environment; main.cpp sets AIO_NAS_URL when NAS starts.
    const QString envUrl = qEnvironmentVariableIsSet("AIO_NAS_URL")
        ? QString::fromUtf8(qgetenv("AIO_NAS_URL"))
        : QString("http://127.0.0.1:8080/");
    setBaseUrl(QUrl(envUrl));

    token_ = qEnvironmentVariable("AIO_NAS_TOKEN");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    title_ = new QLabel("NAS", this);
    title_->setAlignment(Qt::AlignCenter);
    title_->setProperty("role", "title");
    root->addWidget(title_);

    pathLabel_ = new QLabel(this);
    pathLabel_->setObjectName("nasPath");
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(pathLabel_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("nasStatus");
    statusLabel_->setText(" ");
    root->addWidget(statusLabel_);

    auto* mid = new QHBoxLayout();
    mid->setSpacing(12);

    list_ = new QListWidget(this);
    list_->setObjectName("nasList");
    list_->setFocusPolicy(Qt::StrongFocus);
    mid->addWidget(list_, 2);

    auto* previewCol = new QVBoxLayout();
    previewCol->setSpacing(8);

    previewTitle_ = new QLabel("Preview", this);
    previewTitle_->setProperty("role", "subtitle");
    previewCol->addWidget(previewTitle_);

    previewInfo_ = new QLabel(this);
    previewInfo_->setWordWrap(true);
    previewCol->addWidget(previewInfo_);

    imagePreview_ = new QLabel(this);
    imagePreview_->setAlignment(Qt::AlignCenter);
    imagePreview_->setMinimumHeight(160);
    imagePreview_->setObjectName("nasImage");
    previewCol->addWidget(imagePreview_);

    textPreview_ = new QPlainTextEdit(this);
    textPreview_->setReadOnly(true);
    textPreview_->setObjectName("nasText");
    textPreview_->setMinimumHeight(180);
    previewCol->addWidget(textPreview_, 1);

    mid->addLayout(previewCol, 1);
    root->addLayout(mid, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(10);

    upBtn_ = new QPushButton("UP", this);
    upBtn_->setCursor(Qt::PointingHandCursor);
    upBtn_->setFocusPolicy(Qt::StrongFocus);

    refreshBtn_ = new QPushButton("REFRESH", this);
    refreshBtn_->setCursor(Qt::PointingHandCursor);
    refreshBtn_->setFocusPolicy(Qt::StrongFocus);

    mkdirBtn_ = new QPushButton("NEW FOLDER", this);
    mkdirBtn_->setCursor(Qt::PointingHandCursor);
    mkdirBtn_->setFocusPolicy(Qt::StrongFocus);

    renameBtn_ = new QPushButton("RENAME", this);
    renameBtn_->setCursor(Qt::PointingHandCursor);
    renameBtn_->setFocusPolicy(Qt::StrongFocus);

    deleteBtn_ = new QPushButton("DELETE", this);
    deleteBtn_->setCursor(Qt::PointingHandCursor);
    deleteBtn_->setFocusPolicy(Qt::StrongFocus);

    uploadBtn_ = new QPushButton("UPLOAD", this);
    uploadBtn_->setCursor(Qt::PointingHandCursor);
    uploadBtn_->setFocusPolicy(Qt::StrongFocus);

    backBtn_ = new QPushButton("BACK", this);
    backBtn_->setCursor(Qt::PointingHandCursor);
    backBtn_->setFocusPolicy(Qt::StrongFocus);

    buttons->addWidget(upBtn_);
    buttons->addWidget(refreshBtn_);
    buttons->addStretch();
    buttons->addWidget(mkdirBtn_);
    buttons->addWidget(renameBtn_);
    buttons->addWidget(deleteBtn_);
    buttons->addWidget(uploadBtn_);
    buttons->addWidget(backBtn_);

    root->addLayout(buttons);

    connect(backBtn_, &QPushButton::clicked, this, [this]() { emit homeRequested(); });
    connect(upBtn_, &QPushButton::clicked, this, &NASPage::navigateUp);
    connect(refreshBtn_, &QPushButton::clicked, this, &NASPage::refresh);
    connect(mkdirBtn_, &QPushButton::clicked, this, &NASPage::mkdirInCurrent);
    connect(renameBtn_, &QPushButton::clicked, this, &NASPage::renameSelected);
    connect(deleteBtn_, &QPushButton::clicked, this, &NASPage::deleteSelected);
    connect(uploadBtn_, &QPushButton::clicked, this, &NASPage::uploadFile);

    connect(list_, &QListWidget::itemActivated, this, &NASPage::onItemActivated);
    connect(list_, &QListWidget::currentItemChanged, this, [this]() { onSelectionChanged(); });

    applyNasTokenTheme();

    setCurrentPath("/");
    refresh();
}

void NASPage::setBaseUrl(const QUrl& baseUrl)
{
    baseUrl_ = baseUrl;
    if (baseUrl_.path().isEmpty()) baseUrl_.setPath("/");
}

void NASPage::applyNasTokenTheme()
{
    // Keep the native page loosely in sync with the NAS web UI theme tokens.
    const QString cssPath = AIO::Common::AssetPath("nas/style.css");
    const auto sets = AIO::Common::LoadCssVarsFromFile(cssPath);

    const QColor window = QApplication::palette().color(QPalette::Window);
    const bool useDark = window.lightness() < 128;

    const auto& vars = (useDark && !sets.dark.isEmpty()) ? sets.dark : sets.light;

    const QString bg = AIO::Common::GetCssVarOr(vars, "bg", "#0b0b0f");
    const QString fg = AIO::Common::GetCssVarOr(vars, "fg", "#f2f2f4");
    const QString muted = AIO::Common::GetCssVarOr(vars, "muted", "#b8b8c2");
    const QString border = AIO::Common::GetCssVarOr(vars, "border", "#2a2a33");
    const QString border2 = AIO::Common::GetCssVarOr(vars, "border2", border);

    const QString qss = QString(
        "QWidget#nasPageRoot { background: %1; color: %2; }"
        "QLabel#nasPath, QLabel#nasStatus { color: %3; }"
        "QListWidget#nasList { background: %1; color: %2; border: 1px solid %4; border-radius: 8px; padding: 6px; }"
        "QListWidget#nasList::item { padding: 6px; border-bottom: 1px solid %5; }"
        "QListWidget#nasList::item:selected { background: %4; }"
        "QPlainTextEdit#nasText { background: %1; color: %2; border: 1px solid %4; border-radius: 8px; }"
        "QLabel#nasImage { border: 1px solid %4; border-radius: 8px; }"
    ).arg(bg, fg, muted, border, border2);

    setStyleSheet(qss);
}

void NASPage::setCurrentPath(const QString& path)
{
    currentPath_ = path.isEmpty() ? "/" : path;
    if (!currentPath_.startsWith('/')) currentPath_.prepend('/');
    // Normalize multiple slashes.
    currentPath_.replace(QRegularExpression(R"(/+)") , "/");
    pathLabel_->setText(QString("Path: %1").arg(currentPath_));
}

void NASPage::refresh()
{
    requestList(currentPath_);
}

void NASPage::navigateUp()
{
    if (currentPath_ == "/") return;
    QString p = currentPath_;
    if (p.endsWith('/')) p.chop(1);
    const int idx = p.lastIndexOf('/');
    if (idx <= 0) {
        setCurrentPath("/");
    } else {
        setCurrentPath(p.left(idx));
    }
    refresh();
}

NASPage::Entry NASPage::selectedEntry() const
{
    const int row = list_ ? list_->currentRow() : -1;
    if (row < 0 || row >= entries_.size()) return {};
    return entries_.at(row);
}

QString NASPage::selectedPath() const
{
    const auto e = selectedEntry();
    if (e.name.isEmpty()) return {};
    QString p = currentPath_;
    if (!p.endsWith('/')) p += '/';
    p += e.name;
    return p;
}

void NASPage::onItemActivated(QListWidgetItem* item)
{
    if (!item) return;
    const auto e = selectedEntry();
    if (e.name.isEmpty()) return;

    if (e.isDir) {
        QString p = currentPath_;
        if (!p.endsWith('/')) p += '/';
        p += e.name;
        setCurrentPath(p);
        refresh();
        return;
    }

    onSelectionChanged();
}

void NASPage::onSelectionChanged()
{
    const auto e = selectedEntry();
    if (e.name.isEmpty()) {
        previewInfo_->setText("Select a file or folder.");
        imagePreview_->clear();
        textPreview_->clear();
        return;
    }

    const QString p = selectedPath();
    const QString type = e.isDir ? "Folder" : "File";
    QString info = QString("%1: %2").arg(type, e.name);
    if (!e.isDir) {
        info += QString("\nSize: %1 bytes").arg(e.size);
    }
    if (e.mtime > 0) {
        info += QString("\nModified: %1").arg(QLocale::system().toString(QDateTime::fromSecsSinceEpoch(e.mtime), QLocale::ShortFormat));
    }
    previewInfo_->setText(info);

    imagePreview_->clear();
    textPreview_->clear();

    if (e.isDir) {
        return;
    }

    if (isProbablyImageFile(e.name)) {
        requestImagePreview(p);
    } else if (isProbablyTextFile(e.name) || e.size <= 256 * 1024) {
        requestTextPreview(p);
    }
}

QNetworkReply* NASPage::get(const QString& path, const QUrlQuery& query)
{
    QUrl url = baseUrl_;
    url.setPath(path);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "AIO-NASPage/1.0");
    if (!token_.isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());
    }
    return net_->get(req);
}

QNetworkReply* NASPage::postJson(const QString& path, const QJsonObject& body)
{
    QUrl url = baseUrl_;
    url.setPath(path);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "AIO-NASPage/1.0");
    if (!token_.isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());
    }

    const QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return net_->post(req, bytes);
}

QNetworkReply* NASPage::postUpload(const QString& dir, const QString& name, const QByteArray& bytes)
{
    QUrl url = baseUrl_;
    url.setPath("/api/upload");

    QUrlQuery q;
    q.addQueryItem("dir", dir);
    q.addQueryItem("name", name);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    req.setHeader(QNetworkRequest::UserAgentHeader, "AIO-NASPage/1.0");
    if (!token_.isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());
    }

    return net_->post(req, bytes);
}

void NASPage::requestList(const QString& path)
{
    statusLabel_->setText("Loading...");

    QUrlQuery q;
    q.addQueryItem("path", path);

    auto* reply = get("/api/list", q);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (status != 200) {
            statusLabel_->setText(QString("Failed to list (HTTP %1)").arg(status));
            return;
        }

        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            statusLabel_->setText("Failed to parse list response");
            return;
        }

        const auto obj = doc.object();
        const QString displayPath = obj.value("path").toString(currentPath_);
        setCurrentPath(displayPath);

        entries_.clear();
        list_->clear();

        const auto items = obj.value("items").toArray();
        for (const auto& v : items) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            Entry e;
            e.name = o.value("name").toString();
            e.isDir = o.value("isDir").toBool();
            e.size = static_cast<qint64>(o.value("size").toDouble());
            e.mtime = static_cast<qint64>(o.value("mtime").toDouble());
            entries_.push_back(e);

            const QString prefix = e.isDir ? "[DIR] " : "      ";
            list_->addItem(prefix + e.name);
        }

        statusLabel_->setText(QString("%1 item(s)").arg(entries_.size()));
        if (list_->count() > 0) {
            list_->setCurrentRow(0);
        }
        onSelectionChanged();
    });
}

void NASPage::requestTextPreview(const QString& path)
{
    QUrlQuery q;
    q.addQueryItem("path", path);
    q.addQueryItem("max", "262144");

    auto* reply = get("/api/text", q);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (status != 200) {
            textPreview_->setPlainText(QString("(text preview failed: HTTP %1)").arg(status));
            return;
        }

        textPreview_->setPlainText(QString::fromUtf8(body));
    });
}

void NASPage::requestImagePreview(const QString& path)
{
    QUrlQuery q;
    q.addQueryItem("path", path);

    auto* reply = get("/file", q);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (status != 200) {
            imagePreview_->setText(QString("(image preview failed: HTTP %1)").arg(status));
            return;
        }

        QPixmap pm;
        pm.loadFromData(body);
        imagePreview_->setPixmap(pm.scaled(imagePreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
}

void NASPage::mkdirInCurrent()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "New Folder", &ok);
    if (!ok) return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;

    statusLabel_->setText("Creating folder...");
    QJsonObject body;
    body["path"] = currentPath_;
    body["name"] = trimmed;

    auto* reply = postJson("/api/mkdir", body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status != 201) {
            statusLabel_->setText(QString("Failed to create folder (HTTP %1)").arg(status));
            return;
        }
        statusLabel_->setText("Folder created");
        refresh();
    });
}

void NASPage::renameSelected()
{
    const auto e = selectedEntry();
    if (e.name.isEmpty()) return;

    bool ok = false;
    const QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, e.name, &ok);
    if (!ok) return;
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == e.name) return;

    statusLabel_->setText("Renaming...");
    QJsonObject body;
    body["path"] = selectedPath();
    body["newName"] = trimmed;

    auto* reply = postJson("/api/rename", body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status != 200) {
            statusLabel_->setText(QString("Failed to rename (HTTP %1)").arg(status));
            return;
        }
        statusLabel_->setText("Renamed");
        refresh();
    });
}

void NASPage::deleteSelected()
{
    const auto e = selectedEntry();
    if (e.name.isEmpty()) return;

    const auto res = QMessageBox::question(this, "Delete", QString("Delete '%1'? This cannot be undone.").arg(e.name));
    if (res != QMessageBox::Yes) return;

    statusLabel_->setText("Deleting...");
    QJsonObject body;
    body["path"] = selectedPath();

    auto* reply = postJson("/api/delete", body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status != 204) {
            statusLabel_->setText(QString("Failed to delete (HTTP %1)").arg(status));
            return;
        }
        statusLabel_->setText("Deleted");
        refresh();
    });
}

void NASPage::uploadFile()
{
    const QString file = QFileDialog::getOpenFileName(this, "Upload File");
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        statusLabel_->setText("Failed to open file");
        return;
    }

    const QByteArray bytes = f.readAll();
    f.close();

    const QFileInfo info(file);
    statusLabel_->setText("Uploading...");

    auto* reply = postUpload(currentPath_, info.fileName(), bytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status != 201) {
            statusLabel_->setText(QString("Upload failed (HTTP %1)").arg(status));
            return;
        }
        statusLabel_->setText("Uploaded");
        refresh();
    });
}

} // namespace AIO::GUI
