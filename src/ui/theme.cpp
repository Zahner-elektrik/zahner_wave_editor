// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QStyleFactory>
#include <algorithm>

namespace zwe::theme {

namespace {

constexpr auto ZahnerRed        = "#a02832";
constexpr auto ZahnerBlue       = "#145099";
constexpr auto ZahnerGreenBlue  = "#3b5c69";
constexpr auto ZahnerAnthracite = "#1e2823";

void setCommonColors(QPalette& palette, Accent accent) {
    const QColor color = accentColor(accent);
    palette.setColor(QPalette::Highlight, color);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, color);
    palette.setColor(QPalette::LinkVisited, QColor(ZahnerGreenBlue));
}

}  // namespace

QColor accentColor(Accent accent) {
    return QColor(accent == Accent::Blue ? ZahnerBlue : ZahnerRed);
}

QString accentName(Accent accent) {
    return accent == Accent::Blue ? QStringLiteral("blue") : QStringLiteral("red");
}

Accent accentFromName(const QString& name, Accent fallback) {
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("red")) {
        return Accent::Red;
    }
    if (normalized == QStringLiteral("blue")) {
        return Accent::Blue;
    }
    return fallback;
}

QColor curveColor(size_t index, const QPalette& palette) {
    // The plot judges its background by Base rather than by Window: the curves
    // are drawn on Base, and that is what they have to stay legible against.
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    QColor color    = palette.color(QPalette::Link);
    // 47 degrees per layer: coprime with 360, so the hues of the first eight
    // layers stay far apart instead of repeating early.
    const int hue =
        (color.hsvHue() < 0 ? 205 : color.hsvHue()) + static_cast<int>(index % 360) * 47;
    color.setHsv(
        hue % 360,
        std::max(color.hsvSaturation(), dark ? 135 : 105),
        std::max(color.value(), dark ? 205 : 145)
    );
    return color;
}

QPalette lightPalette(Accent accent) {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#f3f3f3"));
    palette.setColor(QPalette::WindowText, QColor("#202020"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#e8e8e8"));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, QColor("#202020"));
    palette.setColor(QPalette::Text, QColor("#202020"));
    palette.setColor(QPalette::Button, QColor("#e5e5e5"));
    palette.setColor(QPalette::ButtonText, QColor("#202020"));
    palette.setColor(QPalette::BrightText, accentColor(accent));
    palette.setColor(QPalette::PlaceholderText, QColor("#777777"));
    palette.setColor(QPalette::Mid, QColor("#a0a0a0"));
    palette.setColor(QPalette::Dark, QColor("#707070"));
    palette.setColor(QPalette::Light, Qt::white);
    setCommonColors(palette, accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#858585"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#858585"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#858585"));
    return palette;
}

QPalette darkPalette(Accent accent) {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(ZahnerAnthracite));
    palette.setColor(QPalette::WindowText, QColor("#f2f5f3"));
    palette.setColor(QPalette::Base, QColor("#101613"));
    palette.setColor(QPalette::AlternateBase, QColor("#293832"));
    palette.setColor(QPalette::ToolTipBase, QColor(ZahnerGreenBlue));
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, QColor("#f2f5f3"));
    palette.setColor(QPalette::Button, QColor("#314039"));
    palette.setColor(QPalette::ButtonText, QColor("#f2f5f3"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor("#b2bdb8"));
    palette.setColor(QPalette::Mid, QColor(ZahnerGreenBlue));
    palette.setColor(QPalette::Dark, QColor("#090d0b"));
    palette.setColor(QPalette::Light, QColor("#698078"));
    setCommonColors(palette, accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#93a099"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#93a099"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#93a099"));
    return palette;
}

bool paletteIsDark(const QPalette& palette) {
    return palette.color(QPalette::Window).lightness() <
           palette.color(QPalette::WindowText).lightness();
}

QIcon applicationIcon(bool dark) {
    return QIcon(
        dark ? QStringLiteral(":/icons/waveeditor-dark.svg")
             : QStringLiteral(":/icons/waveeditor.svg")
    );
}

QIcon accentIcon(const QIcon& icon, const QColor& accent) {
    if (icon.isNull()) {
        return icon;
    }
    QImage image = icon.pixmap(QSize(64, 64)).toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() == 0 || pixel.hsvSaturation() < 110) {
                continue;
            }
            const int hue = pixel.hsvHue();
            if (hue <= 20 || hue >= 340 || (hue >= 195 && hue <= 225)) {
                pixel.setRgb(accent.red(), accent.green(), accent.blue(), pixel.alpha());
                image.setPixelColor(x, y, pixel);
            }
        }
    }
    return QIcon(QPixmap::fromImage(image));
}

QIcon tintedIcon(const QIcon& icon, const QColor& color) {
    if (icon.isNull()) {
        return icon;
    }
    QImage image = icon.pixmap(QSize(64, 64)).toImage().convertToFormat(QImage::Format_ARGB32);
    // The mask is used as it is, so the fill has to cover device pixels rather
    // than the logical size a high-DPI pixmap reports.
    image.setDevicePixelRatio(1.0);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    painter.end();
    return QIcon(QPixmap::fromImage(image));
}

void apply(bool dark, Accent accent) {
    if (! QApplication::style() ||
        QApplication::style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) !=
            0) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }
    QApplication::setPalette(dark ? darkPalette(accent) : lightPalette(accent));
}

}  // namespace zwe::theme
