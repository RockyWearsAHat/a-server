#include "gui/StreamingHubWidget.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QStyle>
#include <QVBoxLayout>

namespace AIO {
namespace GUI {

namespace {

constexpr int kTileFocusGrow = 8;
constexpr int kAnimDuration = 150;

struct BrandColors {
  QColor gradTop;
  QColor gradBottom;
  QColor glow;
};

BrandColors brandColorsFor(StreamingApp app) {
  switch (app) {
  case StreamingApp::YouTube:
    return {QColor(200, 18, 20), QColor(100, 6, 8), QColor(255, 40, 30, 160)};
  case StreamingApp::Netflix:
    return {QColor(44, 10, 16), QColor(16, 3, 5), QColor(229, 9, 20, 160)};
  case StreamingApp::DisneyPlus:
    return {QColor(16, 55, 185), QColor(4, 18, 78), QColor(30, 80, 240, 160)};
  case StreamingApp::Hulu:
    return {QColor(24, 68, 42), QColor(12, 36, 22), QColor(28, 231, 131, 160)};
  }
  return {};
}

QRect expandedRectFor(const QRect &baseRect) {
  return baseRect.adjusted(-kTileFocusGrow, -kTileFocusGrow, kTileFocusGrow,
                           kTileFocusGrow);
}

void animateRect(QWidget *widget, const QRect &targetRect, int duration) {
  if (!widget || widget->geometry() == targetRect)
    return;

  if (auto *existing = widget->findChild<QPropertyAnimation *>(
          QStringLiteral("aioStreamTileGeomAnim"))) {
    existing->stop();
    existing->deleteLater();
  }

  auto *animation = new QPropertyAnimation(widget, "geometry", widget);
  animation->setObjectName(QStringLiteral("aioStreamTileGeomAnim"));
  animation->setDuration(duration);
  animation->setStartValue(widget->geometry());
  animation->setEndValue(targetRect);
  animation->setEasingCurve(QEasingCurve::OutCubic);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void animateShadow(QGraphicsDropShadowEffect *shadow, qreal blurRadius,
                   const QColor &color, int duration) {
  if (!shadow)
    return;

  auto restartAnimation = [&](const QString &name, const QByteArray &property,
                              const QVariant &endValue) {
    if (auto *existing = shadow->findChild<QPropertyAnimation *>(name)) {
      existing->stop();
      existing->deleteLater();
    }
    auto *animation = new QPropertyAnimation(shadow, property, shadow);
    animation->setObjectName(name);
    animation->setDuration(duration);
    animation->setEndValue(endValue);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
  };

  restartAnimation(QStringLiteral("aioStreamShadowBlur"), "blurRadius",
                   blurRadius);
  shadow->setColor(color);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// StreamingTile
// ---------------------------------------------------------------------------

StreamingTile::StreamingTile(StreamingApp app, const QString &name,
                             QWidget *parent)
    : QFrame(parent), app_(app), name_(name) {
  setObjectName(QStringLiteral("aioStreamingTile"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setCursor(Qt::PointingHandCursor);
}

void StreamingTile::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    emit clicked();
    event->accept();
  } else {
    QFrame::mousePressEvent(event);
  }
}

void StreamingTile::paintEvent(QPaintEvent * /*event*/) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const QRectF r(rect());
  const auto brand = brandColorsFor(app_);
  const qreal radius = 18.0;

  // --- Gradient background with rounded corners ---
  QLinearGradient grad(r.topLeft(), r.bottomLeft());
  grad.setColorAt(0.0, brand.gradTop);
  grad.setColorAt(1.0, brand.gradBottom);

  QPainterPath bgPath;
  bgPath.addRoundedRect(r.adjusted(2, 2, -2, -2), radius, radius);
  p.fillPath(bgPath, grad);

  // --- Subtle radial texture ---
  {
    QRadialGradient rg(r.center(), r.width() * 0.8);
    rg.setColorAt(0.0, QColor(255, 255, 255, 6));
    rg.setColorAt(0.5, QColor(255, 255, 255, 0));
    rg.setColorAt(1.0, QColor(0, 0, 0, 12));
    p.fillPath(bgPath, rg);
  }

  // --- Top shimmer ---
  {
    QLinearGradient shine(r.topLeft(),
                          QPointF(r.left(), r.top() + r.height() * 0.15));
    shine.setColorAt(0.0, QColor(255, 255, 255, 18));
    shine.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillPath(bgPath, shine);
  }

  // --- Logo glyph area (upper 60% of the tile) ---
  const qreal logoSize = qMin(r.width(), r.height()) * 0.32;
  const QRectF logoBox(r.center().x() - logoSize / 2.0,
                       r.top() + r.height() * 0.22, logoSize, logoSize);

  switch (app_) {
  case StreamingApp::YouTube:
    paintYouTube(p, logoBox);
    break;
  case StreamingApp::Netflix:
    paintNetflix(p, logoBox);
    break;
  case StreamingApp::DisneyPlus:
    paintDisneyPlus(p, logoBox);
    break;
  case StreamingApp::Hulu:
    paintHulu(p, logoBox);
    break;
  }

  // --- Service name text with shadow ---
  QFont font = p.font();
  font.setPixelSize(qMax(14, static_cast<int>(r.height() * 0.070)));
  font.setWeight(QFont::DemiBold);
  font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
  p.setFont(font);

  QRectF textBox(r.left(), r.top() + r.height() * 0.68, r.width(),
                 r.height() * 0.20);
  p.setPen(QColor(0, 0, 0, 60));
  p.drawText(textBox.translated(0, 1.5), Qt::AlignHCenter | Qt::AlignTop,
             name_);
  p.setPen(Qt::white);
  p.drawText(textBox, Qt::AlignHCenter | Qt::AlignTop, name_);
}

void StreamingTile::paintYouTube(QPainter &p, const QRectF &box) {
  // Iconic YouTube red rounded-rect play button
  const qreal w = box.width();
  const qreal h = box.height() * 0.65;
  QRectF btnRect(box.center().x() - w / 2, box.center().y() - h / 2, w, h);
  const qreal rad = h * 0.30;

  // Shadow
  QPainterPath shadow;
  shadow.addRoundedRect(btnRect.translated(0, 2.5), rad, rad);
  p.fillPath(shadow, QColor(0, 0, 0, 50));

  // Red pill shape
  QPainterPath pill;
  pill.addRoundedRect(btnRect, rad, rad);
  QLinearGradient rg(btnRect.topLeft(), btnRect.bottomLeft());
  rg.setColorAt(0.0, QColor(255, 18, 18));
  rg.setColorAt(1.0, QColor(180, 4, 4));
  p.fillPath(pill, rg);

  // Top highlight
  {
    QLinearGradient sh(btnRect.topLeft(),
                       QPointF(btnRect.left(), btnRect.top() + h * 0.35));
    sh.setColorAt(0.0, QColor(255, 255, 255, 28));
    sh.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillPath(pill, sh);
  }

  // White play triangle centered inside
  const qreal triH = h * 0.50;
  const qreal triW = triH * 0.70;
  const qreal cx = btnRect.center().x() + triW * 0.08; // slight right offset
  const qreal cy = btnRect.center().y();
  QPainterPath tri;
  tri.moveTo(cx - triW / 2, cy - triH / 2);
  tri.lineTo(cx + triW / 2, cy);
  tri.lineTo(cx - triW / 2, cy + triH / 2);
  tri.closeSubpath();
  p.fillPath(tri, Qt::white);
}

void StreamingTile::paintNetflix(QPainter &p, const QRectF &box) {
  // Bold "N" glyph with proper depth
  p.setPen(Qt::NoPen);
  const QColor nRed(229, 9, 20);
  const qreal barW = box.width() * 0.20;
  const qreal top = box.top();
  const qreal bot = box.bottom();
  const qreal left = box.left() + box.width() * 0.18;
  const qreal right = box.right() - box.width() * 0.18;

  // Shadow pass
  p.fillRect(QRectF(left + 2, top + 2, barW, bot - top), QColor(0, 0, 0, 40));
  p.fillRect(QRectF(right - barW + 2, top + 2, barW, bot - top),
             QColor(0, 0, 0, 40));

  // Diagonal in brand red
  QPainterPath diag;
  diag.moveTo(left, top);
  diag.lineTo(left + barW, top);
  diag.lineTo(right, bot);
  diag.lineTo(right - barW, bot);
  diag.closeSubpath();
  p.fillPath(diag, nRed);

  // Left vertical bar
  p.fillRect(QRectF(left, top, barW, bot - top), nRed);
  // Inner highlight
  {
    QLinearGradient lh(QPointF(left, top), QPointF(left + barW, top));
    lh.setColorAt(0.0, QColor(255, 255, 255, 25));
    lh.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(QRectF(left, top, barW, bot - top), lh);
  }

  // Right vertical bar
  p.fillRect(QRectF(right - barW, top, barW, bot - top), nRed);
}

void StreamingTile::paintDisneyPlus(QPainter &p, const QRectF &box) {
  // Castle silhouette (matching homescreen style)
  const qreal s = qMin(box.width(), box.height());
  const QPointF ctr = box.center();
  const QColor col = Qt::white;

  const qreal castleW = s * 0.90;
  const qreal castleH = s * 0.65;
  const qreal castleL = ctr.x() - castleW / 2;
  const qreal castleBot = ctr.y() + s * 0.08;

  struct Tower {
    qreal xOff, h, w;
  };
  const Tower towers[] = {
      {0.10, 0.36, 0.12}, {0.26, 0.52, 0.12}, {0.42, 0.78, 0.16},
      {0.62, 0.52, 0.12}, {0.78, 0.36, 0.12},
  };

  for (const auto &t : towers) {
    qreal tx = castleL + castleW * t.xOff;
    qreal tw = castleW * t.w;
    qreal th = castleH * t.h;
    qreal ty = castleBot - th;

    QRectF towerRect(tx, ty + th * 0.20, tw, th * 0.80);
    QPainterPath towerPath;
    towerPath.addRoundedRect(towerRect, tw * 0.06, tw * 0.06);
    p.fillPath(towerPath, col);

    QPainterPath spire;
    spire.moveTo(tx + tw / 2, ty);
    spire.lineTo(tx, ty + th * 0.22);
    spire.lineTo(tx + tw, ty + th * 0.22);
    spire.closeSubpath();
    p.fillPath(spire, col);
  }

  // Base wall
  QRectF wall(castleL + castleW * 0.06, castleBot - castleH * 0.14,
              castleW * 0.88, castleH * 0.14);
  QPainterPath wallPath;
  wallPath.addRoundedRect(wall, 1, 1);
  p.fillPath(wallPath, col);
}

void StreamingTile::paintHulu(QPainter &p, const QRectF &box) {
  // Bold HULU wordmark in Hulu green
  const QColor huluGreen(28, 231, 131);
  QFont f(QStringLiteral("Noto Sans"));
  f.setPixelSize(static_cast<int>(box.height() * 0.50));
  f.setWeight(QFont::ExtraBold);
  f.setLetterSpacing(QFont::AbsoluteSpacing, box.width() * 0.04);
  f.setStyleStrategy(QFont::PreferAntialias);
  p.setFont(f);

  // Shadow
  p.setPen(QColor(0, 0, 0, 60));
  p.drawText(box.translated(0, 2), Qt::AlignCenter, QStringLiteral("HULU"));
  // Main
  p.setPen(huluGreen);
  p.drawText(box, Qt::AlignCenter, QStringLiteral("HULU"));
}

// ---------------------------------------------------------------------------
// StreamingHubWidget
// ---------------------------------------------------------------------------

StreamingHubWidget::StreamingHubWidget(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("aioStreamingHub"));
  setFocusPolicy(Qt::StrongFocus);
  setupUi();
  updateFocus();
}

void StreamingHubWidget::setupUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(60, 50, 60, 50);
  root->setSpacing(12);

  // Eyebrow title
  title_ = new QLabel(QStringLiteral("Apps"), this);
  title_->setObjectName(QStringLiteral("aioStreamingHubTitle"));
  title_->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
  root->addWidget(title_);

  // Spacer to push tiles into vertical center
  root->addStretch(1);

  // Horizontal row of 4 tiles
  tileRow_ = new QHBoxLayout();
  tileRow_->setSpacing(16);
  tileRow_->setContentsMargins(0, 0, 0, 0);

  const struct {
    const char *label;
    StreamingApp app;
  } entries[4] = {
      {"YouTube", StreamingApp::YouTube},
      {"Netflix", StreamingApp::Netflix},
      {"Disney+", StreamingApp::DisneyPlus},
      {"Hulu", StreamingApp::Hulu},
  };

  for (int i = 0; i < 4; ++i) {
    auto *tile = new StreamingTile(entries[i].app,
                                   QString::fromLatin1(entries[i].label), this);
    tiles_[i] = tile;

    // Drop shadow (starts invisible)
    auto *shadow = new QGraphicsDropShadowEffect(tile);
    shadow->setBlurRadius(0.0);
    shadow->setOffset(0.0, 0.0);
    shadow->setColor(Qt::transparent);
    tile->setGraphicsEffect(shadow);
    shadows_[i] = shadow;

    connect(tile, &StreamingTile::clicked, this,
            [this, app = entries[i].app]() { emit launchRequested(app); });

    tileRow_->addWidget(tile);
  }

  root->addLayout(tileRow_);
  root->addStretch(1);
}

void StreamingHubWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // Tiles size themselves via expanding policy; just ensure focus visuals stay
  // correct.
  updateFocus();
}

void StreamingHubWidget::updateFocus() {
  for (int i = 0; i < 4; ++i) {
    const bool selected = (i == focusedIndex_);
    tiles_[i]->setProperty("aio_selected", selected);
    tiles_[i]->style()->unpolish(tiles_[i]);
    tiles_[i]->style()->polish(tiles_[i]);
    animateTile(tiles_[i], selected);

    const auto brand = brandColorsFor(tiles_[i]->app());
    animateShadow(shadows_[i], selected ? 28.0 : 0.0,
                  selected ? brand.glow : Qt::transparent, kAnimDuration);

    tiles_[i]->update();
  }
}

void StreamingHubWidget::animateTile(StreamingTile *tile, bool selected) {
  if (!tile || !tile->parentWidget())
    return;

  const QRect base = tile->geometry();
  // Only animate if the tile has been laid out (non-zero size)
  if (base.width() == 0 || base.height() == 0)
    return;

  // Calculate where the base rect *should* be (un-expanded) to avoid drift.
  // We store the layout-assigned geometry as a property for stable animation.
  QVariant storedBase = tile->property("aio_baseGeometry");
  QRect baseRect;
  if (storedBase.isValid()) {
    baseRect = storedBase.toRect();
  } else {
    baseRect = base;
    tile->setProperty("aio_baseGeometry", base);
  }

  const QRect targetRect = selected ? expandedRectFor(baseRect) : baseRect;
  animateRect(tile, targetRect, kAnimDuration);
}

void StreamingHubWidget::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
  case Qt::Key_Left:
    if (focusedIndex_ > 0) {
      --focusedIndex_;
      updateFocus();
    }
    event->accept();
    return;
  case Qt::Key_Right:
    if (focusedIndex_ < 3) {
      ++focusedIndex_;
      updateFocus();
    }
    event->accept();
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space: {
    StreamingApp app = tiles_[focusedIndex_]->app();
    emit launchRequested(app);
    event->accept();
    return;
  }
  default:
    break;
  }

  QWidget::keyPressEvent(event);
}

} // namespace GUI
} // namespace AIO
