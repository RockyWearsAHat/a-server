#pragma once

#include <QWidget>

#include "input/AppActions.h"

namespace AIO::GUI {

class ControllerDiagramWidget : public QWidget {
    Q_OBJECT

public:
    explicit ControllerDiagramWidget(QWidget* parent = nullptr);

    void setApp(AIO::Input::AppId app);
    void setHighlightedAction(AIO::Input::ActionId action);
    void setCapturing(bool capturing);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    AIO::Input::AppId app_ = AIO::Input::AppId::System;
    AIO::Input::ActionId highlighted_ = AIO::Input::ActionId::NavConfirm;
    bool capturing_ = false;
};

} // namespace AIO::GUI
