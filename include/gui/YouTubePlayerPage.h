#pragma once

#include <QWidget>

class QToolButton;
class QLabel;
class QWebEngineView;

namespace AIO {
namespace GUI {

class YouTubePlayerPage final : public QWidget {
    Q_OBJECT

public:
    explicit YouTubePlayerPage(QWidget* parent = nullptr);

    void playVideoUrl(const QString& url);
    void onControllerInput(uint16_t keyInput);

signals:
    void homeRequested();
    void backRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyWebSettings();
    void setTopBarText(const QString& text);

    QWebEngineView* view_ = nullptr;
    QWidget* topBar_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QToolButton* backButton_ = nullptr;
    QToolButton* homeButton_ = nullptr;

    uint16_t lastControllerState_ = 0x03FF;
};

} // namespace GUI
} // namespace AIO
