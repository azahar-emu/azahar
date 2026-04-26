// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QRadialGradient>
#include "citra_qt/notification_led.h"

LedWidget::LedWidget(QWidget* parent) : QWidget(parent), color(0, 0, 0) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize LedWidget::sizeHint() const {
    return QSize(16, 16);
}

QSize LedWidget::minimumSizeHint() const {
    return QSize(16, 16);
}

void LedWidget::setColor(const QColor& _color) {
    if (color == _color)
        return;

    color = _color;
    update();
}

QColor LedWidget::lerpColor(const QColor& a, const QColor& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);

    return QColor(int(a.red() + (b.red() - a.red()) * t),
                  int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

QColor LedWidget::blendLedColor(int r, int g, int b) const {
    // Default "off" color
    const QColor off_color(64, 64, 64);

    // If completely off, just show gray and skip further calculations
    if (r == 0 && g == 0 && b == 0)
        return off_color;

    // Normalize lit color so hue stays pure
    int max_c = std::max({r, g, b});
    QColor lit_color((r * 255) / max_c, (g * 255) / max_c, (b * 255) / max_c);

    // Convert PWM duty to perceived brightness.
    // This gives better results as LED RGB values
    // are not linear.
    constexpr float gamma = 2.4f;
    float pwm = max_c / 255.0;
    float t = std::pow(pwm, 1.f / gamma);

    return lerpColor(off_color, lit_color, t * 0.8f);
}

void LedWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF rect = this->rect().adjusted(0, 2, 0, -2);

    qreal size = std::min(rect.width(), rect.height());
    QRectF circle((rect.center().x() - size / 2.f) - 2, rect.center().y() - size / 2.f, size, size);

    QPointF center = circle.center();
    qreal radius = circle.width() / 2.f;

    QColor base = blendLedColor(color.red(), color.green(), color.blue());

    QRadialGradient g(center, radius);

    QColor inner = base.lighter(135);
    QColor outer = base.darker(125);

    g.setColorAt(0.f, inner);
    g.setColorAt(0.7f, base);
    g.setColorAt(1.f, outer);

    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(circle);
}
