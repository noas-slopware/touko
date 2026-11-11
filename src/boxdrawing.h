// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QRectF>

class QPainter;

// Box drawing, block elements and powerline separators drawn as shapes
// rather than glyphs, so they join up across cells whatever the font's
// line height is.
namespace BoxDrawing {
bool handles(char32_t cp);
bool draw(QPainter &p, char32_t cp, const QRectF &cellRect, const QColor &color);
}
