// SPDX-License-Identifier: GPL-2.0-or-later
#include "boxdrawing.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

// Generated from the Unicode character names (U+2500-U+257F): bits are
// up | right << 2 | down << 4 | left << 6, each 0 none, 1 light, 2 heavy, 3 double.
// 0xffff: not a plain line (dashes, arcs, diagonals; drawn separately).
constexpr quint16 kLines[128] = {
    0x0044, // U+2500 light horizontal
    0x0088, // U+2501 heavy horizontal
    0x0011, // U+2502 light vertical
    0x0022, // U+2503 heavy vertical
    0xffff, // U+2504 light triple dash horizontal
    0xffff, // U+2505 heavy triple dash horizontal
    0xffff, // U+2506 light triple dash vertical
    0xffff, // U+2507 heavy triple dash vertical
    0xffff, // U+2508 light quadruple dash horizontal
    0xffff, // U+2509 heavy quadruple dash horizontal
    0xffff, // U+250A light quadruple dash vertical
    0xffff, // U+250B heavy quadruple dash vertical
    0x0014, // U+250C light down and right
    0x0018, // U+250D down light and right heavy
    0x0024, // U+250E down heavy and right light
    0x0028, // U+250F heavy down and right
    0x0050, // U+2510 light down and left
    0x0090, // U+2511 down light and left heavy
    0x0060, // U+2512 down heavy and left light
    0x00a0, // U+2513 heavy down and left
    0x0005, // U+2514 light up and right
    0x0009, // U+2515 up light and right heavy
    0x0006, // U+2516 up heavy and right light
    0x000a, // U+2517 heavy up and right
    0x0041, // U+2518 light up and left
    0x0081, // U+2519 up light and left heavy
    0x0042, // U+251A up heavy and left light
    0x0082, // U+251B heavy up and left
    0x0015, // U+251C light vertical and right
    0x0019, // U+251D vertical light and right heavy
    0x0016, // U+251E up heavy and right down light
    0x0025, // U+251F down heavy and right up light
    0x0026, // U+2520 vertical heavy and right light
    0x001a, // U+2521 down light and right up heavy
    0x0029, // U+2522 up light and right down heavy
    0x002a, // U+2523 heavy vertical and right
    0x0051, // U+2524 light vertical and left
    0x0091, // U+2525 vertical light and left heavy
    0x0052, // U+2526 up heavy and left down light
    0x0061, // U+2527 down heavy and left up light
    0x0062, // U+2528 vertical heavy and left light
    0x0092, // U+2529 down light and left up heavy
    0x00a1, // U+252A up light and left down heavy
    0x00a2, // U+252B heavy vertical and left
    0x0054, // U+252C light down and horizontal
    0x0094, // U+252D left heavy and right down light
    0x0058, // U+252E right heavy and left down light
    0x0098, // U+252F down light and horizontal heavy
    0x0064, // U+2530 down heavy and horizontal light
    0x00a4, // U+2531 right light and left down heavy
    0x0068, // U+2532 left light and right down heavy
    0x00a8, // U+2533 heavy down and horizontal
    0x0045, // U+2534 light up and horizontal
    0x0085, // U+2535 left heavy and right up light
    0x0049, // U+2536 right heavy and left up light
    0x0089, // U+2537 up light and horizontal heavy
    0x0046, // U+2538 up heavy and horizontal light
    0x0086, // U+2539 right light and left up heavy
    0x004a, // U+253A left light and right up heavy
    0x008a, // U+253B heavy up and horizontal
    0x0055, // U+253C light vertical and horizontal
    0x0095, // U+253D left heavy and right vertical light
    0x0059, // U+253E right heavy and left vertical light
    0x0099, // U+253F vertical light and horizontal heavy
    0x0056, // U+2540 up heavy and down horizontal light
    0x0065, // U+2541 down heavy and up horizontal light
    0x0066, // U+2542 vertical heavy and horizontal light
    0x0096, // U+2543 left up heavy and right down light
    0x005a, // U+2544 right up heavy and left down light
    0x00a5, // U+2545 left down heavy and right up light
    0x0069, // U+2546 right down heavy and left up light
    0x009a, // U+2547 down light and up horizontal heavy
    0x00a9, // U+2548 up light and down horizontal heavy
    0x00a6, // U+2549 right light and left vertical heavy
    0x006a, // U+254A left light and right vertical heavy
    0x00aa, // U+254B heavy vertical and horizontal
    0xffff, // U+254C light double dash horizontal
    0xffff, // U+254D heavy double dash horizontal
    0xffff, // U+254E light double dash vertical
    0xffff, // U+254F heavy double dash vertical
    0x00cc, // U+2550 double horizontal
    0x0033, // U+2551 double vertical
    0x001c, // U+2552 down single and right double
    0x0034, // U+2553 down double and right single
    0x003c, // U+2554 double down and right
    0x00d0, // U+2555 down single and left double
    0x0070, // U+2556 down double and left single
    0x00f0, // U+2557 double down and left
    0x000d, // U+2558 up single and right double
    0x0007, // U+2559 up double and right single
    0x000f, // U+255A double up and right
    0x00c1, // U+255B up single and left double
    0x0043, // U+255C up double and left single
    0x00c3, // U+255D double up and left
    0x001d, // U+255E vertical single and right double
    0x0037, // U+255F vertical double and right single
    0x003f, // U+2560 double vertical and right
    0x00d1, // U+2561 vertical single and left double
    0x0073, // U+2562 vertical double and left single
    0x00f3, // U+2563 double vertical and left
    0x00dc, // U+2564 down single and horizontal double
    0x0074, // U+2565 down double and horizontal single
    0x00fc, // U+2566 double down and horizontal
    0x00cd, // U+2567 up single and horizontal double
    0x0047, // U+2568 up double and horizontal single
    0x00cf, // U+2569 double up and horizontal
    0x00dd, // U+256A vertical single and horizontal double
    0x0077, // U+256B vertical double and horizontal single
    0x00ff, // U+256C double vertical and horizontal
    0xffff, // U+256D light arc down and right
    0xffff, // U+256E light arc down and left
    0xffff, // U+256F light arc up and left
    0xffff, // U+2570 light arc up and right
    0xffff, // U+2571 light diagonal upper right to lower left
    0xffff, // U+2572 light diagonal upper left to lower right
    0xffff, // U+2573 light diagonal cross
    0x0040, // U+2574 light left
    0x0001, // U+2575 light up
    0x0004, // U+2576 light right
    0x0010, // U+2577 light down
    0x0080, // U+2578 heavy left
    0x0002, // U+2579 heavy up
    0x0008, // U+257A heavy right
    0x0020, // U+257B heavy down
    0x0048, // U+257C light left and heavy right
    0x0021, // U+257D light up and heavy down
    0x0084, // U+257E heavy left and light right
    0x0012, // U+257F heavy up and light down
};

enum Dir { Up = 0, Right = 1, Down = 2, Left = 3 };

int weight(quint16 v, Dir d)
{
    return (v >> (d * 2)) & 3;
}

struct Geometry {
    QRectF r;
    qreal light, heavy, gap;
    qreal cx, cy;
    // Snap a stroke centre so that a stroke of width w covers whole pixels.
    qreal snap(qreal c, qreal w) const
    {
        const qreal start = std::round(c - w / 2);
        return start + w / 2;
    }
};

void hline(QPainter &p, const QColor &c, qreal x0, qreal x1, qreal y, qreal w)
{
    if (x1 < x0)
        std::swap(x0, x1);
    p.fillRect(QRectF(x0, std::round(y - w / 2), x1 - x0, w), c);
}

void vline(QPainter &p, const QColor &c, qreal y0, qreal y1, qreal x, qreal w)
{
    if (y1 < y0)
        std::swap(y0, y1);
    p.fillRect(QRectF(std::round(x - w / 2), y0, w, y1 - y0), c);
}

void drawLines(QPainter &p, const Geometry &g, quint16 v, const QColor &col)
{
    const QRectF &r = g.r;
    const int up = weight(v, Up), right = weight(v, Right), down = weight(v, Down), left = weight(v, Left);
    auto width = [&](int w) { return w == 2 ? g.heavy : g.light; };
    // How far a single or heavy stroke runs past the centre so that it meets
    // the perpendicular strokes cleanly.
    const qreal hExt = std::max(up == 2 || down == 2 ? g.heavy : g.light, (up == 3 || down == 3) ? g.gap + g.light : 0.0) / 2;
    const qreal vExt = std::max(left == 2 || right == 2 ? g.heavy : g.light, (left == 3 || right == 3) ? g.gap + g.light : 0.0) / 2;
    const qreal cx = g.cx, cy = g.cy, d = g.gap / 2 + g.light / 2;

    if (right == 1 || right == 2)
        hline(p, col, cx - ((up || down) ? hExt : 0), r.right(), cy, width(right));
    if (left == 1 || left == 2)
        hline(p, col, r.left(), cx + ((up || down) ? hExt : 0), cy, width(left));
    if (down == 1 || down == 2)
        vline(p, col, cy - ((left || right) ? vExt : 0), r.bottom(), cx, width(down));
    if (up == 1 || up == 2)
        vline(p, col, r.top(), cy + ((left || right) ? vExt : 0), cx, width(up));

    const qreal w = g.light;
    if (right == 3) {
        hline(p, col, up == 3 ? cx + d : cx - d, r.right(), cy - d, w);
        hline(p, col, down == 3 ? cx + d : cx - d, r.right(), cy + d, w);
    }
    if (left == 3) {
        hline(p, col, r.left(), up == 3 ? cx - d : cx + d, cy - d, w);
        hline(p, col, r.left(), down == 3 ? cx - d : cx + d, cy + d, w);
    }
    if (down == 3) {
        vline(p, col, left == 3 ? cy + d : cy - d, r.bottom(), cx - d, w);
        vline(p, col, right == 3 ? cy + d : cy - d, r.bottom(), cx + d, w);
    }
    if (up == 3) {
        vline(p, col, r.top(), left == 3 ? cy - d : cy + d, cx - d, w);
        vline(p, col, r.top(), right == 3 ? cy - d : cy + d, cx + d, w);
    }
}

void dashes(QPainter &p, const Geometry &g, bool horizontal, int count, qreal w, const QColor &col)
{
    const QRectF &r = g.r;
    const qreal len = horizontal ? r.width() : r.height();
    const qreal seg = len / count;
    const qreal gap = std::max<qreal>(1, std::round(seg * 0.3));
    for (int i = 0; i < count; ++i) {
        const qreal a = std::round(i * seg + gap / 2), b = std::round((i + 1) * seg - gap / 2);
        if (horizontal)
            hline(p, col, r.left() + a, r.left() + b, g.cy, w);
        else
            vline(p, col, r.top() + a, r.top() + b, g.cx, w);
    }
}

} // namespace

bool BoxDrawing::handles(char32_t cp)
{
    return (cp >= 0x2500 && cp <= 0x259f) || (cp >= 0xe0b0 && cp <= 0xe0b3);
}

bool BoxDrawing::draw(QPainter &p, char32_t cp, const QRectF &rect, const QColor &col)
{
    if (!handles(cp))
        return false;
    Geometry g;
    g.r = rect;
    g.light = std::max<qreal>(1, std::round(rect.height() / 16));
    g.heavy = g.light * 2;
    g.gap = g.light;
    g.cx = g.snap(rect.left() + rect.width() / 2, g.light);
    g.cy = g.snap(rect.top() + rect.height() / 2, g.light);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRectF &r = rect;

    if (cp < 0x2580) {
        const quint16 v = kLines[cp - 0x2500];
        if (v != 0xffff) {
            drawLines(p, g, v, col);
            p.restore();
            return true;
        }
        switch (cp) {
        case 0x2504: dashes(p, g, true, 3, g.light, col); break;
        case 0x2505: dashes(p, g, true, 3, g.heavy, col); break;
        case 0x2506: dashes(p, g, false, 3, g.light, col); break;
        case 0x2507: dashes(p, g, false, 3, g.heavy, col); break;
        case 0x2508: dashes(p, g, true, 4, g.light, col); break;
        case 0x2509: dashes(p, g, true, 4, g.heavy, col); break;
        case 0x250a: dashes(p, g, false, 4, g.light, col); break;
        case 0x250b: dashes(p, g, false, 4, g.heavy, col); break;
        case 0x254c: dashes(p, g, true, 2, g.light, col); break;
        case 0x254d: dashes(p, g, true, 2, g.heavy, col); break;
        case 0x254e: dashes(p, g, false, 2, g.light, col); break;
        case 0x254f: dashes(p, g, false, 2, g.heavy, col); break;
        case 0x256d: case 0x256e: case 0x256f: case 0x2570: {
            // Rounded corners: a quarter circle joining two half lines.
            p.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(col, g.light);
            pen.setCapStyle(Qt::FlatCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            const qreal rad = std::min(r.width(), r.height()) / 2;
            QPainterPath path;
            const qreal cx = g.cx, cy = g.cy;
            if (cp == 0x256d) { // down and right
                path.moveTo(cx, r.bottom());
                path.lineTo(cx, cy + rad);
                path.quadTo(cx, cy, cx + rad, cy);
                path.lineTo(r.right(), cy);
            } else if (cp == 0x256e) { // down and left
                path.moveTo(cx, r.bottom());
                path.lineTo(cx, cy + rad);
                path.quadTo(cx, cy, cx - rad, cy);
                path.lineTo(r.left(), cy);
            } else if (cp == 0x256f) { // up and left
                path.moveTo(cx, r.top());
                path.lineTo(cx, cy - rad);
                path.quadTo(cx, cy, cx - rad, cy);
                path.lineTo(r.left(), cy);
            } else { // up and right
                path.moveTo(cx, r.top());
                path.lineTo(cx, cy - rad);
                path.quadTo(cx, cy, cx + rad, cy);
                path.lineTo(r.right(), cy);
            }
            p.drawPath(path);
            break;
        }
        case 0x2571: case 0x2572: case 0x2573: {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(col, g.light));
            if (cp != 0x2572)
                p.drawLine(QPointF(r.right(), r.top()), QPointF(r.left(), r.bottom()));
            if (cp != 0x2571)
                p.drawLine(r.topLeft(), r.bottomRight());
            break;
        }
        default:
            p.restore();
            return false;
        }
        p.restore();
        return true;
    }

    auto fill = [&](qreal x0, qreal y0, qreal x1, qreal y1) {
        // Fractions of the cell, rounded to pixels so neighbours tile.
        const qreal l = std::round(r.left() + x0 * r.width()), t = std::round(r.top() + y0 * r.height());
        const qreal rr = std::round(r.left() + x1 * r.width()), b = std::round(r.top() + y1 * r.height());
        p.fillRect(QRectF(l, t, rr - l, b - t), col);
    };

    if (cp >= 0xe0b0) {
        // Powerline separators.
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        if (cp == 0xe0b0 || cp == 0xe0b2) {
            if (cp == 0xe0b0) {
                path.moveTo(r.topLeft());
                path.lineTo(r.right(), r.center().y());
                path.lineTo(r.bottomLeft());
            } else {
                path.moveTo(r.topRight());
                path.lineTo(r.left(), r.center().y());
                path.lineTo(r.bottomRight());
            }
            path.closeSubpath();
            p.fillPath(path, col);
        } else {
            p.setPen(QPen(col, g.light));
            if (cp == 0xe0b1) {
                path.moveTo(r.topLeft());
                path.lineTo(r.right(), r.center().y());
                path.lineTo(r.bottomLeft());
            } else {
                path.moveTo(r.topRight());
                path.lineTo(r.left(), r.center().y());
                path.lineTo(r.bottomRight());
            }
            p.drawPath(path);
        }
        p.restore();
        return true;
    }

    switch (cp) {
    case 0x2580: fill(0, 0, 1, 0.5); break;
    case 0x2581: case 0x2582: case 0x2583: case 0x2584: case 0x2585: case 0x2586: case 0x2587:
        fill(0, 1 - (cp - 0x2580) / 8.0, 1, 1);
        break;
    case 0x2588: fill(0, 0, 1, 1); break;
    case 0x2589: case 0x258a: case 0x258b: case 0x258c: case 0x258d: case 0x258e: case 0x258f:
        fill(0, 0, (0x2590 - cp) / 8.0, 1);
        break;
    case 0x2590: fill(0.5, 0, 1, 1); break;
    case 0x2591: case 0x2592: case 0x2593: {
        QColor c = col;
        c.setAlphaF(c.alphaF() * (cp - 0x2590) / 4.0);
        p.fillRect(r, c);
        break;
    }
    case 0x2594: fill(0, 0, 1, 1 / 8.0); break;
    case 0x2595: fill(7 / 8.0, 0, 1, 1); break;
    default: {
        // Quadrants U+2596-U+259F: bits are upper left, upper right, lower left, lower right.
        static const quint8 quads[] = {0b0010, 0b0001, 0b1000, 0b1011, 0b1001, 0b1110, 0b1101, 0b0100, 0b0110, 0b0111};
        const quint8 q = quads[cp - 0x2596];
        if (q & 0b1000) fill(0, 0, 0.5, 0.5);
        if (q & 0b0100) fill(0.5, 0, 1, 0.5);
        if (q & 0b0010) fill(0, 0.5, 0.5, 1);
        if (q & 0b0001) fill(0.5, 0.5, 1, 1);
        break;
    }
    }
    p.restore();
    return true;
}
