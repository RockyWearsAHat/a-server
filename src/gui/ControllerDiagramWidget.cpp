#include "gui/ControllerDiagramWidget.h"

#include <QPainter>

#include "input/ActionBindings.h"
#include "input/InputManager.h"

namespace {
struct Box {
    QRect r;
    QString label;
    bool highlight;
};

static QColor bg() { return QColor(18, 18, 20); }
static QColor fg() { return QColor(235, 235, 245); }
static QColor dim() { return QColor(140, 140, 150); }
static QColor accent() { return QColor(139, 92, 246); }
} // namespace

namespace AIO::GUI {

ControllerDiagramWidget::ControllerDiagramWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(420, 260);
}

void ControllerDiagramWidget::setApp(AIO::Input::AppId app) {
    app_ = app;
    update();
}

void ControllerDiagramWidget::setHighlightedAction(AIO::Input::ActionId action) {
    highlighted_ = action;
    update();
}

void ControllerDiagramWidget::setCapturing(bool capturing) {
    capturing_ = capturing;
    update();
}

void ControllerDiagramWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), bg());

    const auto fam = AIO::Input::InputManager::instance().activeControllerFamily();

    auto bind = [&](AIO::Input::ActionId action) {
        const auto logical = AIO::Input::ActionBindings::resolve(app_, action);
        return AIO::Input::ActionBindings::logicalDisplayName(logical, fam);
    };

    // Layout: simple generic controller.
    const QRect area = rect().adjusted(20, 20, -20, -20);

    // Controller body
    QRect body(area.left() + 10, area.top() + 20, area.width() - 20, area.height() - 40);
    p.setPen(QPen(dim(), 2));
    p.setBrush(QColor(28, 28, 32));
    p.drawRoundedRect(body, 28, 28);

    // Regions
    const int cx = body.center().x();
    const int cy = body.center().y();

    QRect dpad(body.left() + 40, cy - 45, 90, 90);
    QRect face(body.right() - 130, cy - 55, 110, 110);
    QRect mid(cx - 60, body.top() + 55, 120, 60);
    QRect shoulders(body.left() + 35, body.top() + 5, body.width() - 70, 28);

    // Helper for labeled rounded boxes.
    auto drawBox = [&](const QRect& r, const QString& label, bool hl) {
        p.setPen(QPen(hl ? accent() : dim(), hl ? 3 : 2));
        p.setBrush(QColor(22, 22, 26));
        p.drawRoundedRect(r, 12, 12);
        p.setPen(hl ? fg() : fg());
        p.drawText(r, Qt::AlignCenter, label);
    };

    // Decide highlight by mapping action groups.
    auto isHl = [&](AIO::Input::ActionId a) { return a == highlighted_; };

    // D-pad labels (System uses Nav*, others use app-specific ids)
    auto upId = (app_ == AIO::Input::AppId::System) ? AIO::Input::ActionId::NavUp :
               (app_ == AIO::Input::AppId::GBA) ? AIO::Input::ActionId::GBA_Up : AIO::Input::ActionId::YouTube_Up;
    auto downId = (app_ == AIO::Input::AppId::System) ? AIO::Input::ActionId::NavDown :
                 (app_ == AIO::Input::AppId::GBA) ? AIO::Input::ActionId::GBA_Down : AIO::Input::ActionId::YouTube_Down;
    auto leftId = (app_ == AIO::Input::AppId::System) ? AIO::Input::ActionId::NavLeft :
                 (app_ == AIO::Input::AppId::GBA) ? AIO::Input::ActionId::GBA_Left : AIO::Input::ActionId::YouTube_Left;
    auto rightId = (app_ == AIO::Input::AppId::System) ? AIO::Input::ActionId::NavRight :
                  (app_ == AIO::Input::AppId::GBA) ? AIO::Input::ActionId::GBA_Right : AIO::Input::ActionId::YouTube_Right;

    // Face buttons
    auto confirmId = (app_ == AIO::Input::AppId::System) ? AIO::Input::ActionId::NavConfirm :
                    (app_ == AIO::Input::AppId::GBA) ? AIO::Input::ActionId::GBA_A : AIO::Input::ActionId::YouTube_Confirm;
    auto backId = (app_ == AIO::Input::AppId::System) ? AIO::Input::ActionId::NavBack :
                 (app_ == AIO::Input::AppId::GBA) ? AIO::Input::ActionId::GBA_B : AIO::Input::ActionId::YouTube_Back;

    // Home
    auto homeId = (app_ == AIO::Input::AppId::System) ? AIO::Input::ActionId::NavHome :
                 (app_ == AIO::Input::AppId::YouTube) ? AIO::Input::ActionId::YouTube_Home : AIO::Input::ActionId::NavHome;

    // D-pad cluster
    drawBox(QRect(dpad.center().x() - 22, dpad.top(), 44, 32), bind(upId), isHl(upId));
    drawBox(QRect(dpad.center().x() - 22, dpad.bottom() - 32, 44, 32), bind(downId), isHl(downId));
    drawBox(QRect(dpad.left(), dpad.center().y() - 16, 44, 32), bind(leftId), isHl(leftId));
    drawBox(QRect(dpad.right() - 44, dpad.center().y() - 16, 44, 32), bind(rightId), isHl(rightId));

    // Face cluster (show A/B equivalents)
    drawBox(QRect(face.center().x() - 22, face.top(), 44, 32), AIO::Input::ActionBindings::logicalDisplayName(AIO::Input::LogicalButton::Aux2, fam), false);
    drawBox(QRect(face.center().x() - 22, face.bottom() - 32, 44, 32), bind(confirmId), isHl(confirmId));
    drawBox(QRect(face.left(), face.center().y() - 16, 44, 32), AIO::Input::ActionBindings::logicalDisplayName(AIO::Input::LogicalButton::Aux1, fam), false);
    drawBox(QRect(face.right() - 44, face.center().y() - 16, 44, 32), bind(backId), isHl(backId));

    // Mid buttons
    p.setPen(QPen(dim(), 2));
    p.setBrush(QColor(20, 20, 24));
    p.drawRoundedRect(mid, 14, 14);
    p.setPen(fg());
    p.drawText(mid.adjusted(0, 0, 0, -18), Qt::AlignCenter, "HOME");
    p.setPen(isHl(homeId) ? accent() : dim());
    p.drawText(mid.adjusted(0, 18, 0, 0), Qt::AlignCenter, bind(homeId));

    // Shoulders (only meaningful for GBA)
    if (app_ == AIO::Input::AppId::GBA) {
        const auto lId = AIO::Input::ActionId::GBA_L;
        const auto rId = AIO::Input::ActionId::GBA_R;
        drawBox(QRect(shoulders.left(), shoulders.top(), 70, shoulders.height()), "L", isHl(lId));
        drawBox(QRect(shoulders.right() - 70, shoulders.top(), 70, shoulders.height()), "R", isHl(rId));
    } else {
        p.setPen(QPen(dim(), 1));
        p.drawRoundedRect(shoulders, 12, 12);
    }

    if (capturing_) {
        p.setPen(QPen(accent(), 3));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(body.adjusted(6, 6, -6, -6), 22, 22);
    }
}

} // namespace AIO::GUI
