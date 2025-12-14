#pragma once

#include <QWidget>

class QToolButton;
class QLabel;

class QWebEngineView;

namespace AIO {
namespace GUI {

enum class StreamingApp;

class StreamingWebViewPage final : public QWidget {
    Q_OBJECT

public:
    explicit StreamingWebViewPage(QWidget* parent = nullptr);

    void openApp(AIO::GUI::StreamingApp app);

signals:
    void homeRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyWebSettings();
    void setTopBarText(const QString& text);
    QString urlForApp(AIO::GUI::StreamingApp app) const;
    QString titleForApp(AIO::GUI::StreamingApp app) const;

    QWebEngineView* view_ = nullptr;
    QWidget* topBar_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QToolButton* backButton_ = nullptr;
    QToolButton* homeButton_ = nullptr;
};

} // namespace GUI
} // namespace AIO
