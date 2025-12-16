#pragma once

#include <optional>

#include <QString>
#include <QSettings>

#include "input/AppActions.h"
#include "input/InputManager.h"

namespace AIO::Input {

class ActionBindings {
public:
    // Per-app override storage. Keying is app/action; values are LogicalButton.
    static std::optional<LogicalButton> loadOverride(AppId app, ActionId action);
    static void saveOverride(AppId app, ActionId action, LogicalButton logical);
    static void clearOverride(AppId app, ActionId action);
    static void clearAllForApp(AppId app);

    // Resolves the effective binding: override -> app default.
    static LogicalButton resolve(AppId app, ActionId action);

    // Pretty label for the bound button, e.g. "A" / "B" / "X" / "Y" / "Start".
    // Uses the active controller family to show the equivalent face labels.
    static QString logicalDisplayName(LogicalButton logical, ControllerFamily family);

private:
    static QString appKey(AppId app);
    static QString actionKey(ActionId action);

    static QString settingsGroup();
};

} // namespace AIO::Input
