#pragma once

#include <QDialog>

#include "input/AppActions.h"

class QListWidget;
class QLabel;
class QPushButton;
class QTimer;
namespace AIO::GUI { class ControllerDiagramWidget; }

namespace AIO::GUI {

class ActionBindingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ActionBindingsDialog(AIO::Input::AppId app, QWidget* parent = nullptr);

protected:
    void reject() override;

private:
    void rebuildList();
    void beginCaptureForRow(int row);
    void finishCapture(AIO::Input::LogicalButton logical);

    QString bindingTextForAction(AIO::Input::ActionId action) const;

    AIO::Input::AppId app_;

    QListWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
    QPushButton* resetBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;

    AIO::GUI::ControllerDiagramWidget* diagram_ = nullptr;

    bool capturing_ = false;
    int capturingRow_ = -1;

    QTimer* pollTimer_ = nullptr;
    uint32_t lastLogical_ = 0xFFFFFFFFu;
};

} // namespace AIO::GUI
