#include "gui/ActionBindingsDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "input/ActionBindings.h"
#include "input/InputManager.h"
#include "gui/ControllerDiagramWidget.h"

namespace {
static uint32_t maskFor(AIO::Input::LogicalButton b) {
    return 1u << static_cast<uint32_t>(b);
}

static QString appTitle(AIO::Input::AppId app) {
    return AIO::Input::AppActions::appDisplayName(app).toUpper() + " CONTROLS";
}

static QString logicalName(AIO::Input::LogicalButton b) {
    switch (b) {
        case AIO::Input::LogicalButton::Confirm: return "Confirm";
        case AIO::Input::LogicalButton::Back: return "Back";
        case AIO::Input::LogicalButton::Aux1: return "Aux1";
        case AIO::Input::LogicalButton::Aux2: return "Aux2";
        case AIO::Input::LogicalButton::Start: return "Start";
        case AIO::Input::LogicalButton::Select: return "Select";
        case AIO::Input::LogicalButton::L: return "L";
        case AIO::Input::LogicalButton::R: return "R";
        case AIO::Input::LogicalButton::Up: return "Up";
        case AIO::Input::LogicalButton::Down: return "Down";
        case AIO::Input::LogicalButton::Left: return "Left";
        case AIO::Input::LogicalButton::Right: return "Right";
        case AIO::Input::LogicalButton::Home: return "Home";
    }
    return "";
}
} // namespace

namespace AIO::GUI {

ActionBindingsDialog::ActionBindingsDialog(AIO::Input::AppId app, QWidget* parent)
    : QDialog(parent)
    , app_(app) {
    setWindowTitle(appTitle(app_));
    setModal(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(30, 30, 30, 30);
    root->setSpacing(16);

    auto* title = new QLabel(appTitle(app_), this);
    title->setAlignment(Qt::AlignCenter);
    title->setProperty("role", "title");
    root->addWidget(title);

    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    hint_->setText(
        "Select an action, then click it (or press Enter) to rebind. "
        "When capturing, press the desired controller button.");
    root->addWidget(hint_);

    auto* content = new QHBoxLayout();
    content->setSpacing(16);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setFocusPolicy(Qt::StrongFocus);

    diagram_ = new AIO::GUI::ControllerDiagramWidget(this);
    diagram_->setApp(app_);

    content->addWidget(list_, 1);
    content->addWidget(diagram_, 1);
    root->addLayout(content, 1);

    auto* buttons = new QHBoxLayout();
    resetBtn_ = new QPushButton("RESET THIS APP", this);
    closeBtn_ = new QPushButton("CLOSE", this);
    resetBtn_->setProperty("variant", "secondary");
    closeBtn_->setProperty("variant", "secondary");

    buttons->addWidget(resetBtn_);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn_);
    root->addLayout(buttons);

    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(resetBtn_, &QPushButton::clicked, this, [this]() {
        AIO::Input::ActionBindings::clearAllForApp(app_);
        rebuildList();
    });

    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const int row = list_->row(item);
        beginCaptureForRow(row);
    });

    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        if (!diagram_ || !current) return;
        const auto action = static_cast<AIO::Input::ActionId>(current->data(Qt::UserRole).toInt());
        diagram_->setHighlightedAction(action);
    });

    // Cancel capture via Esc.
    // Keep this local to the dialog so it doesn't bubble into global navigation.
    // (We still want normal Esc-to-close when not capturing.)
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(16);
    connect(pollTimer_, &QTimer::timeout, this, [this]() {
        // Keep input state fresh.
        // Note: this should not break global navigation because the dialog is modal.
        AIO::Input::InputManager::instance().update();

        const uint32_t now = AIO::Input::InputManager::instance().logicalButtonsDown();

        if (diagram_) diagram_->setCapturing(capturing_);

        if (capturing_) {
            // Detect any newly pressed logical button.
            for (int i = 0; i <= static_cast<int>(AIO::Input::LogicalButton::Home); ++i) {
                const auto b = static_cast<AIO::Input::LogicalButton>(i);
                const bool downNow = (now & maskFor(b)) == 0;
                const bool downPrev = (lastLogical_ & maskFor(b)) == 0;
                if (downNow && !downPrev) {
                    finishCapture(b);
                    break;
                }
            }
        }

        lastLogical_ = now;
    });
    pollTimer_->start();

    rebuildList();

    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }

    if (diagram_ && list_->currentItem()) {
        const auto action = static_cast<AIO::Input::ActionId>(list_->currentItem()->data(Qt::UserRole).toInt());
        diagram_->setHighlightedAction(action);
        diagram_->setCapturing(false);
    }
}

QString ActionBindingsDialog::bindingTextForAction(AIO::Input::ActionId action) const {
    const auto logical = AIO::Input::ActionBindings::resolve(app_, action);
    const auto fam = AIO::Input::InputManager::instance().activeControllerFamily();

    const QString face = AIO::Input::ActionBindings::logicalDisplayName(logical, fam);
    const QString internal = logicalName(logical);

    // Example: "A (Confirm)" or "△ (Aux2)"
    if (!face.isEmpty() && face != internal) {
        return face + " (" + internal + ")";
    }
    return internal;
}

void ActionBindingsDialog::rebuildList() {
    list_->clear();

    const auto& actions = AIO::Input::AppActions::actionsFor(app_);
    for (const auto& a : actions) {
        auto* item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, static_cast<int>(a.id));

        const QString text = a.displayName + ":  " + bindingTextForAction(a.id);
        item->setText(text);
        list_->addItem(item);
    }

    if (capturing_) {
        hint_->setText("Press a controller button to bind (Esc to cancel).");
    } else {
        hint_->setText(
            "Select an action, then click it (or press Enter) to rebind. "
            "Defaults are the A/B/X/Y-equivalent for your controller.");
    }
}

void ActionBindingsDialog::beginCaptureForRow(int row) {
    if (row < 0 || row >= list_->count()) return;
    capturing_ = true;
    capturingRow_ = row;

    // Freeze selection while capturing.
    list_->setEnabled(false);

    rebuildList();
}

void ActionBindingsDialog::finishCapture(AIO::Input::LogicalButton logical) {
    if (capturingRow_ < 0 || capturingRow_ >= list_->count()) {
        capturing_ = false;
        capturingRow_ = -1;
        list_->setEnabled(true);
        rebuildList();
        return;
    }

    auto* item = list_->item(capturingRow_);
    const auto action = static_cast<AIO::Input::ActionId>(item->data(Qt::UserRole).toInt());

    AIO::Input::ActionBindings::saveOverride(app_, action, logical);

    capturing_ = false;
    capturingRow_ = -1;
    list_->setEnabled(true);
    rebuildList();
}

void ActionBindingsDialog::reject() {
    if (capturing_) {
        capturing_ = false;
        capturingRow_ = -1;
        list_->setEnabled(true);
        rebuildList();
        return;
    }
    QDialog::reject();
}

} // namespace AIO::GUI
