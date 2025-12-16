#pragma once

#include "gui/ButtonListAdapter.h"
#include "gui/NavigationAdapter.h"

#include <QPointer>
#include <QWidget>
#include <vector>

class QPushButton;
class QListWidget;

namespace AIO::GUI {

class MainWindow;  // Forward declaration

/**
 * @class GameSelectAdapter
 * @brief Navigation adapter for game selection menu.
 * 
 * Handles keyboard, mouse, and controller input for selecting games from
 * a list. Inherits all navigation functionality from ButtonListAdapter.
 * 
 * Note: Game list uses QListWidget instead of buttons, but we adapt it
 * to use ButtonListAdapter interface for consistency.
 */
class GameSelectAdapter final : public ButtonListAdapter {
public:
    /**
     * @brief Construct the game select adapter.
     * @param page The widget containing the game selection UI
     * @param buttons Vector of action buttons (back, etc.)
     * @param owner Pointer to MainWindow for callbacks
     */
    GameSelectAdapter(QWidget* page,
                      const std::vector<QPushButton*>& buttons,
                      MainWindow* owner,
                      QListWidget* listWidget);

    // NavigationAdapter overrides: make list widget navigable via controller/keyboard.
    int itemCount() const override;
    void setHoveredIndex(int index) override;
    void activateIndex(int index) override;

    /**
     * @brief Handle back button press.
     * @return true if navigation was successful
     */
    bool back() override;

private:
    MainWindow* owner_;
    QPointer<QListWidget> listWidget_;

    // Cache the primary back button (first in the ButtonListAdapter list).
    QPointer<QPushButton> backButton_;
};

} // namespace AIO::GUI
