// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractButton>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>

/// Painted pieces shared by the console-style views (home, platforms).
namespace ConsoleWidgets {

/// Windows' icon font (Segoe Fluent Icons on 11, MDL2 Assets on 10); elsewhere the buttons show
/// plain symbols instead.
inline QString IconFamily() {
    static const QString family = [] {
        const QStringList families = QFontDatabase::families();
        for (const auto* name : {"Segoe Fluent Icons", "Segoe MDL2 Assets"}) {
            if (families.contains(QLatin1String(name))) {
                return QString::fromLatin1(name);
            }
        }
        return QString{};
    }();
    return family;
}

/// A glyph of the icon font, or `fallback` without it.
inline QString Glyph(char16_t code, const QString& fallback) {
    return IconFamily().isEmpty() ? fallback : QString{QChar{code}};
}

/// `image` scaled to cover `size`, cropped centrally.
inline QImage Covering(const QImage& image, QSize size) {
    const QImage scaled =
        image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return scaled.copy((scaled.width() - size.width()) / 2,
                       (scaled.height() - size.height()) / 2, size.width(), size.height());
}

/// A soft blur: shrink to a few pixels, then scale back up smoothly.
inline QImage Blurred(const QImage& image, QSize size) {
    const QImage tiny = image.scaled(18, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return tiny.scaled(size.width() / 6 + 1, size.height() / 6 + 1, Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation)
        .scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

/// A console-style button, painted here so the app's theme stylesheet can't reshape it: a white
/// pill for the main action, a translucent pill, or a circle with an icon.
class ConsoleButton : public QAbstractButton {
public:
    enum class Kind { Primary, Pill, Round };

    ConsoleButton(Kind kind_, const QString& glyph_, const QString& label, QWidget* parent)
        : QAbstractButton{parent}, kind{kind_}, glyph{glyph_} {
        setText(kind == Kind::Round ? QString{} : label);
        setToolTip(kind == Kind::Round ? label : QString{});
        setAccessibleName(label);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover);
    }

    QSize sizeHint() const override {
        if (kind == Kind::Round) {
            return {48, 48};
        }
        QFont font = TextFont();
        const int text_width = QFontMetrics{font}.horizontalAdvance(text());
        const int glyph_width = glyph.isEmpty() ? 0 : 26;
        return kind == Kind::Primary ? QSize{text_width + glyph_width + 80, 50}
                                     : QSize{text_width + glyph_width + 38, 38};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        if (!isEnabled()) {
            painter.setOpacity(0.35);
        }
        const bool hot = underMouse() && isEnabled();
        const QRectF bounds = QRectF(rect()).adjusted(3, 3, -3, -3);
        const double radius = bounds.height() / 2;

        QColor fill = kind == Kind::Primary ? QColor(Qt::white) : QColor(255, 255, 255, 26);
        if (hot) {
            fill = kind == Kind::Primary ? QColor(0xdc, 0xf2, 0xff) : QColor(255, 255, 255, 52);
        }
        if (isDown()) {
            fill = fill.darker(115);
        }
        painter.setPen(kind == Kind::Primary ? Qt::NoPen : QPen(QColor(255, 255, 255, 40), 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(bounds, radius, radius);
        if (hasFocus()) {
            painter.setPen(QPen(Qt::white, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius + 2, radius + 2);
        }

        const QColor ink = kind == Kind::Primary ? QColor(0x07, 0x09, 0x10) : QColor(0xee, 0xf2, 0xfa);
        painter.setPen(ink);
        QFont icon_font = IconFamily().isEmpty() ? font() : QFont{IconFamily()};
        icon_font.setPixelSize(kind == Kind::Round ? 18 : 15);
        if (kind == Kind::Round) {
            painter.setFont(icon_font);
            painter.drawText(bounds, Qt::AlignCenter, glyph);
            return;
        }
        const QFont font = TextFont();
        const int text_width = QFontMetrics{font}.horizontalAdvance(text());
        const int glyph_width = glyph.isEmpty() ? 0 : 26;
        double x = bounds.center().x() - (text_width + glyph_width) / 2.0;
        if (!glyph.isEmpty()) {
            painter.setFont(icon_font);
            painter.drawText(QRectF(x, bounds.top(), 20, bounds.height()), Qt::AlignCenter, glyph);
            x += glyph_width;
        }
        painter.setFont(font);
        painter.drawText(QRectF(x, bounds.top(), text_width + 2, bounds.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    QFont TextFont() const {
        QFont text_font = font();
        text_font.setPixelSize(kind == Kind::Primary ? 17 : 14);
        text_font.setWeight(kind == Kind::Primary ? QFont::ExtraBold : QFont::DemiBold);
        return text_font;
    }

    Kind kind;
    QString glyph;
};

inline QAbstractButton* RoundButton(const QString& glyph, const QString& label, QWidget* parent) {
    return new ConsoleButton(ConsoleButton::Kind::Round, glyph, label, parent);
}

} // namespace ConsoleWidgets
