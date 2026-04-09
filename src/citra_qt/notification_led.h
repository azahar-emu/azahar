// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QColor>
#include <QWidget>

class LedWidget : public QWidget {
    Q_OBJECT

public:
    explicit LedWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    void setColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor blendLedColor(int r, int g, int b) const;
    static QColor lerpColor(const QColor& a, const QColor& b, float t);

private:
    QColor color;
};
