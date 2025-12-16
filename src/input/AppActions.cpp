#include "input/AppActions.h"

namespace AIO::Input {

QString AppActions::appDisplayName(AppId app) {
    switch (app) {
        case AppId::System: return "System";
        case AppId::GBA: return "GBA";
        case AppId::YouTube: return "YouTube";
    }
    return "Unknown";
}

const QVector<ActionDescriptor>& AppActions::actionsFor(AppId app) {
    static const QVector<ActionDescriptor> kSystem = {
        {AppId::System, ActionId::NavUp, "Up", LogicalButton::Up},
        {AppId::System, ActionId::NavDown, "Down", LogicalButton::Down},
        {AppId::System, ActionId::NavLeft, "Left", LogicalButton::Left},
        {AppId::System, ActionId::NavRight, "Right", LogicalButton::Right},
        {AppId::System, ActionId::NavConfirm, "Select", LogicalButton::Confirm},
        {AppId::System, ActionId::NavBack, "Back", LogicalButton::Back},
        {AppId::System, ActionId::NavHome, "Home", LogicalButton::Home},
    };

    static const QVector<ActionDescriptor> kGba = {
        {AppId::GBA, ActionId::GBA_Up, "D-Pad Up", LogicalButton::Up},
        {AppId::GBA, ActionId::GBA_Down, "D-Pad Down", LogicalButton::Down},
        {AppId::GBA, ActionId::GBA_Left, "D-Pad Left", LogicalButton::Left},
        {AppId::GBA, ActionId::GBA_Right, "D-Pad Right", LogicalButton::Right},
        {AppId::GBA, ActionId::GBA_A, "A", LogicalButton::Confirm},
        {AppId::GBA, ActionId::GBA_B, "B", LogicalButton::Back},
        {AppId::GBA, ActionId::GBA_L, "L", LogicalButton::L},
        {AppId::GBA, ActionId::GBA_R, "R", LogicalButton::R},
        {AppId::GBA, ActionId::GBA_Start, "Start", LogicalButton::Start},
        {AppId::GBA, ActionId::GBA_Select, "Select", LogicalButton::Select},
    };

    static const QVector<ActionDescriptor> kYouTube = {
        {AppId::YouTube, ActionId::YouTube_Up, "Up", LogicalButton::Up},
        {AppId::YouTube, ActionId::YouTube_Down, "Down", LogicalButton::Down},
        {AppId::YouTube, ActionId::YouTube_Left, "Left", LogicalButton::Left},
        {AppId::YouTube, ActionId::YouTube_Right, "Right", LogicalButton::Right},
        {AppId::YouTube, ActionId::YouTube_Confirm, "Select", LogicalButton::Confirm},
        {AppId::YouTube, ActionId::YouTube_Back, "Back", LogicalButton::Back},
        {AppId::YouTube, ActionId::YouTube_Home, "Home", LogicalButton::Home},
    };

    switch (app) {
        case AppId::System: return kSystem;
        case AppId::GBA: return kGba;
        case AppId::YouTube: return kYouTube;
    }

    return kSystem;
}

} // namespace AIO::Input
