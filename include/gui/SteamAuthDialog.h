#pragma once

#include <QDialog>
#include <QString>

class QKeyEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QWebEngineView;
class QWidget;

namespace AIO::GUI {

// Embeds Steam's OpenID login page inside a QDialog.
// When Steam redirects to the local callback URL the dialog extracts the
// Steam ID 64 from the URL, confirms via the server status endpoint, and
// emits authComplete(steamId).
class SteamAuthDialog final : public QDialog {
  Q_OBJECT
public:
  explicit SteamAuthDialog(int localPort, QWidget *parent = nullptr);

signals:
  void authComplete(const QString &steamId64);

protected:
  void showEvent(QShowEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private slots:
  void onUrlChanged(const QUrl &url);
  void onLoadFinished(bool ok);

private:
  void startPolling();

  int localPort_;
  QWebEngineView *webView_ = nullptr;
  QWidget *loadingOverlay_ = nullptr;
  QString resolvedSteamId_;
};

} // namespace AIO::GUI
