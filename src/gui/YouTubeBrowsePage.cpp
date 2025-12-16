#include "gui/YouTubeBrowsePage.h"

#include "gui/ThumbnailCache.h"
#include "streaming/StreamingManager.h"
#include "streaming/YouTubeService.h"
#include "streaming/StreamingService.h"

#include "input/InputManager.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QGridLayout>
#include <QFrame>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QCursor>
#include <QUrl>
#include <QStyle>

#include <algorithm>

namespace AIO {
namespace GUI {

static constexpr auto kTileIndexProperty = "aio_tile_index";
static constexpr auto kTileSelectedProperty = "aio_tile_selected";
static constexpr auto kTileHoveredProperty = "aio_tile_hovered";

static int tileIndexForObject(QObject* obj) {
    if (!obj) return -1;
    QVariant v = obj->property(kTileIndexProperty);
    if (!v.isValid()) return -1;
    bool ok = false;
    int idx = v.toInt(&ok);
    return ok ? idx : -1;
}

YouTubeBrowsePage::YouTubeBrowsePage(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    auto service = AIO::Streaming::StreamingManager::getInstance().getService(AIO::Streaming::StreamingServiceType::YouTube);
    youTube_ = dynamic_cast<AIO::Streaming::YouTubeService*>(service.get());

    setupUi();

    // Load something immediately so the page doesn't look empty.
    loadTrending();
}

void YouTubeBrowsePage::setInputMode(InputMode mode) {
    if (inputMode_ == mode) return;
    inputMode_ = mode;

    if (inputMode_ == InputMode::Nav) {
        if (!cursorHidden_) {
            QGuiApplication::setOverrideCursor(Qt::BlankCursor);
            cursorHidden_ = true;
        }
    } else {
        if (cursorHidden_) {
            QGuiApplication::restoreOverrideCursor();
            cursorHidden_ = false;
        }
    }
}

void YouTubeBrowsePage::clearHover() {
    if (hoveredIndex_ == -1) return;
    hoveredIndex_ = -1;
    updateFocusStyle();
}

void YouTubeBrowsePage::setFocusedIndex(int idx, bool ensureVisible) {
    if (results_.empty()) return;
    const int clamped = std::clamp(idx, 0, (int)results_.size() - 1);
    if (focusedIndex_ == clamped) return;
    focusedIndex_ = clamped;
    updateFocusStyle();
    if (ensureVisible) ensureFocusedVisible();
}

void YouTubeBrowsePage::setupUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    topBar_ = new QWidget(this);
    topBar_->setObjectName("aioTopBar");

    auto* barLayout = new QHBoxLayout(topBar_);
    barLayout->setContentsMargins(12, 10, 12, 10);
    barLayout->setSpacing(10);

    backButton_ = new QPushButton("Back", topBar_);
    backButton_->setFocusPolicy(Qt::NoFocus);
    backButton_->setProperty("variant", "secondary");

    homeButton_ = new QPushButton("Home", topBar_);
    homeButton_->setFocusPolicy(Qt::NoFocus);
    homeButton_->setProperty("variant", "secondary");

    titleLabel_ = new QLabel("YouTube", topBar_);
    titleLabel_->setProperty("role", "subtitle");

    searchEdit_ = new QLineEdit(topBar_);
    searchEdit_->setPlaceholderText("Search YouTube…");

    searchButton_ = new QPushButton("Search", topBar_);
    searchButton_->setFocusPolicy(Qt::NoFocus);
    searchButton_->setProperty("variant", "secondary");

    barLayout->addWidget(backButton_);
    barLayout->addWidget(homeButton_);
    barLayout->addSpacing(8);
    barLayout->addWidget(titleLabel_);
    barLayout->addStretch();
    barLayout->addWidget(searchEdit_, 2);
    barLayout->addWidget(searchButton_);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(36, 22, 36, 30);
    contentLayout->setSpacing(16);

    statusLabel_ = new QLabel("", content);
    statusLabel_->setProperty("role", "subtitle");

    scroll_ = new QScrollArea(content);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFocusPolicy(Qt::NoFocus);
    scroll_->viewport()->setFocusPolicy(Qt::NoFocus);
    scroll_->viewport()->installEventFilter(this);

    gridHost_ = new QWidget(scroll_);
    gridHost_->setFocusPolicy(Qt::NoFocus);
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(18);
    grid_->setVerticalSpacing(18);
    scroll_->setWidget(gridHost_);

    contentLayout->addWidget(statusLabel_);
    contentLayout->addWidget(scroll_, 1);

    root->addWidget(topBar_);
    root->addWidget(content, 1);

    connect(homeButton_, &QPushButton::clicked, this, [this]() { emit homeRequested(); });
    connect(backButton_, &QPushButton::clicked, this, [this]() { emit homeRequested(); });

    connect(searchButton_, &QPushButton::clicked, this, [this]() { runSearch(); });
    connect(searchEdit_, &QLineEdit::returnPressed, this, [this]() { runSearch(); });

    connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this, [this](const QString&) {
        // Only update visuals; avoids rebuilding widgets.
        updateFocusStyle();
    });
}

void YouTubeBrowsePage::setStatus(const QString& text) {
    statusLabel_->setText(text);
}

void YouTubeBrowsePage::loadTrending() {
    if (!youTube_) {
        setStatus("YouTube service not available.");
        return;
    }

    setStatus("Loading trending…");

    const auto items = youTube_->getTrending(25);

    setResults(items);
}

void YouTubeBrowsePage::runSearch() {
    if (!youTube_) {
        setStatus("YouTube service not available.");
        return;
    }

    const auto query = searchEdit_->text().trimmed();
    if (query.isEmpty()) {
        loadTrending();
        return;
    }

    setStatus("Searching…");

    const auto items = youTube_->search(query.toStdString(), 25);

    setResults(items);
}

void YouTubeBrowsePage::keyPressEvent(QKeyEvent* event) {
    // Keyboard navigation: keep cursor visible, but discard hover until mouse moves again.
    setInputMode(InputMode::Mouse);
    clearHover();

    // Let the search box behave normally.
    if (searchEdit_ && searchEdit_->hasFocus()) {
        if (event->key() == Qt::Key_Escape) {
            gridHost_->setFocus();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
        emit homeRequested();
        event->accept();
        return;
    }

    switch (event->key()) {
        case Qt::Key_Left:
            if (lastHoveredIndex_ >= 0) setFocusedIndex(lastHoveredIndex_, false);
            moveFocus(-1, 0);
            event->accept();
            return;
        case Qt::Key_Right:
            if (lastHoveredIndex_ >= 0) setFocusedIndex(lastHoveredIndex_, false);
            moveFocus(1, 0);
            event->accept();
            return;
        case Qt::Key_Up:
            if (lastHoveredIndex_ >= 0) setFocusedIndex(lastHoveredIndex_, false);
            moveFocus(0, -1);
            event->accept();
            return;
        case Qt::Key_Down:
            if (lastHoveredIndex_ >= 0) setFocusedIndex(lastHoveredIndex_, false);
            moveFocus(0, 1);
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            activateFocused();
            event->accept();
            return;
        default:
            break;
    }

    QWidget::keyPressEvent(event);
}

bool YouTubeBrowsePage::eventFilter(QObject* watched, QEvent* event) {
    // Prevent the scroll area viewport from consuming arrows / wheel in ways
    // that fight with selection movement.
    if (scroll_ && watched == scroll_->viewport()) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            switch (ke->key()) {
                case Qt::Key_Left:
                case Qt::Key_Right:
                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_Return:
                case Qt::Key_Enter:
                case Qt::Key_Space:
                    // Let MainWindow deliver to this widget instead.
                    setFocus();
                    ke->accept();
                    return true;
                default:
                    break;
            }
        }
    }

    const int idx = tileIndexForObject(watched);
    if (idx < 0) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Enter) {
        setInputMode(InputMode::Mouse);
        if (results_.empty()) return false;
        lastHoveredIndex_ = std::clamp(idx, 0, (int)results_.size() - 1);
        hoveredIndex_ = lastHoveredIndex_;
        setFocusedIndex(lastHoveredIndex_, false);
        return false;
    }
    if (event->type() == QEvent::Leave) {
        if (hoveredIndex_ == idx) {
            hoveredIndex_ = -1;
            updateFocusStyle();
        }
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            setInputMode(InputMode::Mouse);
            if (!results_.empty()) {
                lastHoveredIndex_ = std::clamp(idx, 0, (int)results_.size() - 1);
                hoveredIndex_ = lastHoveredIndex_;
                setFocusedIndex(lastHoveredIndex_, false);
            }
            activateFocused();
            return true;
        }
        return false;
    }

    return QWidget::eventFilter(watched, event);
}

void YouTubeBrowsePage::mouseMoveEvent(QMouseEvent* event) {
    setInputMode(InputMode::Mouse);
    QWidget::mouseMoveEvent(event);
}

void YouTubeBrowsePage::leaveEvent(QEvent* event) {
    // When the cursor is not hovering a tile, we intentionally show no hover outline.
    // Selection still exists for nav mode, but we don't change it here.
    clearHover();
    QWidget::leaveEvent(event);
}

void YouTubeBrowsePage::setSearchFocused(bool focused) {
    if (!searchEdit_) return;
    if (focused) {
        searchEdit_->setFocus();
        searchEdit_->selectAll();
    } else {
        gridHost_->setFocus();
    }
}

void YouTubeBrowsePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!scroll_) return;
    const int newCols = columnsForWidth(scroll_->viewport()->width());
    if (newCols != columns_) {
        columns_ = newCols;
        rebuildGrid();
    }
}

int YouTubeBrowsePage::columnsForWidth(int w) const {
    // Tile width ~280 plus spacing; keep within 3..6 columns.
    const int tileW = 280;
    const int spacing = 18;
    const int usable = std::max(0, w);
    const int cols = std::max(1, (usable + spacing) / (tileW + spacing));
    return std::clamp(cols, 3, 6);
}

void YouTubeBrowsePage::setResults(const std::vector<AIO::Streaming::VideoContent>& items) {
    results_ = items;
    focusedIndex_ = 0;
    lastHoveredIndex_ = results_.empty() ? -1 : 0;
    hoveredIndex_ = -1;

    if (results_.empty()) {
        setStatus("No results. Make sure `YOUTUBE_API_KEY` is set in .env.");
        rebuildGrid();
        return;
    }

    setStatus(QString("%1 videos • Enter/A to open • Esc/B to back").arg((int)results_.size()));
    rebuildGrid();
}

void YouTubeBrowsePage::rebuildGrid() {
    // Clear old tiles
    while (QLayoutItem* item = grid_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    tiles_.clear();

    columns_ = columnsForWidth(scroll_->viewport()->width());

    const int tileW = 280;
    const int tileH = 240;
    const int thumbH = 158;

    for (int i = 0; i < (int)results_.size(); ++i) {
        const int row = i / columns_;
        const int col = i % columns_;

        auto* frame = new QFrame(gridHost_);
        frame->setFixedSize(tileW, tileH);
        frame->setObjectName("aioTile");
        frame->setProperty(kTileSelectedProperty, i == focusedIndex_);
        frame->setProperty(kTileHoveredProperty, i == hoveredIndex_);

        auto* v = new QVBoxLayout(frame);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(10);

        auto* thumb = new QLabel(frame);
        thumb->setObjectName("thumb");
        thumb->setFixedSize(tileW - 24, thumbH);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setProperty("role", "thumb");
        thumb->setText("Loading");

        auto* title = new QLabel(frame);
        title->setObjectName("title");
        title->setWordWrap(true);
        title->setTextInteractionFlags(Qt::NoTextInteraction);
        title->setProperty("role", "tileTitle");
        title->setText(QString::fromStdString(results_[i].title));

        v->addWidget(thumb);
        v->addWidget(title);
        v->addStretch();

        frame->setProperty(kTileIndexProperty, i);
        frame->setFocusPolicy(Qt::NoFocus);
        frame->setMouseTracking(true);
        frame->installEventFilter(this);

        grid_->addWidget(frame, row, col);
        tiles_.push_back(frame);

        // Prime thumb
        const QString thumbUrl = QString::fromStdString(results_[i].thumbnailUrl);
        if (!thumbUrl.isEmpty()) {
            QPixmap px;
            if (ThumbnailCache::instance().tryGet(thumbUrl, &px)) {
                thumb->setPixmap(px.scaled(thumb->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                thumb->setText(QString());
            } else {
                ThumbnailCache::instance().request(thumbUrl);
            }
        }
    }

    updateFocusStyle();
    ensureFocusedVisible();
    setFocus();
}

void YouTubeBrowsePage::updateFocusStyle() {
    for (int i = 0; i < (int)tiles_.size(); ++i) {
        auto* frame = tiles_[i];
        if (!frame) continue;
        const bool selected = (i == focusedIndex_);
        const bool hovered = (inputMode_ == InputMode::Mouse) && (i == hoveredIndex_);
        frame->setProperty(kTileSelectedProperty, selected);
        frame->setProperty(kTileHoveredProperty, hovered);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
        frame->update();

        const QString thumbUrl = (i < (int)results_.size()) ? QString::fromStdString(results_[i].thumbnailUrl) : QString();
        if (!thumbUrl.isEmpty()) {
            QPixmap px;
            if (ThumbnailCache::instance().tryGet(thumbUrl, &px)) {
                if (auto* thumb = frame->findChild<QLabel*>("thumb")) {
                    thumb->setPixmap(px.scaled(thumb->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    thumb->setText(QString());
                }
            }
        }
    }
}

void YouTubeBrowsePage::ensureFocusedVisible() {
    if (focusedIndex_ < 0 || focusedIndex_ >= (int)tiles_.size()) return;
    auto* tile = tiles_[focusedIndex_];
    if (!tile) return;
    scroll_->ensureWidgetVisible(tile, 24, 24);
}

void YouTubeBrowsePage::moveFocus(int dx, int dy) {
    if (results_.empty()) return;

    const int row = focusedIndex_ / columns_;
    const int col = focusedIndex_ % columns_;
    int newRow = row + dy;
    int newCol = col + dx;

    newCol = std::clamp(newCol, 0, columns_ - 1);
    newRow = std::max(0, newRow);

    int newIndex = newRow * columns_ + newCol;
    newIndex = std::clamp(newIndex, 0, (int)results_.size() - 1);

    focusedIndex_ = newIndex;
    updateFocusStyle();
    ensureFocusedVisible();
}

void YouTubeBrowsePage::activateFocused() {
    if (focusedIndex_ < 0 || focusedIndex_ >= (int)results_.size()) return;
    const QString url = QString::fromStdString(results_[focusedIndex_].videoUrl);
    if (!url.isEmpty()) {
        emit videoRequested(url);
    }
}

} // namespace GUI
} // namespace AIO
