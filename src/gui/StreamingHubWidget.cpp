#include "gui/StreamingHubWidget.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace AIO {
namespace GUI {

static QString tileStyle(bool focused) {
    if (focused) {
        return "QPushButton {"
               "  background-color: #1f3a4d;"
               "  border: 2px solid #00aaff;"
               "  border-radius: 16px;"
               "  padding: 22px;"
               "  font-size: 22px;"
               "  font-weight: 700;"
               "}"
               "QPushButton:hover { background-color: #24485f; }";
    }

    return "QPushButton {"
           "  background-color: #202020;"
           "  border: 1px solid #3d3d3d;"
           "  border-radius: 16px;"
           "  padding: 22px;"
           "  font-size: 22px;"
           "  font-weight: 700;"
           "}"
           "QPushButton:hover { background-color: #2a2a2a; }";
}

StreamingHubWidget::StreamingHubWidget(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    updateFocusStyle();
}

void StreamingHubWidget::setupUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(60, 50, 60, 50);
    root->setSpacing(24);

    title_ = new QLabel("STREAMING", this);
    title_->setAlignment(Qt::AlignCenter);
    title_->setStyleSheet("font-size: 30px; font-weight: 800; color: #00aaff; letter-spacing: 1px;");
    root->addWidget(title_);

    grid_ = new QGridLayout();
    grid_->setHorizontalSpacing(22);
    grid_->setVerticalSpacing(22);

    const struct {
        const char* label;
        StreamingApp app;
    } entries[4] = {
        {"YouTube", StreamingApp::YouTube},
        {"Netflix", StreamingApp::Netflix},
        {"Disney+", StreamingApp::DisneyPlus},
        {"Hulu", StreamingApp::Hulu},
    };

    for (int i = 0; i < 4; ++i) {
        auto* btn = new QPushButton(entries[i].label, this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(140);
        btn->setFocusPolicy(Qt::NoFocus);
        tiles_[i] = btn;

        connect(btn, &QPushButton::clicked, this, [this, app = entries[i].app]() { emit launchRequested(app); });

        const int row = i / 2;
        const int col = i % 2;
        grid_->addWidget(btn, row, col);
    }

    root->addLayout(grid_);

    auto* hint = new QLabel("Enter/A to open • Esc/B to go back", this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color: #777; font-size: 12px;");
    root->addWidget(hint);
}

void StreamingHubWidget::updateFocusStyle() {
    for (int i = 0; i < 4; ++i) {
        tiles_[i]->setStyleSheet(tileStyle(i == focusedIndex_));
    }
}

void StreamingHubWidget::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Left:
            if (focusedIndex_ % 2 == 1) focusedIndex_ -= 1;
            updateFocusStyle();
            event->accept();
            return;
        case Qt::Key_Right:
            if (focusedIndex_ % 2 == 0) focusedIndex_ += 1;
            updateFocusStyle();
            event->accept();
            return;
        case Qt::Key_Up:
            if (focusedIndex_ >= 2) focusedIndex_ -= 2;
            updateFocusStyle();
            event->accept();
            return;
        case Qt::Key_Down:
            if (focusedIndex_ <= 1) focusedIndex_ += 2;
            updateFocusStyle();
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space: {
            StreamingApp app = StreamingApp::YouTube;
            if (focusedIndex_ == 1) app = StreamingApp::Netflix;
            if (focusedIndex_ == 2) app = StreamingApp::DisneyPlus;
            if (focusedIndex_ == 3) app = StreamingApp::Hulu;
            emit launchRequested(app);
            event->accept();
            return;
        }
        default:
            break;
    }

    QWidget::keyPressEvent(event);
}

} // namespace GUI
} // namespace AIO
