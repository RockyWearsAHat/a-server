#pragma once

#include <QWidget>
#include <QPointer>

class QLabel;
class QPushButton;
class QGridLayout;

namespace AIO {
namespace GUI {

enum class StreamingApp {
    YouTube,
    Netflix,
    DisneyPlus,
    Hulu
};

class StreamingHubWidget final : public QWidget {
    Q_OBJECT

public:
    explicit StreamingHubWidget(QWidget* parent = nullptr);

signals:
    void launchRequested(AIO::GUI::StreamingApp app);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUi();
    void updateFocusStyle();

    QGridLayout* grid_ = nullptr;
    QLabel* title_ = nullptr;

    QPushButton* tiles_[4]{};
    int focusedIndex_ = 0;
};

} // namespace GUI
} // namespace AIO
