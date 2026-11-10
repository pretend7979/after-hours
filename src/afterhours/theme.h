// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

#include <QAbstractButton>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QString>

#include "afterhours/console_widgets.h"

/// After Hours' look: dark panels, a lavender primary and a lime accent.
namespace Theme {

inline const QColor Background{0x10, 0x11, 0x15};
inline const QColor Card{0x1a, 0x1b, 0x21};
inline const QColor Popover{0x20, 0x21, 0x28};
inline const QColor Secondary{0x29, 0x2b, 0x32};
inline const QColor Border{0x33, 0x34, 0x3d};
inline const QColor Foreground{0xf5, 0xf2, 0xe9};
inline const QColor Muted{0xb9, 0xb9, 0xc6};
inline const QColor Primary{0xc5, 0xb0, 0xff};
inline const QColor PrimaryInk{0x24, 0x14, 0x39};
inline const QColor Accent{0xd8, 0xf0, 0x77};
inline const QColor Danger{0xff, 0x87, 0x91};

inline QColor Alpha(QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
}

/// How much larger than its nominal size all text is drawn.
inline constexpr double TextScale = 1.2;

/// Headings use the display face, everything else the body face.
inline QFont Font(int pixel_size, QFont::Weight weight = QFont::Normal, bool display = false) {
    QFont font{display ? QStringLiteral("Segoe UI Variable Display") : QStringLiteral("Segoe UI")};
    font.setPixelSize(static_cast<int>(pixel_size * TextScale + 0.5));
    font.setWeight(weight);
    return font;
}

/// Spaced capitals for small labels ("BY CONSOLE").
inline QFont EyebrowFont(int pixel_size = 11) {
    QFont font = Font(pixel_size, QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
    font.setCapitalization(QFont::AllUppercase);
    return font;
}

/// `image` with every pixel painted `color`, keeping its alpha (a logo in one colour).
inline QImage Tinted(const QImage& image, const QColor& color) {
    QImage tinted = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter{&tinted};
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    return tinted;
}

/// How light an image's visible pixels are on average (0-255), weighted by their opacity.
inline int Lightness(const QImage& image) {
    // Full size: thin outline logos vanish when shrunk.
    const QImage small = image.convertToFormat(QImage::Format_ARGB32);
    double sum = 0;
    double weight = 0;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QRgb pixel = small.pixel(x, y);
            const double alpha = qAlpha(pixel) / 255.0;
            sum += alpha * (0.299 * qRed(pixel) + 0.587 * qGreen(pixel) + 0.114 * qBlue(pixel));
            weight += alpha;
        }
    }
    return weight > 0 ? static_cast<int>(sum / weight) : 255;
}

/// `image` with each pixel's lightness flipped and its hue kept: a dark logo with lettering cut
/// out in white becomes a light one with dark lettering, and coloured parts keep their colour.
inline QImage Inverted(const QImage& image) {
    QImage result = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int alpha = qAlpha(line[x]);
            if (alpha == 0) {
                continue;
            }
            QColor color = QColor::fromRgb(line[x]);
            float hue, saturation, lightness, unused;
            color.getHslF(&hue, &saturation, &lightness, &unused);
            color.setHslF(hue, saturation, 1.0f - lightness);
            line[x] = qRgba(color.red(), color.green(), color.blue(), alpha);
        }
    }
    result.setDevicePixelRatio(image.devicePixelRatio());
    return result;
}

/// `image` without its fully transparent margins.
inline QImage Trimmed(const QImage& image) {
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int left = argb.width(), top = argb.height(), right = -1, bottom = -1;
    for (int y = 0; y < argb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < left || bottom < top) {
        return image;
    }
    return argb.copy(left, top, right - left + 1, bottom - top + 1);
}

/// A console logo in its own colours, fitted in `box`. Logos too dark for the dark background
/// use their white version ("<name>-w") when there is one, else are inverted.
/// With `area` (in square pixels), the logo's margins are trimmed and it is sized to that area
/// within `box`, so wide wordmarks and square badges look about as large as each other.
inline QImage ReadLogo(const QString& file, QSize box, qreal ratio, int area = 0) {
    const auto read = [&](const QString& path) {
        QImageReader reader{path};
        QSize size = reader.size();
        if (!size.isValid() || size.isEmpty()) {
            size = box;
        }
        // Rendered large enough to trim and shrink without blurring.
        const QSize render = area > 0 ? box * ratio * 3 : box * ratio;
        reader.setScaledSize(size.scaled(render, Qt::KeepAspectRatio));
        QImage image = reader.read();
        if (image.isNull()) {
            return image;
        }
        if (area > 0) {
            image = Trimmed(image);
            const double aspect = static_cast<double>(image.width()) / image.height();
            double height = std::sqrt(area / aspect);
            double width = height * aspect;
            const double fit = std::min({1.0, box.width() / width, box.height() / height});
            width *= fit;
            height *= fit;
            image = image.scaled(QSize{std::max(1, static_cast<int>(width * ratio)),
                                       std::max(1, static_cast<int>(height * ratio))},
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        image.setDevicePixelRatio(ratio);
        return image;
    };
    QImage image = read(file);
    if (image.isNull() || Lightness(image) >= 105) {
        return image;
    }
    const QFileInfo info{file};
    const QString white =
        info.dir().filePath(info.completeBaseName() + QStringLiteral("-w.") + info.suffix());
    if (QFileInfo::exists(white)) {
        if (QImage light = read(white); !light.isNull()) {
            return light;
        }
    }
    return Inverted(image);
}

/// A painted button in the site's styles, immune to the application's widget style.
class Button : public QAbstractButton {
public:
    enum class Kind { Primary, Secondary, Ghost, Outline, Icon };

    Button(Kind kind_, const QString& glyph_, const QString& label, QWidget* parent = nullptr)
        : QAbstractButton{parent}, kind{kind_}, glyph{glyph_} {
        setText(kind == Kind::Icon ? QString{} : label);
        setToolTip(kind == Kind::Icon ? label : QString{});
        setAccessibleName(label);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
    }

    void SetLarge(bool large_) {
        large = large_;
        updateGeometry();
    }

    QSize sizeHint() const override {
        const int height = large ? 48 : 38;
        if (kind == Kind::Icon) {
            return {height, height};
        }
        const int text_width = QFontMetrics{TextFont()}.horizontalAdvance(text());
        const int glyph_width = glyph.isEmpty() ? 0 : 24;
        return {text_width + glyph_width + (large ? 44 : 30), height};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        if (!isEnabled()) {
            painter.setOpacity(0.4);
        }
        const bool hot = (underMouse() || hasFocus()) && isEnabled();
        const QRectF bounds = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const double radius = kind == Kind::Icon ? bounds.height() / 2 : 10.0;

        QColor fill = Qt::transparent;
        QColor ink = Foreground;
        QPen pen = Qt::NoPen;
        switch (kind) {
        case Kind::Primary:
            fill = hot ? Primary.lighter(108) : Primary;
            ink = PrimaryInk;
            break;
        case Kind::Secondary:
            fill = hot ? Secondary.lighter(130) : Secondary;
            break;
        case Kind::Outline:
            pen = QPen(hot ? Alpha(Primary, 160) : Border, 1);
            fill = hot ? Alpha(Qt::white, 10) : Qt::transparent;
            break;
        case Kind::Ghost:
        case Kind::Icon:
            fill = hot ? Alpha(Qt::white, 18) : Qt::transparent;
            ink = hot ? Foreground : Muted;
            break;
        }
        if (isDown()) {
            fill = fill.darker(112);
        }
        painter.setPen(pen);
        painter.setBrush(fill);
        painter.drawRoundedRect(bounds, radius, radius);
        if (hasFocus()) {
            painter.setPen(QPen(Alpha(Primary, 200), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds, radius, radius);
        }

        painter.setPen(ink);
        QFont icon_font = ConsoleWidgets::IconFamily().isEmpty()
                              ? font()
                              : QFont{ConsoleWidgets::IconFamily()};
        icon_font.setPixelSize(large ? 17 : 15);
        if (kind == Kind::Icon) {
            painter.setFont(icon_font);
            painter.drawText(bounds, Qt::AlignCenter, glyph);
            return;
        }
        const QFont text_font = TextFont();
        const int text_width = QFontMetrics{text_font}.horizontalAdvance(text());
        const int glyph_width = glyph.isEmpty() ? 0 : 24;
        double x = bounds.center().x() - (text_width + glyph_width) / 2.0;
        if (!glyph.isEmpty()) {
            painter.setFont(icon_font);
            painter.drawText(QRectF(x, bounds.top(), 18, bounds.height()), Qt::AlignCenter, glyph);
            x += glyph_width;
        }
        painter.setFont(text_font);
        painter.drawText(QRectF(x, bounds.top(), text_width + 2, bounds.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    QFont TextFont() const {
        return Font(large ? 15 : 13, kind == Kind::Primary ? QFont::Bold : QFont::DemiBold);
    }

    Kind kind;
    QString glyph;
    bool large{};
};

/// Glyphs of the Windows icon font used across the app.
namespace Icons {
inline QString Play() {
    return ConsoleWidgets::Glyph(0xE768, QStringLiteral("▶"));
}
inline QString Add() {
    return ConsoleWidgets::Glyph(0xE710, QStringLiteral("+"));
}
inline QString Settings() {
    return ConsoleWidgets::Glyph(0xE713, QStringLiteral("⚙"));
}
inline QString Folder() {
    return ConsoleWidgets::Glyph(0xE8B7, QStringLiteral("▤"));
}
inline QString Download() {
    return ConsoleWidgets::Glyph(0xE896, QStringLiteral("↓"));
}
inline QString Close() {
    return ConsoleWidgets::Glyph(0xE711, QStringLiteral("✕"));
}
inline QString Edit() {
    return ConsoleWidgets::Glyph(0xE70F, QStringLiteral("✎"));
}
inline QString Search() {
    return ConsoleWidgets::Glyph(0xE721, QStringLiteral("⌕"));
}
inline QString FullScreen() {
    return ConsoleWidgets::Glyph(0xE740, QStringLiteral("⤢"));
}
inline QString Library() {
    return ConsoleWidgets::Glyph(0xE8F1, QStringLiteral("☰"));
}
inline QString Video() {
    return ConsoleWidgets::Glyph(0xE714, QStringLiteral("▶"));
}
} // namespace Icons

} // namespace Theme
