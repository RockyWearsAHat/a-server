#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;

namespace AIO::GUI {

class NASPage final : public QWidget {
    Q_OBJECT

public:
    explicit NASPage(QWidget* parent = nullptr);

    void setBaseUrl(const QUrl& baseUrl);

    // Exposed so MainWindow can build a NavigationAdapter around our widgets.
    QListWidget* listWidget() const { return list_; }
    QPushButton* upButton() const { return upBtn_; }
    QPushButton* refreshButton() const { return refreshBtn_; }
    QPushButton* mkdirButton() const { return mkdirBtn_; }
    QPushButton* renameButton() const { return renameBtn_; }
    QPushButton* deleteButton() const { return deleteBtn_; }
    QPushButton* uploadButton() const { return uploadBtn_; }
    QPushButton* backButton() const { return backBtn_; }

public slots:
    void refresh();

signals:
    void homeRequested();

private slots:
    void navigateUp();
    void onItemActivated(QListWidgetItem* item);
    void onSelectionChanged();
    void mkdirInCurrent();
    void renameSelected();
    void deleteSelected();
    void uploadFile();

private:
    struct Entry {
        QString name;
        bool isDir = false;
        qint64 size = 0;
        qint64 mtime = 0;
    };

    void setCurrentPath(const QString& path);
    QString selectedPath() const;
    Entry selectedEntry() const;

    void requestList(const QString& path);
    void requestTextPreview(const QString& path);
    void requestImagePreview(const QString& path);

    QNetworkReply* get(const QString& path, const QUrlQuery& query);
    QNetworkReply* postJson(const QString& path, const QJsonObject& body);
    QNetworkReply* postUpload(const QString& dir, const QString& name, const QByteArray& bytes);

    void applyNasTokenTheme();

    QUrl baseUrl_;
    QString token_;
    QString currentPath_ = "/";

    QNetworkAccessManager* net_ = nullptr;

    QLabel* title_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QListWidget* list_ = nullptr;

    QLabel* previewTitle_ = nullptr;
    QLabel* previewInfo_ = nullptr;
    QLabel* imagePreview_ = nullptr;
    QPlainTextEdit* textPreview_ = nullptr;

    QPushButton* upBtn_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    QPushButton* mkdirBtn_ = nullptr;
    QPushButton* renameBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
    QPushButton* uploadBtn_ = nullptr;
    QPushButton* backBtn_ = nullptr;

    QList<Entry> entries_;
};

} // namespace AIO::GUI
