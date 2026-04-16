#include "gui/HomeScreen.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdlib>

namespace AIO {
namespace GUI {

namespace {

struct TileStyle {
  QColor tint;
  QColor tintFocus;
};

struct GradientSpec {
  QColor top;
  QColor bottom;
};

GradientSpec gradientFor(HomeTileKind kind, bool focused) {
  // Premium brand gradients — high contrast top-to-bottom for card depth.
  switch (kind) {
  case HomeTileKind::GBA:
    return focused ? GradientSpec{QColor(115, 88, 235), QColor(28, 18, 72)}
                   : GradientSpec{QColor(68, 52, 165), QColor(22, 16, 58)};
  case HomeTileKind::PS1:
    return focused ? GradientSpec{QColor(65, 140, 225), QColor(12, 35, 72)}
                   : GradientSpec{QColor(38, 90, 160), QColor(10, 28, 55)};
  case HomeTileKind::Switch:
    return focused ? GradientSpec{QColor(245, 58, 52), QColor(82, 12, 10)}
                   : GradientSpec{QColor(168, 35, 32), QColor(55, 10, 8)};
  case HomeTileKind::MediaServer:
    return focused ? GradientSpec{QColor(25, 145, 185), QColor(5, 35, 52)}
                   : GradientSpec{QColor(18, 95, 128), QColor(5, 30, 45)};
  case HomeTileKind::ScreenMirror:
    return focused ? GradientSpec{QColor(42, 178, 146), QColor(7, 46, 38)}
                   : GradientSpec{QColor(30, 125, 103), QColor(7, 36, 29)};
  case HomeTileKind::Settings:
    return focused ? GradientSpec{QColor(100, 112, 170), QColor(32, 38, 62)}
                   : GradientSpec{QColor(68, 76, 120), QColor(26, 32, 50)};
  case HomeTileKind::YouTube:
    return focused ? GradientSpec{QColor(225, 24, 26), QColor(68, 4, 5)}
                   : GradientSpec{QColor(128, 14, 16), QColor(42, 3, 4)};
  case HomeTileKind::Netflix:
    return focused ? GradientSpec{QColor(95, 18, 28), QColor(24, 4, 7)}
                   : GradientSpec{QColor(65, 12, 18), QColor(18, 3, 5)};
  case HomeTileKind::DisneyPlus:
    return focused ? GradientSpec{QColor(22, 68, 210), QColor(3, 12, 58)}
                   : GradientSpec{QColor(12, 42, 148), QColor(3, 10, 48)};
  case HomeTileKind::Hulu:
    return focused ? GradientSpec{QColor(16, 88, 48), QColor(4, 28, 14)}
                   : GradientSpec{QColor(10, 58, 32), QColor(3, 22, 10)};
  case HomeTileKind::Store:
    return focused ? GradientSpec{QColor(210, 148, 28), QColor(68, 42, 5)}
                   : GradientSpec{QColor(140, 98, 18), QColor(45, 28, 4)};
  case HomeTileKind::Library:
    return focused ? GradientSpec{QColor(35, 145, 210), QColor(8, 34, 58)}
                   : GradientSpec{QColor(22, 92, 145), QColor(6, 24, 44)};
  case HomeTileKind::Blank:
    return focused ? GradientSpec{QColor(34, 36, 48), QColor(16, 18, 24)}
                   : GradientSpec{QColor(26, 28, 36), QColor(14, 16, 22)};
  }
  return {QColor(24, 26, 34), QColor(14, 16, 22)};
}

TileStyle styleFor(HomeTileKind kind) {
  // Kept for icon accent coloring — not used for backgrounds anymore.
  switch (kind) {
  case HomeTileKind::GBA:
    return {QColor(120, 90, 255, 38), QColor(120, 90, 255, 72)};
  case HomeTileKind::PS1:
    return {QColor(80, 150, 255, 38), QColor(80, 150, 255, 72)};
  case HomeTileKind::Switch:
    return {QColor(255, 60, 50, 38), QColor(255, 60, 50, 72)};
  case HomeTileKind::MediaServer:
    return {QColor(0, 190, 230, 32), QColor(0, 190, 230, 64)};
  case HomeTileKind::ScreenMirror:
    return {QColor(42, 178, 146, 28), QColor(42, 178, 146, 64)};
  case HomeTileKind::Settings:
    return {QColor(170, 180, 200, 24), QColor(170, 180, 200, 52)};
  case HomeTileKind::YouTube:
    return {QColor(255, 30, 30, 28), QColor(255, 30, 30, 56)};
  case HomeTileKind::Netflix:
    return {QColor(229, 9, 20, 28), QColor(229, 9, 20, 56)};
  case HomeTileKind::DisneyPlus:
    return {QColor(17, 60, 207, 36), QColor(17, 60, 207, 70)};
  case HomeTileKind::Hulu:
    return {QColor(28, 231, 131, 28), QColor(28, 231, 131, 56)};
  case HomeTileKind::Store:
    return {QColor(212, 156, 28, 28), QColor(212, 156, 28, 56)};
  case HomeTileKind::Library:
    return {QColor(64, 181, 246, 28), QColor(64, 181, 246, 60)};
  case HomeTileKind::Blank:
    return {QColor(120, 130, 150, 10), QColor(120, 130, 150, 22)};
  }
  return {};
}

const char *labelFor(HomeTileKind kind) {
  switch (kind) {
  case HomeTileKind::GBA:
    return "Game Boy Advance";
  case HomeTileKind::PS1:
    return "PlayStation";
  case HomeTileKind::Switch:
    return "Switch";
  case HomeTileKind::MediaServer:
    return "Media";
  case HomeTileKind::ScreenMirror:
    return "Mirror";
  case HomeTileKind::Settings:
    return "Settings";
  case HomeTileKind::YouTube:
    return "YouTube";
  case HomeTileKind::Netflix:
    return "Netflix";
  case HomeTileKind::DisneyPlus:
    return "Disney+";
  case HomeTileKind::Hulu:
    return "Hulu";
  case HomeTileKind::Store:
    return "Store";
  case HomeTileKind::Library:
    return "Library";
  case HomeTileKind::Blank:
    return "";
  }
  return "";
}

const char *subtitleFor(HomeTileKind kind) {
  switch (kind) {
  case HomeTileKind::GBA:
    return "Handheld classics";
  case HomeTileKind::PS1:
    return "Relive the 32-bit era";
  case HomeTileKind::Switch:
    return "Your modern library";
  case HomeTileKind::MediaServer:
    return "Your personal collection";
  case HomeTileKind::ScreenMirror:
    return "Cast from any device";
  case HomeTileKind::Settings:
    return "Make it yours";
  case HomeTileKind::YouTube:
    return "Endless video";
  case HomeTileKind::Netflix:
    return "Movies, series & more";
  case HomeTileKind::DisneyPlus:
    return "Stories you love";
  case HomeTileKind::Hulu:
    return "Live & on demand";
  case HomeTileKind::Store:
    return "Something new to play";
  case HomeTileKind::Library:
    return "Ready to launch";
  case HomeTileKind::Blank:
    return "";
  }
  return "";
}

QColor ambientGlowFor(HomeTileKind kind) {
  // Brighter, more saturated glow colors for dramatic halo effect.
  switch (kind) {
  case HomeTileKind::GBA:
    return QColor(170, 140, 255, 255);
  case HomeTileKind::PS1:
    return QColor(80, 160, 255, 255);
  case HomeTileKind::Switch:
    return QColor(255, 60, 50, 255);
  case HomeTileKind::MediaServer:
    return QColor(0, 210, 255, 240);
  case HomeTileKind::ScreenMirror:
    return QColor(42, 178, 146, 240);
  case HomeTileKind::Settings:
    return QColor(190, 200, 235, 220);
  case HomeTileKind::YouTube:
    return QColor(255, 40, 30, 255);
  case HomeTileKind::Netflix:
    return QColor(229, 9, 20, 255);
  case HomeTileKind::DisneyPlus:
    return QColor(30, 80, 240, 255);
  case HomeTileKind::Hulu:
    return QColor(28, 231, 131, 240);
  case HomeTileKind::Store:
    return QColor(212, 156, 28, 245);
  case HomeTileKind::Library:
    return QColor(64, 181, 246, 240);
  case HomeTileKind::Blank:
    return QColor(0, 0, 0, 0);
  }
  return QColor(0, 0, 0, 0);
}

QColor composited(const QColor &base, const QColor &tint) {
  const qreal a = tint.alphaF();
  return QColor(qMin(255, static_cast<int>(base.red() + tint.red() * a)),
                qMin(255, static_cast<int>(base.green() + tint.green() * a)),
                qMin(255, static_cast<int>(base.blue() + tint.blue() * a)));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// HomeTile
// ---------------------------------------------------------------------------

HomeTile::HomeTile(HomeTileKind kind, QWidget *parent)
    : QFrame(parent), kind_(kind) {
  setObjectName(QStringLiteral("aioHomeTile"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  if (kind != HomeTileKind::Blank)
    setCursor(Qt::PointingHandCursor);
}

void HomeTile::mousePressEvent(QMouseEvent *event) {
  if (kind_ == HomeTileKind::Blank) {
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton) {
    emit clicked();
    event->accept();
  } else {
    QFrame::mousePressEvent(event);
  }
}

void HomeTile::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const QRectF r(rect());
  const bool focused = property("aio_selected").toBool();
  const qreal fp = focusProgress_; // 0→1 animated
  const qreal radius = 16.0;

  // Content fill with a subtle scale (1.0 → 1.04).
  // The focus ring is drawn AFTER p.restore() so it always sits at exact widget
  // boundaries — not affected by the scale transform.
  const qreal scale = 1.0 + 0.04 * fp;
  p.save();
  p.translate(r.center());
  p.scale(scale, scale);
  p.translate(-r.center());

  const auto grad = gradientFor(kind_, focused);
  // Content fills the full tile rect — no inset margin wasted space.
  const QRectF tileRect = r;

  // ── STEP 1: Main gradient fill ──
  QPainterPath bg;
  bg.addRoundedRect(tileRect, radius, radius);
  QLinearGradient lg(tileRect.topLeft(), tileRect.bottomLeft());
  lg.setColorAt(0.0, grad.top);
  lg.setColorAt(1.0, grad.bottom);
  p.fillPath(bg, lg);

  // Subtle radial texture overlay
  {
    QRadialGradient rg(tileRect.center(), tileRect.width() * 0.8);
    rg.setColorAt(0.0, QColor(255, 255, 255, focused ? 8 : 4));
    rg.setColorAt(0.5, QColor(255, 255, 255, 0));
    rg.setColorAt(1.0, QColor(0, 0, 0, focused ? 14 : 8));
    p.fillPath(bg, rg);
  }

  // Dim unfocused tiles — moderate push-back
  if (fp < 0.99) {
    p.fillPath(bg, QColor(0, 0, 0,
                          static_cast<int>(60 * (1.0 - qBound(0.0, fp, 1.0)))));
  }

  // ── STEP 2: Top-edge highlight — glass depth ──
  {
    QLinearGradient shine(
        tileRect.topLeft(),
        QPointF(tileRect.left(), tileRect.top() + tileRect.height() * 0.22));
    shine.setColorAt(0.0,
                     QColor(255, 255, 255,
                            static_cast<int>(10 + 40 * qBound(0.0, fp, 1.0))));
    shine.setColorAt(
        0.5,
        QColor(255, 255, 255, static_cast<int>(2 + 18 * qBound(0.0, fp, 1.0))));
    shine.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillPath(bg, shine);
  }

  // Specular rim — bright top-edge band for glass material feel
  if (kind_ != HomeTileKind::Blank) {
    QLinearGradient rim(tileRect.topLeft(),
                        QPointF(tileRect.left(), tileRect.top() + 4.0));
    rim.setColorAt(0.0,
                   QColor(255, 255, 255,
                          static_cast<int>(20 + 45 * qBound(0.0, fp, 1.0))));
    rim.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillPath(bg, rim);
  }

  if (kind_ == HomeTileKind::Blank) {
    p.restore();
    // Blank placeholder: dashed border at widget coords
    const qreal bw = 0.5 + 1.0 * fp;
    QPen pen(focused ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 12),
             bw, Qt::DashLine);
    pen.setDashPattern({6, 4});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius, radius);
    return;
  }

  // Vertically center icon + label + subtitle as a content cluster
  const qreal iconH = qMin(tileRect.width() * 0.44, tileRect.height() * 0.38);
  const int labelPx =
      qMax(18, static_cast<int>(tileRect.height() * (0.060 + 0.014 * fp)));
  const int subPx = qMax(11, static_cast<int>(tileRect.height() * 0.032));
  const qreal gap1 = 12.0; // icon to label
  const qreal gap2 = 4.0;  // label to subtitle
  const qreal contentH = iconH + gap1 + labelPx + gap2 + subPx;
  const qreal contentTop =
      tileRect.top() + (tileRect.height() - contentH) / 2.0;

  const QRectF iconBox(tileRect.center().x() - iconH / 2.0, contentTop, iconH,
                       iconH);
  const QColor iconCol =
      QColor(255, 255, 255, static_cast<int>(190 + 65 * qBound(0.0, fp, 1.0)));
  paintIcon(p, iconBox, iconCol);

  // Label
  p.setPen(
      QColor(255, 255, 255, static_cast<int>(180 + 75 * qBound(0.0, fp, 1.0))));
  QFont font = p.font();
  font.setPixelSize(labelPx);
  font.setWeight(QFont::DemiBold);
  font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
  p.setFont(font);

  QRectF labelBox(tileRect.left(), iconBox.bottom() + gap1, tileRect.width(),
                  labelPx + 6);
  p.drawText(labelBox, Qt::AlignHCenter | Qt::AlignTop,
             QString::fromLatin1(labelFor(kind_)));

  // Subtitle
  const char *sub = subtitleFor(kind_);
  if (sub && sub[0]) {
    p.setPen(QColor(255, 255, 255,
                    static_cast<int>(80 + 60 * qBound(0.0, fp, 1.0))));
    QFont subFont = p.font();
    subFont.setPixelSize(subPx);
    subFont.setWeight(QFont::Normal);
    subFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
    p.setFont(subFont);

    QRectF subBox(tileRect.left(), labelBox.bottom() + gap2, tileRect.width(),
                  subPx + 4);
    p.drawText(subBox, Qt::AlignHCenter | Qt::AlignTop,
               QString::fromLatin1(sub));
  }
  p.restore();

  // ── Focus ring: drawn after restore, always at exact widget boundary ──
  // This is the tvOS/PS5/Switch pattern: a crisp, stable ring that doesn't
  // scale or skew — it sits at a fixed position outside the card artwork.
  {
    const qreal ringAlpha =
        focused ? (180.0 + 75.0 * qBound(0.0, fp, 1.0)) : 0.0;
    const qreal ringWidth = focused ? 3.0 : 0.0;
    QPainterPath ring;
    ring.addRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
    p.setPen(
        QPen(QColor(255, 255, 255, static_cast<int>(ringAlpha)), ringWidth));
    p.setBrush(Qt::NoBrush);
    p.drawPath(ring);
  }
}

void HomeTile::paintIcon(QPainter &p, const QRectF &box, const QColor &col) {
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::NoBrush);

  switch (kind_) {

  // ── GBA: Clean handheld silhouette ──
  case HomeTileKind::GBA: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();

    // Bold handheld body — instantly recognizable shape
    const qreal bw = s * 0.78;
    const qreal bh = s * 0.46;
    QRectF body(ctr.x() - bw / 2, ctr.y() - bh * 0.25, bw, bh);

    QPainterPath shape;
    shape.addRoundedRect(body, bh * 0.30, bh * 0.30);

    // Shoulder bumps
    const qreal sw = bw * 0.18;
    const qreal sh = bh * 0.15;
    QPainterPath lSh, rSh;
    lSh.addRoundedRect(
        QRectF(body.left() + bw * 0.02, body.top() - sh * 0.35, sw, sh),
        sh * 0.5, sh * 0.5);
    rSh.addRoundedRect(
        QRectF(body.right() - bw * 0.02 - sw, body.top() - sh * 0.35, sw, sh),
        sh * 0.5, sh * 0.5);
    shape = shape.united(lSh).united(rSh);

    p.fillPath(shape, col);

    // Subtle screen inset
    const qreal scrW = bw * 0.42;
    const qreal scrH = bh * 0.44;
    QRectF screen(ctr.x() - scrW / 2, body.top() + bh * 0.15, scrW, scrH);
    QPainterPath scrPath;
    scrPath.addRoundedRect(screen, 3, 3);
    p.fillPath(scrPath, QColor(255, 255, 255, 35));
    break;
  }

  // ── PS1: PlayStation face button symbols ──
  case HomeTileKind::PS1: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const qreal spread = s * 0.22;
    const qreal sym = s * 0.11;
    const qreal penW = qMax(1.8, s * 0.035);

    p.setPen(QPen(col, penW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    // Triangle (top)
    {
      const qreal ty = ctr.y() - spread;
      QPainterPath tri;
      tri.moveTo(ctr.x(), ty - sym);
      tri.lineTo(ctr.x() - sym * 0.88, ty + sym * 0.65);
      tri.lineTo(ctr.x() + sym * 0.88, ty + sym * 0.65);
      tri.closeSubpath();
      p.drawPath(tri);
    }

    // Circle (right)
    p.drawEllipse(QPointF(ctr.x() + spread, ctr.y()), sym * 0.65, sym * 0.65);

    // Cross (bottom)
    {
      const qreal bx = ctr.x();
      const qreal by = ctr.y() + spread;
      const qreal cs = sym * 0.58;
      p.drawLine(QPointF(bx - cs, by - cs), QPointF(bx + cs, by + cs));
      p.drawLine(QPointF(bx + cs, by - cs), QPointF(bx - cs, by + cs));
    }

    // Square (left)
    {
      const qreal sq = sym * 0.58;
      p.drawRect(QRectF(ctr.x() - spread - sq, ctr.y() - sq, sq * 2, sq * 2));
    }

    p.setPen(Qt::NoPen);
    p.setBrush(Qt::NoBrush);
    break;
  }

  // ── Switch: Console in portable mode ──
  case HomeTileKind::Switch: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const auto grad = gradientFor(kind_, property("aio_selected").toBool());

    // Screen body (dark knockout)
    const qreal bw = s * 0.48;
    const qreal bh = s * 0.66;
    QRectF screen(ctr.x() - bw / 2, ctr.y() - bh / 2, bw, bh);
    QPainterPath scrPath;
    scrPath.addRoundedRect(screen, 4, 4);
    p.fillPath(scrPath, grad.bottom);

    // Screen bezel highlight
    p.setPen(QPen(QColor(255, 255, 255, 18), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(screen, 4, 4);
    p.setPen(Qt::NoPen);

    // Left Joy-Con rail
    const qreal jw = s * 0.13;
    QRectF lJoy(screen.left() - jw, screen.top(), jw, bh);
    QPainterPath lPath;
    lPath.addRoundedRect(lJoy, jw * 0.45, jw * 0.22);
    p.fillPath(lPath, col);

    // Right Joy-Con rail
    QRectF rJoy(screen.right(), screen.top(), jw, bh);
    QPainterPath rPath;
    rPath.addRoundedRect(rJoy, jw * 0.45, jw * 0.22);
    p.fillPath(rPath, col);

    // Left stick dot
    const qreal dotR = jw * 0.18;
    p.setBrush(grad.bottom);
    p.drawEllipse(QPointF(lJoy.center().x(), lJoy.top() + bh * 0.30), dotR,
                  dotR);
    // Right stick dot
    p.drawEllipse(QPointF(rJoy.center().x(), rJoy.top() + bh * 0.55), dotR,
                  dotR);
    p.setBrush(Qt::NoBrush);
    break;
  }

  // ── Media Server: Film-strip folder icon ──
  case HomeTileKind::MediaServer: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const auto grad = gradientFor(kind_, property("aio_selected").toBool());

    // Main screen/rectangle
    const qreal rw = s * 0.60;
    const qreal rh = s * 0.44;
    QRectF screen(ctr.x() - rw / 2, ctr.y() - rh / 2, rw, rh);
    QPainterPath screenPath;
    screenPath.addRoundedRect(screen, s * 0.03, s * 0.03);
    p.fillPath(screenPath, col);

    // Film perforations — left column
    const qreal perfW = rw * 0.08;
    const qreal perfH = rh * 0.12;
    const qreal perfGap = rh * 0.20;
    const QColor knock = grad.bottom;
    for (int i = 0; i < 3; ++i) {
      qreal py = screen.top() + rh * 0.12 + i * perfGap;
      QRectF perf(screen.left() + rw * 0.06, py, perfW, perfH);
      QPainterPath pp;
      pp.addRoundedRect(perf, 1.5, 1.5);
      p.fillPath(pp, knock);
    }
    // Film perforations — right column
    for (int i = 0; i < 3; ++i) {
      qreal py = screen.top() + rh * 0.12 + i * perfGap;
      QRectF perf(screen.right() - rw * 0.06 - perfW, py, perfW, perfH);
      QPainterPath pp;
      pp.addRoundedRect(perf, 1.5, 1.5);
      p.fillPath(pp, knock);
    }

    // Center play triangle (smaller, inside the film frame)
    const qreal triH = rh * 0.50;
    const qreal triW = triH * 0.80;
    QPainterPath tri;
    tri.moveTo(ctr.x() - triW * 0.30, ctr.y() - triH / 2);
    tri.lineTo(ctr.x() + triW * 0.55, ctr.y());
    tri.lineTo(ctr.x() - triW * 0.30, ctr.y() + triH / 2);
    tri.closeSubpath();
    p.fillPath(tri, knock);
    break;
  }

  // ── Settings: Modern slider controls ──
  case HomeTileKind::Settings: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const qreal lineW = s * 0.52;
    const qreal lineH = s * 0.028;
    const qreal gap = s * 0.19;
    const qreal dotR = s * 0.052;
    const qreal positions[] = {0.30, 0.68, 0.48};

    p.setPen(Qt::NoPen);
    for (int i = -1; i <= 1; ++i) {
      const qreal ly = ctr.y() + i * gap;
      const qreal lx = ctr.x() - lineW / 2;

      // Track line
      QRectF line(lx, ly - lineH / 2, lineW, lineH);
      QPainterPath lp;
      lp.addRoundedRect(line, lineH / 2, lineH / 2);
      p.fillPath(lp,
                 QColor(col.red(), col.green(), col.blue(), col.alpha() / 2));

      // Handle dot
      const qreal dotX = lx + lineW * positions[i + 1];
      p.setBrush(col);
      p.drawEllipse(QPointF(dotX, ly), dotR, dotR);
      p.setBrush(Qt::NoBrush);
    }
    break;
  }

  // ── YouTube: Iconic rounded-rect play button ──
  case HomeTileKind::YouTube: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();

    // Rounded-rectangle "button" background (like the real YouTube logo)
    const qreal btnW = s * 0.68;
    const qreal btnH = s * 0.48;
    const qreal btnR = btnH * 0.28;
    QRectF btnRect(ctr.x() - btnW / 2, ctr.y() - btnH / 2, btnW, btnH);

    // Shadow behind the button
    QPainterPath btnShadow;
    btnShadow.addRoundedRect(btnRect.translated(0, 3), btnR, btnR);
    p.fillPath(btnShadow, QColor(0, 0, 0, 55));

    // Red button background
    QPainterPath btnPath;
    btnPath.addRoundedRect(btnRect, btnR, btnR);
    p.fillPath(btnPath, QColor(255, 255, 255, 30));

    // White play triangle centered inside the button
    const qreal triH = btnH * 0.56;
    const qreal triW = triH * 0.82;
    const qreal cx = ctr.x() + triW * 0.08;

    QPainterPath tri;
    tri.moveTo(cx - triW * 0.36, ctr.y() - triH / 2);
    tri.lineTo(cx + triW * 0.58, ctr.y());
    tri.lineTo(cx - triW * 0.36, ctr.y() + triH / 2);
    tri.closeSubpath();
    p.fillPath(tri, col);
    break;
  }

  // ── Netflix: Bold red N — single united path, fully opaque ──
  case HomeTileKind::Netflix: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const QColor nRed(229, 9, 20); // fully opaque — never transparent

    const qreal nH = s * 0.68;
    const qreal nW = s * 0.48;
    const qreal barW = nW * 0.25;
    const qreal left = ctr.x() - nW / 2;
    const qreal right = ctr.x() + nW / 2;
    const qreal top = ctr.y() - nH / 2;
    const qreal bot = ctr.y() + nH / 2;

    // Build the entire N as one united path — no seams, no overlap artifacts
    QPainterPath leftBar;
    leftBar.addRoundedRect(QRectF(left, top, barW, nH), barW * 0.08,
                           barW * 0.08);
    QPainterPath rightBar;
    rightBar.addRoundedRect(QRectF(right - barW, top, barW, nH), barW * 0.08,
                            barW * 0.08);
    QPainterPath diag;
    diag.moveTo(left, top);
    diag.lineTo(left + barW, top);
    diag.lineTo(right, bot);
    diag.lineTo(right - barW, bot);
    diag.closeSubpath();

    QPainterPath fullN = leftBar.united(rightBar).united(diag);

    // Drop shadow
    const qreal shOff = 3.0;
    QPainterPath shadowN;
    shadowN.addPath(fullN.translated(shOff, shOff));
    p.fillPath(shadowN, QColor(0, 0, 0, 60));

    // Fill the N once, solid
    p.fillPath(fullN, nRed);

    // Subtle left-edge ribbon highlight for depth
    {
      QLinearGradient ribH(QPointF(left, top), QPointF(left + barW, top));
      ribH.setColorAt(0.0, QColor(255, 255, 255, 22));
      ribH.setColorAt(1.0, QColor(255, 255, 255, 0));
      p.fillPath(leftBar, ribH);
    }
    break;
  }

  // ── Disney+: Castle silhouette + wordmark ──
  case HomeTileKind::DisneyPlus: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();

    const qreal castleW = s * 1.0;
    const qreal castleH = s * 0.78;
    const qreal castleL = ctr.x() - castleW / 2;
    const qreal castleBot = ctr.y() + s * 0.02;

    struct Tower {
      qreal xOff, h, w;
    };
    const Tower towers[] = {
        {0.08, 0.38, 0.11}, {0.24, 0.54, 0.11}, {0.42, 0.78, 0.16},
        {0.63, 0.54, 0.11}, {0.81, 0.38, 0.11},
    };

    // Shadow pass for depth
    for (const auto &t : towers) {
      qreal tx = castleL + castleW * t.xOff + 2;
      qreal tw = castleW * t.w;
      qreal th = castleH * t.h;
      qreal ty = castleBot - th + 2;

      QRectF towerRect(tx, ty + th * 0.20, tw, th * 0.80);
      QPainterPath towerSh;
      towerSh.addRoundedRect(towerRect, tw * 0.06, tw * 0.06);
      p.fillPath(towerSh, QColor(0, 0, 0, 35));
    }

    for (const auto &t : towers) {
      qreal tx = castleL + castleW * t.xOff;
      qreal tw = castleW * t.w;
      qreal th = castleH * t.h;
      qreal ty = castleBot - th;

      // Tower body
      QRectF towerRect(tx, ty + th * 0.20, tw, th * 0.80);
      QPainterPath towerPath;
      towerPath.addRoundedRect(towerRect, tw * 0.06, tw * 0.06);
      p.fillPath(towerPath, col);

      // Pointed spire
      QPainterPath spire;
      spire.moveTo(tx + tw / 2, ty);
      spire.lineTo(tx - tw * 0.02, ty + th * 0.22);
      spire.lineTo(tx + tw * 1.02, ty + th * 0.22);
      spire.closeSubpath();
      p.fillPath(spire, col);

      // Tiny spire ball at top
      p.setBrush(col);
      p.drawEllipse(QPointF(tx + tw / 2, ty - th * 0.01), tw * 0.08, tw * 0.08);
      p.setBrush(Qt::NoBrush);
    }

    // Base wall connecting towers
    QRectF wall(castleL + castleW * 0.04, castleBot - castleH * 0.16,
                castleW * 0.92, castleH * 0.16);
    QPainterPath wallPath;
    wallPath.addRoundedRect(wall, s * 0.01, s * 0.01);
    p.fillPath(wallPath, col);

    // Center arch in the wall
    {
      const qreal archW = castleW * 0.10;
      const qreal archH = castleH * 0.12;
      const qreal archX = ctr.x() - archW / 2;
      const qreal archY = castleBot - archH;
      const auto grad = gradientFor(kind_, property("aio_selected").toBool());

      QPainterPath arch;
      arch.moveTo(archX, castleBot);
      arch.lineTo(archX, archY + archH * 0.4);
      arch.quadTo(archX, archY, archX + archW / 2, archY);
      arch.quadTo(archX + archW, archY, archX + archW, archY + archH * 0.4);
      arch.lineTo(archX + archW, castleBot);
      arch.closeSubpath();
      p.fillPath(arch, grad.bottom);
    }

    // DISNEY+ text
    QFont f = p.font();
    f.setPixelSize(static_cast<int>(s * 0.18));
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, s * 0.025);
    p.setFont(f);
    p.setPen(QColor(0, 0, 0, 50));
    QRectF textBox(ctr.x() - s * 0.5, castleBot + s * 0.04, s, s * 0.22);
    p.drawText(textBox.translated(0, 1.5), Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("DISNEY+"));
    p.setPen(col);
    p.drawText(textBox, Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("DISNEY+"));
    p.setPen(Qt::NoPen);
    break;
  }

  // ── Hulu: Bold green HULU wordmark ──
  case HomeTileKind::Hulu: {
    const qreal s = qMin(box.width(), box.height());
    const qreal fp2 = focusProgress_;
    const QColor huluWhite = QColor(255, 255, 255);

    QFont f(QStringLiteral("Noto Sans"));
    f.setPixelSize(static_cast<int>(s * 0.36));
    f.setWeight(QFont::ExtraBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, s * 0.04);
    f.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(f);

    // Use full tile width for text centering
    const QRectF tileR = QRectF(rect()).adjusted(10, 0, -10, 0);
    const QRectF textBox(tileR.left(), box.top(), tileR.width(), box.height());

    // Drop shadow for depth
    p.setPen(QColor(0, 0, 0, 70));
    p.drawText(textBox.translated(0, 2.5), Qt::AlignCenter,
               QStringLiteral("HULU"));

    // Main text
    p.setPen(huluWhite);
    p.drawText(textBox, Qt::AlignCenter, QStringLiteral("HULU"));
    p.setPen(Qt::NoPen);
    break;
  }

  case HomeTileKind::ScreenMirror: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const qreal w = s * 0.78;
    const qreal h = s * 0.46;
    QRectF screen(ctr.x() - w / 2, ctr.y() - h / 2 - s * 0.05, w, h);
    QPainterPath frame;
    frame.addRoundedRect(screen, s * 0.05, s * 0.05);
    p.fillPath(frame, col);
    p.fillPath(frame, QColor(255, 255, 255, 24));

    p.setPen(QPen(col, qMax(2.0, s * 0.04), Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    const qreal arc = s * 0.28;
    p.drawArc(QRectF(ctr.x() - arc / 2, ctr.y() + h * 0.12, arc, arc), 32 * 16,
              116 * 16);
    p.drawArc(QRectF(ctr.x() - arc * 0.78, ctr.y() + h * 0.22, arc * 1.56,
                     arc * 1.56),
              35 * 16, 110 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawEllipse(QPointF(ctr.x(), ctr.y() + h * 0.46), s * 0.045, s * 0.045);
    break;
  }

  case HomeTileKind::Store: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const qreal w = s * 0.62;
    const qreal h = s * 0.54;
    QRectF bag(ctr.x() - w / 2, ctr.y() - h * 0.10, w, h);
    QPainterPath bagPath;
    bagPath.addRoundedRect(bag, s * 0.07, s * 0.07);
    p.fillPath(bagPath, col);
    p.fillPath(bagPath, QColor(255, 255, 255, 22));

    p.setPen(QPen(QColor(255, 255, 255, 205), qMax(2.0, s * 0.035),
                  Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    const qreal hw = w * 0.42;
    const qreal hh = h * 0.42;
    p.drawArc(QRectF(ctr.x() - hw / 2, bag.top() - hh * 0.52, hw, hh), 0,
              180 * 16);
    p.setPen(Qt::NoPen);
    break;
  }

  case HomeTileKind::Library: {
    const qreal s = qMin(box.width(), box.height());
    const QPointF ctr = box.center();
    const QRectF outer(ctr.x() - s * 0.28, ctr.y() - s * 0.28, s * 0.56,
                       s * 0.56);
    QPainterPath outerPath;
    outerPath.addRoundedRect(outer, s * 0.06, s * 0.06);
    p.fillPath(outerPath, col);

    const QColor inner = QColor(255, 255, 255, 44);
    const qreal cell = s * 0.12;
    const qreal gap = s * 0.08;
    const qreal left = outer.left() + gap;
    const qreal top = outer.top() + gap;
    for (int row = 0; row < 3; ++row) {
      for (int colIdx = 0; colIdx < 3; ++colIdx) {
        if (row == 1 && colIdx == 1)
          continue;
        p.fillRect(QRectF(left + colIdx * (cell + gap),
                          top + row * (cell + gap), cell, cell),
                   inner);
      }
    }
    break;
  }

  case HomeTileKind::Blank:
    break;

  } // switch
}

// ---------------------------------------------------------------------------
// HomeScreen
// ---------------------------------------------------------------------------

HomeScreen::HomeScreen(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("aioHomeScreen"));
  setFocusPolicy(Qt::StrongFocus);
  setupUi();
  updateFocus();
}

void HomeScreen::setupUi() {
  const int blanksEnabled = qEnvironmentVariableIntValue("HOMESCREEN_BLANKS");
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);
  {
    auto *hdr = new QWidget(this);
    hdr->setObjectName(QStringLiteral("aioHomeTopBar"));
    hdr->setFixedHeight(52);
    auto *hdrLay = new QHBoxLayout(hdr);
    hdrLay->setContentsMargins(40, 10, 40, 0);
    auto *appTitle =
        new QLabel(QStringLiteral("AIO ENTERTAINMENT SYSTEM"), hdr);
    appTitle->setObjectName(QStringLiteral("aioHomeBrand"));
    hdrLay->addWidget(appTitle);
    hdrLay->addStretch();
    auto *clockLabel = new QLabel(hdr);
    clockLabel->setObjectName(QStringLiteral("aioHomeClock"));
    clockLabel->setText(
        QTime::currentTime().toString(QStringLiteral("h:mm AP")));
    hdrLay->addWidget(clockLabel);
    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, clockLabel, [clockLabel]() {
      clockLabel->setText(
          QTime::currentTime().toString(QStringLiteral("h:mm AP")));
    });
    clockTimer->start(60000);
    outerLayout->addWidget(hdr);
  }
  {
    auto *hero = new QWidget(this);
    hero->setObjectName(QStringLiteral("aioHomeHero"));
    auto *heroLay = new QVBoxLayout(hero);
    heroLay->setContentsMargins(52, 18, 52, 20);
    heroLay->setSpacing(6);
    eyebrowLabel_ = new QLabel(QStringLiteral("HOME"), hero);
    eyebrowLabel_->setObjectName(QStringLiteral("aioHomeHeroEyebrow"));
    heroLay->addWidget(eyebrowLabel_);
    titleLabel_ = new QLabel(QStringLiteral("Welcome Back"), hero);
    titleLabel_->setObjectName(QStringLiteral("aioHomeHeroTitle"));
    heroLay->addWidget(titleLabel_);
    subtitleLabel_ = new QLabel(
        QStringLiteral("Pick a tile and press Select to launch."), hero);
    subtitleLabel_->setObjectName(QStringLiteral("aioHomeHeroSubtitle"));
    subtitleLabel_->setWordWrap(true);
    heroLay->addWidget(subtitleLabel_);
    auto *chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 4, 0, 0);
    chipRow->setSpacing(10);
    modeChipLabel_ = new QLabel(QStringLiteral("BROWSE MODE"), hero);
    modeChipLabel_->setObjectName(QStringLiteral("aioHomeModeChip"));
    chipRow->addWidget(modeChipLabel_);
    hintLabel_ = new QLabel(
        QStringLiteral(
            "D-pad navigate  \u00b7  Select launch  \u00b7  Home returns here"),
        hero);
    hintLabel_->setObjectName(QStringLiteral("aioHomeHint"));
    chipRow->addWidget(hintLabel_);
    chipRow->addStretch();
    heroLay->addLayout(chipRow);
    outerLayout->addWidget(hero);
  }
  if (blanksEnabled) {
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"));
    scrollContent_ = new QWidget();
    scrollContent_->setObjectName(QStringLiteral("aioHomeScrollContent"));
    scrollContent_->setStyleSheet(
        QStringLiteral("#aioHomeScrollContent { background: transparent; }"));
    gridHost_ = scrollContent_;
    outerLayout->addWidget(scrollArea_, 1);
  } else {
    auto *gridWidget = new QWidget(this);
    gridWidget->setObjectName(QStringLiteral("aioHomeGridHost"));
    scrollContent_ = gridWidget;
    gridHost_ = gridWidget;
    outerLayout->addWidget(gridWidget, 1);
  }
  gridLayout_ = new QGridLayout(gridHost_);
  gridLayout_->setContentsMargins(52, 18, 52, 52);
  gridLayout_->setHorizontalSpacing(14);
  gridLayout_->setVerticalSpacing(16);
  for (int c = 0; c < 12; ++c)
    gridLayout_->setColumnStretch(c, 1);
  if (blanksEnabled)
    scrollArea_->setWidget(scrollContent_);
  loadTileOrder();
  rebuildGrid();
}

void HomeScreen::loadTileOrder() {
  QSettings s;
  const QString orderStr = s.value(QStringLiteral("home/tileOrder")).toString();
  const QString hiddenStr =
      s.value(QStringLiteral("home/hiddenTiles")).toString();
  tileOrder_.clear();
  hiddenTiles_.clear();
  if (!orderStr.isEmpty()) {
    for (const auto &tok : orderStr.split(QLatin1Char(','))) {
      bool ok = false;
      const int v = tok.trimmed().toInt(&ok);
      if (ok)
        tileOrder_.append(static_cast<HomeTileKind>(v));
    }
  }
  if (tileOrder_.isEmpty()) {
    tileOrder_ = {HomeTileKind::YouTube,     HomeTileKind::Netflix,
                  HomeTileKind::DisneyPlus,  HomeTileKind::Hulu,
                  HomeTileKind::MediaServer, HomeTileKind::ScreenMirror,
                  HomeTileKind::Store,       HomeTileKind::Library,
                  HomeTileKind::GBA,         HomeTileKind::PS1,
                  HomeTileKind::Switch,      HomeTileKind::Settings};
  }
  if (!hiddenStr.isEmpty()) {
    for (const auto &tok : hiddenStr.split(QLatin1Char(','))) {
      bool ok = false;
      const int v = tok.trimmed().toInt(&ok);
      if (ok)
        hiddenTiles_.append(static_cast<HomeTileKind>(v));
    }
  }
}

void HomeScreen::saveTileOrder() const {
  QSettings s;
  QStringList orderList, hiddenList;
  for (auto k : tileOrder_)
    orderList << QString::number(static_cast<int>(k));
  for (auto k : hiddenTiles_)
    hiddenList << QString::number(static_cast<int>(k));
  s.setValue(QStringLiteral("home/tileOrder"),
             orderList.join(QLatin1Char(',')));
  s.setValue(QStringLiteral("home/hiddenTiles"),
             hiddenList.join(QLatin1Char(',')));
}

void HomeScreen::rebuildGrid(HomeTileKind preferredKind) {
  // 1. Delete existing tile widgets
  for (auto &row : rows_)
    for (auto *t : row)
      delete t;
  rows_.clear();

  // 2. Drain layout items
  while (gridLayout_->count() > 0) {
    QLayoutItem *item = gridLayout_->takeAt(0);
    delete item;
  }

  // 3. Delete section header labels
  const QObjectList kids = gridHost_->children();
  for (QObject *child : kids) {
    QLabel *lbl = qobject_cast<QLabel *>(child);
    if (lbl && lbl->objectName() == QLatin1String("aioHomeSectionHeader"))
      delete lbl;
  }

  // 4. Row stretches
  gridLayout_->setRowStretch(0, 0);
  gridLayout_->setRowStretch(1, 4);
  gridLayout_->setRowMinimumHeight(1, 186);
  gridLayout_->setRowStretch(2, 0);
  gridLayout_->setRowStretch(3, 4);
  gridLayout_->setRowMinimumHeight(3, 186);
  gridLayout_->setRowStretch(4, 0);
  gridLayout_->setRowStretch(5, 4);
  gridLayout_->setRowMinimumHeight(5, 186);

  // 5. Section header labels
  const char *sectionNames[] = {"Watch Now", "Apps & Media", "Play"};
  const int sectionGridRows[] = {0, 2, 4};
  for (int i = 0; i < 3; ++i) {
    auto *lbl = new QLabel(QString::fromLatin1(sectionNames[i]), gridHost_);
    lbl->setObjectName(QStringLiteral("aioHomeSectionHeader"));
    lbl->setFixedHeight(30);
    gridLayout_->addWidget(lbl, sectionGridRows[i], 0, 1, 12);
  }

  // 6. Lay visible tiles: 4-per-logical-row in a 12-column grid
  QVector<HomeTile *> currentRow;
  int visibleIndex = 0;
  for (HomeTileKind k : tileOrder_) {
    if (hiddenTiles_.contains(k))
      continue;
    const int tileLogicalRow = visibleIndex / 4;
    const int tileCol = visibleIndex % 4;
    auto *tile = new HomeTile(k, gridHost_);
    tile->setMinimumHeight(184);
    connect(tile, &HomeTile::clicked, this,
            [this, k]() { activateTileKind(k); });
    gridLayout_->addWidget(tile, tileLogicalRow * 2 + 1, tileCol * 3, 1, 3);
    currentRow.append(tile);
    if (tileCol == 3) {
      rows_.append(currentRow);
      currentRow.clear();
    }
    ++visibleIndex;
  }
  if (!currentRow.isEmpty())
    rows_.append(currentRow);

  // Restore preferred focus
  if (preferredKind != HomeTileKind::Blank)
    findTile(preferredKind, focusRow_, focusCol_);

  // Clamp focus to valid range
  if (rows_.isEmpty()) {
    focusRow_ = 0;
    focusCol_ = 0;
  } else {
    focusRow_ = qBound(0, focusRow_, rows_.size() - 1);
    focusCol_ = qBound(0, focusCol_, rows_[focusRow_].size() - 1);
  }

  updateFocus();
}

void HomeScreen::activateTileKind(HomeTileKind kind) {
  switch (kind) {
  case HomeTileKind::GBA:
    emit gbaRequested();
    break;
  case HomeTileKind::PS1:
    emit ps1Requested();
    break;
  case HomeTileKind::Switch:
    emit switchRequested();
    break;
  case HomeTileKind::MediaServer:
    emit nasRequested();
    break;
  case HomeTileKind::ScreenMirror:
    emit screenMirrorRequested();
    break;
  case HomeTileKind::Settings:
    emit settingsRequested();
    break;
  case HomeTileKind::YouTube:
    emit streamingAppRequested(StreamingApp::YouTube);
    break;
  case HomeTileKind::Netflix:
    emit streamingAppRequested(StreamingApp::Netflix);
    break;
  case HomeTileKind::DisneyPlus:
    emit streamingAppRequested(StreamingApp::DisneyPlus);
    break;
  case HomeTileKind::Hulu:
    emit streamingAppRequested(StreamingApp::Hulu);
    break;
  case HomeTileKind::Store:
    emit storeRequested();
    break;
  case HomeTileKind::Library:
    emit libraryRequested();
    break;
  case HomeTileKind::Blank:
    break;
  }
}

void HomeScreen::toggleOrganizeMode() {
  organizeMode_ = !organizeMode_;
  updateHero();
  if (!organizeMode_ && addAppsOverlay_)
    hideAddAppsOverlay();
}

void HomeScreen::resetLayout() { rebuildGrid(); }

void HomeScreen::moveFocusedTile(int rowDelta, int colDelta) {
  const int tilesPerRow = 4;
  const int fromLinear = focusRow_ * tilesPerRow + focusCol_;
  const int toLinear =
      (focusRow_ + rowDelta) * tilesPerRow + (focusCol_ + colDelta);

  QVector<HomeTileKind> visible;
  for (auto k : tileOrder_)
    if (!hiddenTiles_.contains(k))
      visible.append(k);

  if (fromLinear < 0 || fromLinear >= visible.size())
    return;
  if (toLinear < 0 || toLinear >= visible.size())
    return;

  const HomeTileKind fromKind = visible[fromLinear];
  const HomeTileKind toKind = visible[toLinear];
  const int realFrom = tileOrder_.indexOf(fromKind);
  const int realTo = tileOrder_.indexOf(toKind);
  if (realFrom < 0 || realTo < 0)
    return;
  std::swap(tileOrder_[realFrom], tileOrder_[realTo]);

  saveTileOrder();
  rebuildGrid(fromKind);
}

void HomeScreen::hideFocusedTile() {
  HomeTile *tile = currentFocusedTile();
  if (!tile || tile->kind() == HomeTileKind::Blank)
    return;
  const HomeTileKind k = tile->kind();
  tileOrder_.removeAll(k);
  hiddenTiles_.append(k);
  focusCol_ = qMin(
      focusCol_,
      qMax(0,
           (rows_.isEmpty() ? 0 : static_cast<int>(rows_[focusRow_].size())) -
               1));
  saveTileOrder();
  rebuildGrid();
}

void HomeScreen::showAddAppsOverlay() {
  if (hiddenTiles_.isEmpty())
    return;
  if (addAppsOverlay_)
    hideAddAppsOverlay();

  addAppsOverlay_ = new QWidget(this);
  addAppsOverlay_->setObjectName(QStringLiteral("aioAddAppsOverlay"));
  addAppsOverlay_->setFixedSize(
      480, qMin(600, 120 + static_cast<int>(hiddenTiles_.size()) * 64));

  addAppsLayout_ = new QVBoxLayout(addAppsOverlay_);
  addAppsLayout_->setContentsMargins(32, 32, 32, 32);
  addAppsLayout_->setSpacing(12);

  auto *title = new QLabel(QStringLiteral("Restore App"), addAppsOverlay_);
  title->setObjectName(QStringLiteral("aioAddAppsTitle"));
  addAppsLayout_->addWidget(title);

  auto *hint = new QLabel(
      QStringLiteral(
          "D-pad select  \u00b7  Enter restore  \u00b7  Back cancel"),
      addAppsOverlay_);
  hint->setObjectName(QStringLiteral("aioAddAppsHint"));
  addAppsLayout_->addWidget(hint);

  addAppsFocus_ = 0;
  for (int i = 0; i < hiddenTiles_.size(); ++i) {
    auto *item = new QLabel(QString::fromLatin1(labelFor(hiddenTiles_[i])),
                            addAppsOverlay_);
    item->setObjectName(QStringLiteral("aioAddAppsItem"));
    item->setProperty("aio_selected", i == addAppsFocus_);
    addAppsLayout_->addWidget(item);
  }

  addAppsOverlay_->move((width() - addAppsOverlay_->width()) / 2,
                        (height() - addAppsOverlay_->height()) / 2);
  addAppsOverlay_->show();
  addAppsOverlay_->raise();
}

void HomeScreen::hideAddAppsOverlay() {
  if (addAppsOverlay_) {
    addAppsOverlay_->deleteLater();
    addAppsOverlay_ = nullptr;
    addAppsLayout_ = nullptr;
  }
}

void HomeScreen::unhideTile(HomeTileKind kind) {
  hiddenTiles_.removeAll(kind);
  if (!tileOrder_.contains(kind))
    tileOrder_.append(kind);
  saveTileOrder();
  hideAddAppsOverlay();
  rebuildGrid();
}

int HomeScreen::currentColumnCount() const { return 4; }

HomeTile *HomeScreen::tileForKind(HomeTileKind kind) const {
  for (const auto &row : rows_)
    for (auto *t : row)
      if (t->kind() == kind)
        return t;
  return nullptr;
}

bool HomeScreen::findTile(HomeTileKind kind, int &row, int &col) const {
  for (int r = 0; r < rows_.size(); ++r)
    for (int c = 0; c < rows_[r].size(); ++c)
      if (rows_[r][c]->kind() == kind) {
        row = r;
        col = c;
        return true;
      }
  return false;
}

int HomeScreen::colsInRow(int row) const {
  if (row < 0 || row >= rows_.size())
    return 0;
  return rows_[row].size();
}

HomeTile *HomeScreen::currentFocusedTile() const {
  if (focusRow_ < 0 || focusRow_ >= rows_.size())
    return nullptr;
  if (focusCol_ < 0 || focusCol_ >= rows_[focusRow_].size())
    return nullptr;
  return rows_[focusRow_][focusCol_];
}

void HomeScreen::updateHero() {
  if (!titleLabel_ || !subtitleLabel_ || !eyebrowLabel_ || !modeChipLabel_ ||
      !hintLabel_) {
    return;
  }

  HomeTile *tile = currentFocusedTile();
  if (!tile) {
    eyebrowLabel_->setText(QStringLiteral("HOME"));
    titleLabel_->setText(QStringLiteral("Welcome Back"));
    subtitleLabel_->setText(QString());
    modeChipLabel_->setVisible(false);
    hintLabel_->setVisible(false);
    return;
  }

  const char *sectionNames[] = {"Watch Now", "Apps & Media", "Play"};
  const int sectionIdx = qBound(0, focusRow_, 2);
  eyebrowLabel_->setText(QString::fromLatin1(sectionNames[sectionIdx]));

  const QString focusedTitle = QString::fromLatin1(labelFor(tile->kind()));
  titleLabel_->setText(focusedTitle);
  subtitleLabel_->setText(QString::fromLatin1(subtitleFor(tile->kind())));

  modeChipLabel_->setVisible(organizeMode_);
  if (organizeMode_) {
    modeChipLabel_->setText(QStringLiteral("ORGANIZE MODE"));
    modeChipLabel_->setProperty("mode", "organize");
    modeChipLabel_->style()->unpolish(modeChipLabel_);
    modeChipLabel_->style()->polish(modeChipLabel_);
  }

  hintLabel_->setVisible(organizeMode_);
  if (organizeMode_) {
    hintLabel_->setText(QStringLiteral("Move: D-pad  \u00b7  Hide: Delete  "
                                       "\u00b7  Add: A  \u00b7  Exit: Esc"));
  }
}

void HomeScreen::updateFocus() {
  for (int r = 0; r < rows_.size(); ++r) {
    for (int c = 0; c < rows_[r].size(); ++c) {
      const bool selected = (focusRow_ == r && focusCol_ == c);
      auto *tile = rows_[r][c];

      const qreal target = selected ? 1.0 : 0.0;

      // Skip tiles whose state hasn't changed.
      if (tile->property("aio_selected").toBool() == selected &&
          qFuzzyCompare(tile->focusProgress(), target))
        continue;

      tile->setProperty("aio_selected", selected);

      auto *anim =
          tile->findChild<QPropertyAnimation *>(QStringLiteral("focusAnim"));
      if (!anim) {
        anim = new QPropertyAnimation(tile, "focusProgress", tile);
        anim->setObjectName(QStringLiteral("focusAnim"));
        anim->setDuration(260);
        anim->setEasingCurve(QEasingCurve::OutCubic);
      }
      if (anim->state() == QAbstractAnimation::Running)
        anim->stop();
      anim->setStartValue(tile->focusProgress());
      anim->setEndValue(target);
      anim->start();

      if (selected)
        tile->raise();
    }
  }
  updateHero();
  update();
}

void HomeScreen::ensureFocusVisible() {
  if (focusRow_ < 0 || focusRow_ >= rows_.size())
    return;
  if (focusCol_ < 0 || focusCol_ >= rows_[focusRow_].size())
    return;
  QWidget *tile = rows_[focusRow_][focusCol_];
  if (scrollArea_)
    scrollArea_->ensureWidgetVisible(tile, 20, 20);
}

void HomeScreen::activateFocusedTile() {
  HomeTile *tile = currentFocusedTile();
  if (!tile)
    return;
  activateTileKind(tile->kind());
}

void HomeScreen::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  const QRectF r(rect());

  QRadialGradient rg(r.center(), r.width() * 0.75);
  rg.setColorAt(0.0, QColor(22, 26, 48));
  rg.setColorAt(0.40, QColor(12, 15, 30));
  rg.setColorAt(1.0, QColor(4, 5, 10));
  p.fillRect(r, rg);

  if (focusRow_ >= 0 && focusRow_ < rows_.size() && focusCol_ >= 0 &&
      focusCol_ < rows_[focusRow_].size()) {
    HomeTile *tile = rows_[focusRow_][focusCol_];
    QColor glow = ambientGlowFor(tile->kind());
    glow.setAlpha(22);
    QLinearGradient wash(0, 0, width() * 0.55, 0);
    wash.setColorAt(0.0, glow);
    wash.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(r, wash);
  }

  if (focusRow_ >= 0 && focusRow_ < rows_.size() && focusCol_ >= 0 &&
      focusCol_ < rows_[focusRow_].size()) {
    HomeTile *tile = rows_[focusRow_][focusCol_];
    if (tile->kind() != HomeTileKind::Blank) {
      const qreal fp = tile->focusProgress();
      if (fp > 0.01) {
        const QColor gc = ambientGlowFor(tile->kind());
        const QPoint tileOrigin = tile->mapTo(this, QPoint(0, 0));
        const QRectF tg(tileOrigin, tile->size());
        const qreal gp = qBound(0.0, fp, 1.0);
        {
          const qreal expand = 40.0 * gp;
          const QRectF glowRect = tg.adjusted(-expand, -expand, expand, expand);
          QRadialGradient rGlow(tg.center(),
                                qMax(tg.width(), tg.height()) * 0.85);
          rGlow.setColorAt(0.0, QColor(gc.red(), gc.green(), gc.blue(),
                                       static_cast<int>(80 * gp)));
          rGlow.setColorAt(0.45, QColor(gc.red(), gc.green(), gc.blue(),
                                        static_cast<int>(28 * gp)));
          rGlow.setColorAt(1.0, QColor(gc.red(), gc.green(), gc.blue(), 0));
          QPainterPath glowPath;
          glowPath.addRoundedRect(glowRect, 36, 36);
          p.fillPath(glowPath, rGlow);
        }
      }
    }
  }
}

void HomeScreen::keyPressEvent(QKeyEvent *event) {
  // --- Add-apps overlay navigation takes full priority ---
  if (addAppsOverlay_) {
    const int itemOffset = 2; // title + hint labels precede item entries
    const int numItems = addAppsLayout_->count() - itemOffset;

    auto refreshOverlaySelection = [&]() {
      for (int i = 0; i < numItems; ++i) {
        QLayoutItem *li = addAppsLayout_->itemAt(itemOffset + i);
        if (!li)
          continue;
        auto *item = qobject_cast<QLabel *>(li->widget());
        if (item) {
          item->setProperty("aio_selected", i == addAppsFocus_);
          item->style()->unpolish(item);
          item->style()->polish(item);
        }
      }
    };

    switch (event->key()) {
    case Qt::Key_Up:
      if (addAppsFocus_ > 0) {
        --addAppsFocus_;
        refreshOverlaySelection();
      }
      event->accept();
      return;
    case Qt::Key_Down:
      if (addAppsFocus_ < numItems - 1) {
        ++addAppsFocus_;
        refreshOverlaySelection();
      }
      event->accept();
      return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
      if (addAppsFocus_ >= 0 && addAppsFocus_ < hiddenTiles_.size())
        unhideTile(hiddenTiles_[addAppsFocus_]);
      event->accept();
      return;
    case Qt::Key_Escape:
    case Qt::Key_Back:
      hideAddAppsOverlay();
      event->accept();
      return;
    default:
      event->accept();
      return;
    }
  }

  // --- Normal / Organize grid navigation ---
  const int maxCol = colsInRow(focusRow_) - 1;
  const int maxRow = rows_.size() - 1;

  switch (event->key()) {
  case Qt::Key_Left:
    if (organizeMode_) {
      if (focusCol_ > 0)
        moveFocusedTile(0, -1);
    } else {
      if (focusCol_ > 0) {
        --focusCol_;
        updateFocus();
      }
    }
    event->accept();
    return;

  case Qt::Key_Right:
    if (organizeMode_) {
      if (focusCol_ < maxCol)
        moveFocusedTile(0, 1);
    } else {
      if (focusCol_ < maxCol) {
        ++focusCol_;
        updateFocus();
      }
    }
    event->accept();
    return;

  case Qt::Key_Up:
    if (organizeMode_) {
      if (focusRow_ > 0)
        moveFocusedTile(-1, 0);
    } else {
      if (focusRow_ > 0) {
        --focusRow_;
        const int newMax = colsInRow(focusRow_) - 1;
        if (focusCol_ > newMax)
          focusCol_ = newMax;
        updateFocus();
        ensureFocusVisible();
      }
    }
    event->accept();
    return;

  case Qt::Key_Down:
    if (organizeMode_) {
      if (focusRow_ < maxRow)
        moveFocusedTile(1, 0);
    } else {
      if (focusRow_ < maxRow) {
        ++focusRow_;
        const int newMax = colsInRow(focusRow_) - 1;
        if (focusCol_ > newMax)
          focusCol_ = newMax;
        updateFocus();
        ensureFocusVisible();
      }
    }
    event->accept();
    return;

  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    activateFocusedTile();
    event->accept();
    return;

  case Qt::Key_O:
    toggleOrganizeMode();
    event->accept();
    return;

  case Qt::Key_Escape:
  case Qt::Key_Back:
    if (organizeMode_) {
      toggleOrganizeMode();
      event->accept();
      return;
    }
    break;

  case Qt::Key_Delete:
  case Qt::Key_Backspace:
    if (organizeMode_) {
      hideFocusedTile();
      event->accept();
      return;
    }
    break;

  case Qt::Key_A:
    if (organizeMode_) {
      if (addAppsOverlay_)
        hideAddAppsOverlay();
      else
        showAddAppsOverlay();
      event->accept();
      return;
    }
    break;

  default:
    break;
  }

  QWidget::keyPressEvent(event);
}

void HomeScreen::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
}

void HomeScreen::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  updateFocus();
}

} // namespace GUI
} // namespace AIO
