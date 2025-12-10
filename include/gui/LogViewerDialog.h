


#pragma once
#include <QDialog>
// Forward declare to avoid including QTextEdit here
class QTextEdit;
class QPushButton;

namespace AIO {
namespace GUI {

class LogViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit LogViewerDialog(QWidget* parent = nullptr);
    ~LogViewerDialog();

    void setLogText(const QString& text);
    void loadLogFile(const QString& path);

private:
    QTextEdit* logTextEdit;
    QPushButton* closeButton;
};

} // namespace GUI
} // namespace AIO
