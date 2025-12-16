#pragma once

#include <QWidget>
#include <vector>

#include "streaming/StreamingService.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QGridLayout;
class QFrame;

namespace AIO {
namespace Streaming {
class YouTubeService;
}

namespace GUI {

class YouTubeBrowsePage : public QWidget {
    Q_OBJECT

public:
    explicit YouTubeBrowsePage(QWidget* parent = nullptr);

signals:
    void homeRequested();
    void videoRequested(const QString& url);

public slots:
    void loadTrending();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class InputMode { Mouse, Nav };

    void setupUi();
    void runSearch();
    void setStatus(const QString& text);

    void setInputMode(InputMode mode);
    void setFocusedIndex(int idx, bool ensureVisible);
    void clearHover();

    void setResults(const std::vector<AIO::Streaming::VideoContent>& items);
    void rebuildGrid();
    void updateFocusStyle();
    void activateFocused();
    void ensureFocusedVisible();
    void moveFocus(int dx, int dy);
    void setSearchFocused(bool focused);

    InputMode inputMode_ = InputMode::Mouse;
    bool cursorHidden_ = false;
    int lastHoveredIndex_ = -1; // anchor for nav when not hovering
    int hoveredIndex_ = -1;     // active hover only (mouse over tile)

    int columnsForWidth(int w) const;

    QWidget* topBar_{};
    QPushButton* backButton_{};
    QPushButton* homeButton_{};
    QLabel* titleLabel_{};

    QLineEdit* searchEdit_{};
    QPushButton* searchButton_{};

    QLabel* statusLabel_{};

    QScrollArea* scroll_{};
    QWidget* gridHost_{};
    QGridLayout* grid_{};

    std::vector<AIO::Streaming::VideoContent> results_;
    std::vector<QFrame*> tiles_;
    int focusedIndex_ = 0;
    int columns_ = 5;

    AIO::Streaming::YouTubeService* youTube_{};
};

} // namespace GUI
} // namespace AIO
