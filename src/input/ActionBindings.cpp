#include "input/ActionBindings.h"

#include <QMetaType>

namespace {
static int toInt(AIO::Input::LogicalButton b) {
    return static_cast<int>(b);
}

static std::optional<AIO::Input::LogicalButton> fromInt(int v) {
    if (v < 0) return std::nullopt;
    if (v > static_cast<int>(AIO::Input::LogicalButton::Home)) return std::nullopt;
    return static_cast<AIO::Input::LogicalButton>(v);
}
} // namespace

namespace AIO::Input {

QString ActionBindings::settingsGroup() {
    return "Input/ActionBindings";
}

QString ActionBindings::appKey(AppId app) {
    switch (app) {
        case AppId::System: return "System";
        case AppId::GBA: return "GBA";
        case AppId::YouTube: return "YouTube";
    }
    return "Unknown";
}

QString ActionBindings::actionKey(ActionId action) {
    return QString::number(static_cast<int>(action));
}

std::optional<LogicalButton> ActionBindings::loadOverride(AppId app, ActionId action) {
    QSettings s;
    s.beginGroup(settingsGroup());
    s.beginGroup(appKey(app));
    const QVariant v = s.value(actionKey(action), QVariant());
    s.endGroup();
    s.endGroup();

    if (!v.isValid()) return std::nullopt;
    const auto maybe = fromInt(v.toInt());
    if (!maybe.has_value()) return std::nullopt;
    return maybe.value();
}

void ActionBindings::saveOverride(AppId app, ActionId action, LogicalButton logical) {
    QSettings s;
    s.beginGroup(settingsGroup());
    s.beginGroup(appKey(app));
    s.setValue(actionKey(action), toInt(logical));
    s.endGroup();
    s.endGroup();
}

void ActionBindings::clearOverride(AppId app, ActionId action) {
    QSettings s;
    s.beginGroup(settingsGroup());
    s.beginGroup(appKey(app));
    s.remove(actionKey(action));
    s.endGroup();
    s.endGroup();
}

void ActionBindings::clearAllForApp(AppId app) {
    QSettings s;
    s.beginGroup(settingsGroup());
    s.remove(appKey(app));
    s.endGroup();
}

LogicalButton ActionBindings::resolve(AppId app, ActionId action) {
    if (auto o = loadOverride(app, action); o.has_value()) {
        return o.value();
    }

    const auto& actions = AppActions::actionsFor(app);
    for (const auto& desc : actions) {
        if (desc.id == action) {
            return desc.defaultLogical;
        }
    }

    // Safe fallback.
    return LogicalButton::Confirm;
}

QString ActionBindings::logicalDisplayName(LogicalButton logical, ControllerFamily family) {
    // Face labels should be shown as the *equivalent* for the controller family.
    // LogicalButton::Confirm is the "A-equivalent" (Nintendo B, PS Cross, Xbox A).
    // LogicalButton::Back is the "B-equivalent" (Nintendo A, PS Circle, Xbox B).
    switch (logical) {
        case LogicalButton::Confirm:
            switch (family) {
                case ControllerFamily::PlayStation: return "X";
                case ControllerFamily::Nintendo: return "B";
                case ControllerFamily::Xbox: return "A";
                default: return "A";
            }
        case LogicalButton::Back:
            switch (family) {
                case ControllerFamily::PlayStation: return "O";
                case ControllerFamily::Nintendo: return "A";
                case ControllerFamily::Xbox: return "B";
                default: return "B";
            }
        case LogicalButton::Aux1:
            switch (family) {
                case ControllerFamily::PlayStation: return "□";
                case ControllerFamily::Nintendo: return "Y";
                case ControllerFamily::Xbox: return "X";
                default: return "X";
            }
        case LogicalButton::Aux2:
            switch (family) {
                case ControllerFamily::PlayStation: return "△";
                case ControllerFamily::Nintendo: return "X";
                case ControllerFamily::Xbox: return "Y";
                default: return "Y";
            }
        case LogicalButton::Start: return "Start";
        case LogicalButton::Select: return "Select";
        case LogicalButton::L: return "L";
        case LogicalButton::R: return "R";
        case LogicalButton::Up: return "Up";
        case LogicalButton::Down: return "Down";
        case LogicalButton::Left: return "Left";
        case LogicalButton::Right: return "Right";
        case LogicalButton::Home: return "Home";
    }

    return "";
}

} // namespace AIO::Input
