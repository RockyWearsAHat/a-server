#pragma once

#include <QString>
#include <QVector>

#include "input/InputTypes.h"

namespace AIO::Input {

enum class AppId {
    System,
    GBA,
    YouTube,
};

enum class ActionId {
    // System
    NavUp,
    NavDown,
    NavLeft,
    NavRight,
    NavConfirm,
    NavBack,
    NavHome,

    // GBA
    GBA_A,
    GBA_B,
    GBA_L,
    GBA_R,
    GBA_Start,
    GBA_Select,
    GBA_Up,
    GBA_Down,
    GBA_Left,
    GBA_Right,

    // YouTube (minimal set for now)
    YouTube_Up,
    YouTube_Down,
    YouTube_Left,
    YouTube_Right,
    YouTube_Confirm,
    YouTube_Back,
    YouTube_Home,
};

struct ActionDescriptor {
    AppId app;
    ActionId id;
    QString displayName;

    // Default logical binding for controller navigation.
    LogicalButton defaultLogical;
};

class AppActions {
public:
    static const QVector<ActionDescriptor>& actionsFor(AppId app);

    static QString appDisplayName(AppId app);
};

} // namespace AIO::Input
