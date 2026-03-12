#pragma once

#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>

namespace AIO {
namespace GUI {

class ThumbnailFillLabel final : public QLabel {
public:
  explicit ThumbnailFillLabel(QWidget *parent = nullptr) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
  }

  void setSourcePixmap(const QPixmap &pixmap) {
    sourcePixmap_ = pixmap;
    updateScaledPixmap();
  }

  void clearSourcePixmap() {
    sourcePixmap_ = QPixmap();
    clear();
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QLabel::resizeEvent(event);
    updateScaledPixmap();
  }

private:
  void updateScaledPixmap() {
    if (sourcePixmap_.isNull()) {
      return;
    }

    const QSize targetSize = contentsRect().size().expandedTo(QSize(1, 1));
    if (targetSize.isEmpty()) {
      return;
    }

    QLabel::setPixmap(sourcePixmap_.scaled(
        targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    setText(QString());
  }

  QPixmap sourcePixmap_;
};

} // namespace GUI
} // namespace AIO