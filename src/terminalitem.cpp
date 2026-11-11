// SPDX-License-Identifier: GPL-2.0-or-later
#include "terminalitem.h"

#include "boxdrawing.h"
#include "keymap.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QScreen>
#include <QStyleHints>

#include <cmath>

namespace {

QColor toQColor(GhosttyColorRgb c)
{
    return QColor(c.r, c.g, c.b);
}

// Root node: a background rectangle, then one image node per row.
class RootNode : public QSGNode
{
public:
    QSGRectangleNode *background = nullptr;
    std::vector<QSGImageNode *> rows;
};

constexpr int kLongPressMs = 450;
constexpr int kBlinkMs = 600;
constexpr int kBlinkIdleMs = 15000;

} // namespace

TerminalItem::TerminalItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setFlag(ItemAcceptsInputMethod);
    setFlag(ItemIsFocusScope, false);
    setActiveFocusOnTab(true);
    setAcceptTouchEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    m_fontFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    ghostty_render_state_new(nullptr, &m_renderState);
    ghostty_render_state_row_iterator_new(nullptr, &m_rowIter);
    ghostty_render_state_row_cells_new(nullptr, &m_cells);
    m_lastActivity.start();
    // An item constructed with a parent already in a window never sees its
    // own ItemSceneChange (the base class sends it before this exists).
    updateMetrics();
}

TerminalItem::~TerminalItem()
{
    ghostty_render_state_row_cells_free(m_cells);
    ghostty_render_state_row_iterator_free(m_rowIter);
    ghostty_render_state_free(m_renderState);
}

void TerminalItem::setSession(TerminalSession *s)
{
    if (m_session == s)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = s;
    // A fresh render state: the old one described another terminal.
    ghostty_render_state_free(m_renderState);
    m_renderState = nullptr;
    ghostty_render_state_new(nullptr, &m_renderState);
    if (m_session) {
        connect(m_session, &TerminalSession::contentChanged, this, [this] {
            m_lastActivity.restart();
            scheduleRender();
        });
        connect(m_session, &TerminalSession::selectionChanged, this, &TerminalItem::selectionGeometryChanged);
        connect(m_session, &QObject::destroyed, this, [this] { setSession(nullptr); });
        applyTheme();
        relayout();
    }
    scheduleRender(true);
    Q_EMIT sessionChanged();
}

void TerminalItem::setFontFamily(const QString &f)
{
    if (f == m_fontFamily || f.isEmpty())
        return;
    m_fontFamily = f;
    updateMetrics();
    Q_EMIT fontChanged();
}

void TerminalItem::setFontSize(qreal pt)
{
    pt = qBound<qreal>(4, pt, 48);
    if (qFuzzyCompare(pt, m_fontSize))
        return;
    m_fontSize = pt;
    updateMetrics();
    Q_EMIT fontChanged();
}

void TerminalItem::setLineHeight(qreal f)
{
    f = qBound<qreal>(0.8, f, 2.0);
    if (qFuzzyCompare(f, m_lineHeight))
        return;
    m_lineHeight = f;
    updateMetrics();
    Q_EMIT fontChanged();
}

void TerminalItem::setLightTheme(const QString &n)
{
    if (n == m_lightThemeName)
        return;
    m_lightThemeName = n;
    applyTheme();
}

void TerminalItem::setDarkTheme(const QString &n)
{
    if (n == m_darkThemeName)
        return;
    m_darkThemeName = n;
    applyTheme();
}

void TerminalItem::setDark(bool d)
{
    if (d == m_dark)
        return;
    m_dark = d;
    applyTheme();
}

void TerminalItem::applyTheme()
{
    const QString name = m_dark ? m_darkThemeName : m_lightThemeName;
    m_theme = Theme::load(name).value_or(m_dark ? Theme::builtinDark() : Theme::builtinLight());
    if (m_session)
        m_session->applyTheme(m_theme, m_dark);
    scheduleRender(true);
    Q_EMIT themeChanged();
}

void TerminalItem::setCtrlLatched(bool v)
{
    if (v == m_ctrlLatched)
        return;
    m_ctrlLatched = v;
    Q_EMIT latchChanged();
}

void TerminalItem::setAltLatched(bool v)
{
    if (v == m_altLatched)
        return;
    m_altLatched = v;
    Q_EMIT latchChanged();
}

// ---- metrics and layout -----------------------------------------------------

void TerminalItem::updateMetrics()
{
    m_dpr = window() ? window()->effectiveDevicePixelRatio() : qApp->devicePixelRatio();
    const QScreen *screen = window() && window()->screen() ? window()->screen() : QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInchY() : 96;

    // A font is usable when its ASCII really is one width. Qt's fixedPitch()
    // says no to dual-width fonts like Noto Sans Mono CJK, which are fine,
    // and a "system fixed font" is not always fixed (DejaVu Sans, with no
    // platform config), so measure instead of asking.
    auto monospaced = [](const QFont &f) {
        const QFontMetricsF m(f);
        const qreal w = m.horizontalAdvance(QLatin1Char('M'));
        for (QLatin1Char c : {QLatin1Char('i'), QLatin1Char('W'), QLatin1Char('.'), QLatin1Char('l')}) {
            if (std::abs(m.horizontalAdvance(c) - w) > 0.01 * w)
                return false;
        }
        return true;
    };
    QFont base(m_fontFamily);
    if (!monospaced(base)) {
        base = QFont(QStringLiteral("monospace"));
        base.setStyleHint(QFont::TypeWriter);
    }
    base.setStyleHint(QFont::Monospace, QFont::PreferDefault);
    base.setFixedPitch(true);
    base.setKerning(false);
    base.setHintingPreference(QFont::PreferFullHinting);
    base.setPixelSize(std::max(4, int(std::round(m_fontSize * dpi / 72.0 * m_dpr))));

    const QFontMetricsF fm(base);
    m_cellW = std::max(1, int(std::round(fm.horizontalAdvance(QLatin1Char('M')))));
    m_cellH = std::max(1, int(std::ceil(fm.height() * m_lineHeight)));
    m_fonts.baseline = int(std::round((m_cellH - fm.height()) / 2 + fm.ascent()));

    // Every font variant advances exactly one cell per ASCII character, so a
    // run of them can be drawn as one string and still land on the grid.
    auto fit = [&](QFont f) {
        const qreal adv = QFontMetricsF(f).horizontalAdvance(QLatin1Char('M'));
        f.setLetterSpacing(QFont::AbsoluteSpacing, m_cellW - adv);
        return f;
    };
    QFont bold = base;
    bold.setBold(true);
    QFont italic = base;
    italic.setItalic(true);
    QFont boldItalic = bold;
    boldItalic.setItalic(true);
    m_fonts.regular = fit(base);
    m_fonts.bold = fit(bold);
    m_fonts.italic = fit(italic);
    m_fonts.boldItalic = fit(boldItalic);
    const QFont plain[4] = {base, bold, italic, boldItalic};
    for (int i = 0; i < 4; ++i) {
        m_fonts.plain[i] = plain[i];
        m_fonts.plainMetrics[i].emplace(plain[i]);
    }
    m_fonts.strikeY = m_fonts.baseline - int(fm.xHeight() / 2);

    relayout();
    Q_EMIT metricsChanged();
}

void TerminalItem::relayout()
{
    const qreal w = width() * m_dpr;
    const qreal h = height() * m_dpr;
    if (w <= 0 || h <= 0)
        return;
    const int minPad = int(std::round(4 * m_dpr));
    const int cols = std::max(2, int((w - 2 * minPad) / m_cellW));
    const int rows = std::max(1, int(h / m_cellH));
    m_padX = std::max(0, int((w - cols * m_cellW) / 2));
    m_padY = 0;
    if (cols != m_cols || rows != m_rows) {
        m_cols = cols;
        m_rows = rows;
    }
    if (m_session)
        m_session->resize(m_cols, m_rows, m_cellW, m_cellH);
    m_geometryDirty = true;
    scheduleRender(true);
    Q_EMIT selectionGeometryChanged();
}

void TerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        relayout();
}

void TerminalItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange && value.window) {
        connect(value.window, &QQuickWindow::screenChanged, this, &TerminalItem::updateMetrics, Qt::UniqueConnection);
        updateMetrics();
    } else if (change == ItemDevicePixelRatioHasChanged) {
        updateMetrics();
    } else if (change == ItemVisibleHasChanged && value.boolValue) {
        scheduleRender(true);
    }
    QQuickItem::itemChange(change, value);
}

void TerminalItem::releaseResources()
{
    m_rowImages.clear();
    m_rowUpload.clear();
    m_fullRender = true;
}

void TerminalItem::scheduleRender(bool full)
{
    if (full)
        m_fullRender = true;
    if (!isVisible())
        return;
    polish();
    update();
}

// ---- painting ---------------------------------------------------------------

void TerminalItem::updatePolish()
{
    if (!m_session || m_cols <= 0 || m_rows <= 0)
        return;
    if (ghostty_render_state_update(m_renderState, m_session->terminal()) != GHOSTTY_SUCCESS)
        return;

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors);
    m_bgForNode = toQColor(colors.background);

    GhosttyRenderStateDirty dirty = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_DIRTY, &dirty);
    uint16_t rsRows = 0, rsCols = 0;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROWS, &rsRows);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_COLS, &rsCols);

    const int imgW = rsCols * m_cellW;
    bool full = m_fullRender || dirty == GHOSTTY_RENDER_STATE_DIRTY_FULL || int(m_rowImages.size()) != rsRows
                || (!m_rowImages.empty() && m_rowImages[0].width() != imgW);
    if (int(m_rowImages.size()) != rsRows) {
        m_rowImages.assign(rsRows, QImage());
        m_rowUpload.assign(rsRows, true);
    }

    GhosttyRenderStateCursor cursor = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cursor);
    const int cursorRow = (cursor.visible && cursor.viewport_has_value) ? int(cursor.viewport_y) : -1;

    // Cursor blink: only when the application asked for it, the view has
    // focus, and something happened recently. An idle terminal does no work.
    const bool wantBlink = cursor.blinking && m_focused && m_lastActivity.elapsed() < kBlinkIdleMs;
    if (wantBlink && !m_blinkTimer.isActive())
        m_blinkTimer.start(kBlinkMs, this);
    else if (!wantBlink && m_blinkTimer.isActive()) {
        m_blinkTimer.stop();
        m_cursorBlinkOn = true;
    }

    if (ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &m_rowIter) != GHOSTTY_SUCCESS)
        return;
    int y = 0;
    while (ghostty_render_state_row_iterator_next(m_rowIter)) {
        if (y >= rsRows)
            break;
        bool rowDirty = full;
        if (!rowDirty)
            ghostty_render_state_row_get(m_rowIter, GHOSTTY_RENDER_STATE_ROW_DATA_DIRTY, &rowDirty);
        if (y == cursorRow || y == m_lastCursorRow)
            rowDirty = true;
        if (rowDirty) {
            renderRow(y, m_rowIter, colors);
            m_rowUpload[y] = true;
        }
        ++y;
    }
    m_lastCursorRow = cursorRow;
    ghostty_render_state_clean(m_renderState);
    m_fullRender = false;
}

void TerminalItem::renderRow(int y, GhosttyRenderStateRowIterator rows, const GhosttyRenderStateColors &colors)
{
    uint16_t cols = 0;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_COLS, &cols);
    QImage &img = m_rowImages[y];
    if (img.width() != cols * m_cellW || img.height() != m_cellH)
        img = QImage(cols * m_cellW, m_cellH, QImage::Format_ARGB32_Premultiplied);

    const QColor defBg = toQColor(colors.background);
    const QColor defFg = toQColor(colors.foreground);
    img.fill(defBg);

    if (ghostty_render_state_row_get(rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &m_cells) != GHOSTTY_SUCCESS)
        return;

    GhosttyRenderStateRowSelection rowSel = GHOSTTY_INIT_SIZED(GhosttyRenderStateRowSelection);
    const bool hasSel = ghostty_render_state_row_get(rows, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION, &rowSel) == GHOSTTY_SUCCESS;
    const QColor selBg = m_theme.selectionBackground.value_or(defFg);
    const QColor selFg = m_theme.selectionForeground.value_or(defBg);

    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    struct Cell {
        QString text;
        QColor fg;
        bool bold = false, italic = false, wide = false, box = false;
    };
    std::vector<Cell> cells(cols);

    // Pass 1: backgrounds and per-cell text attributes.
    struct Deco {
        int x, span;
        int underline;
        bool strike, overline;
        QColor color;
    };
    std::vector<Deco> decos;
    uint32_t cps[32];
    int x = 0;
    while (x < cols && ghostty_render_state_row_cells_next(m_cells)) {
        GhosttyCell raw = 0;
        ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
        GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
        ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
        if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) {
            ++x;
            continue;
        }
        const int span = (wide == GHOSTTY_CELL_WIDE_WIDE) ? 2 : 1;

        GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
        bool styled = false;
        ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_HAS_STYLING, &styled);
        if (styled)
            ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

        GhosttyColorRgb c{};
        QColor bg = ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &c) == GHOSTTY_SUCCESS
                        ? toQColor(c)
                        : QColor();
        QColor fg = ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &c) == GHOSTTY_SUCCESS
                        ? toQColor(c)
                        : defFg;
        if (style.inverse) {
            const QColor b = bg.isValid() ? bg : defBg;
            bg = fg;
            fg = b;
        }
        const bool selected = hasSel && x >= rowSel.start_x && x <= rowSel.end_x;
        if (selected) {
            bg = selBg;
            fg = selFg;
        }
        if (bg.isValid() && bg != defBg)
            p.fillRect(QRect(x * m_cellW, 0, span * m_cellW, m_cellH), bg);
        if (style.faint)
            fg.setAlphaF(0.55f);

        uint32_t len = 0;
        ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &len);
        if (len > 0 && !style.invisible) {
            uint32_t *buf = cps;
            std::vector<uint32_t> big;
            if (len > 32) {
                big.resize(len);
                buf = big.data();
            }
            ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, buf);
            Cell &cell = cells[x];
            cell.text = QString::fromUcs4(reinterpret_cast<const char32_t *>(buf), len);
            cell.fg = fg;
            cell.bold = style.bold;
            cell.italic = style.italic;
            cell.wide = span == 2;
            cell.box = len == 1 && BoxDrawing::handles(buf[0]);
        }
        if (style.underline != GHOSTTY_SGR_UNDERLINE_NONE || style.strikethrough || style.overline) {
            QColor uc = fg;
            if (style.underline_color.tag == GHOSTTY_STYLE_COLOR_RGB)
                uc = toQColor(style.underline_color.value.rgb);
            else if (style.underline_color.tag == GHOSTTY_STYLE_COLOR_PALETTE)
                uc = toQColor(colors.palette[style.underline_color.value.palette]);
            decos.push_back({x, span, style.underline, style.strikethrough, style.overline, uc});
        } else {
            bool link = false;
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_HYPERLINK, &link);
            if (link) {
                QColor lc = fg;
                lc.setAlphaF(0.6f);
                decos.push_back({x, span, GHOSTTY_SGR_UNDERLINE_DOTTED, false, false, lc});
            }
        }
        // The iterator walks every cell; a wide character's spacer comes next.
        ++x;
    }

    // Pass 2: text. Plain ASCII in the same style is drawn as one run (the
    // fonts are spaced to the cell width); everything else cell by cell,
    // squeezed into its cells if a fallback font is wider.
    auto drawCell = [&](const Cell &cell, int cx, const QColor &color) {
        const QRect r(cx * m_cellW, 0, (cell.wide ? 2 : 1) * m_cellW, m_cellH);
        if (cell.box) {
            BoxDrawing::draw(p, cell.text.at(0).isHighSurrogate() ? cell.text.toUcs4().value(0) : cell.text.at(0).unicode(),
                             r, color);
            return;
        }
        const int fi = Fonts::index(cell.bold, cell.italic);
        p.setFont(m_fonts.plain[fi]);
        p.setPen(color);
        const qreal adv = m_fonts.plainMetrics[fi]->horizontalAdvance(cell.text);
        if (adv > r.width() * 1.05) {
            p.save();
            p.translate(r.left(), 0);
            p.scale(r.width() / adv, 1);
            p.drawText(QPointF(0, m_fonts.baseline), cell.text);
            p.restore();
        } else {
            p.drawText(QPointF(r.left() + (r.width() - adv) / 2, m_fonts.baseline), cell.text);
        }
    };

    QString run;
    int runStart = 0;
    const Cell *runStyle = nullptr;
    auto flush = [&] {
        if (run.isEmpty())
            return;
        p.setFont(m_fonts.pick(runStyle->bold, runStyle->italic));
        p.setPen(runStyle->fg);
        p.drawText(QPointF(runStart * m_cellW, m_fonts.baseline), run);
        run.clear();
        runStyle = nullptr;
    };
    for (int cx = 0; cx < cols; ++cx) {
        const Cell &cell = cells[cx];
        if (cell.text.isEmpty()) {
            flush();
            continue;
        }
        const bool ascii = !cell.box && !cell.wide && cell.text.size() == 1 && cell.text.at(0).unicode() > 0x20
                           && cell.text.at(0).unicode() < 0x7f;
        if (ascii && runStyle && runStyle->fg == cell.fg && runStyle->bold == cell.bold && runStyle->italic == cell.italic
            && runStart + run.size() == cx) {
            run += cell.text;
            continue;
        }
        flush();
        if (ascii) {
            run = cell.text;
            runStart = cx;
            runStyle = &cell;
        } else {
            drawCell(cell, cx, cell.fg);
        }
    }
    flush();

    // Pass 3: lines.
    const int thick = std::max(1, int(std::round(m_cellH / 16.0)));
    const int ulY = std::min(m_cellH - thick, m_fonts.baseline + std::max(1, thick));
    for (const Deco &d : decos) {
        const int x0 = d.x * m_cellW, w = d.span * m_cellW;
        switch (d.underline) {
        case GHOSTTY_SGR_UNDERLINE_SINGLE:
            p.fillRect(QRect(x0, ulY, w, thick), d.color);
            break;
        case GHOSTTY_SGR_UNDERLINE_DOUBLE:
            p.fillRect(QRect(x0, ulY - thick, w, thick), d.color);
            p.fillRect(QRect(x0, std::min(m_cellH - thick, ulY + thick), w, thick), d.color);
            break;
        case GHOSTTY_SGR_UNDERLINE_CURLY: {
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(d.color, thick));
            QPainterPath path;
            const qreal amp = std::max(1.0, thick * 1.2);
            path.moveTo(x0, ulY);
            for (int i = 0; i < d.span * 2; ++i) {
                const qreal half = m_cellW / 2.0;
                path.quadTo(x0 + i * half + half / 2, ulY + ((i % 2) ? amp : -amp), x0 + (i + 1) * half, ulY);
            }
            p.drawPath(path);
            p.restore();
            break;
        }
        case GHOSTTY_SGR_UNDERLINE_DOTTED:
            for (int px = x0; px < x0 + w; px += thick * 2)
                p.fillRect(QRect(px, ulY, thick, thick), d.color);
            break;
        case GHOSTTY_SGR_UNDERLINE_DASHED:
            for (int px = x0; px < x0 + w; px += thick * 6)
                p.fillRect(QRect(px, ulY, std::min(thick * 4, x0 + w - px), thick), d.color);
            break;
        default:
            break;
        }
        if (d.strike)
            p.fillRect(QRect(x0, m_fonts.strikeY, w, thick), d.color);
        if (d.overline)
            p.fillRect(QRect(x0, 0, w, thick), d.color);
    }

    // Cursor and preedit.
    GhosttyRenderStateCursor cursor = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cursor);
    if (!cursor.viewport_has_value || int(cursor.viewport_y) != y)
        return;
    int cx = cursor.viewport_x;
    if (cursor.wide_tail && cx > 0)
        --cx;

    const QColor cursorColor = colors.cursor_has_value ? toQColor(colors.cursor) : m_theme.cursor.value_or(defFg);
    if (!m_preedit.isEmpty()) {
        // The composing text (a Hangul syllable, say) sits on top of the
        // cells at the cursor, underlined, until the input method commits it.
        int w = 0;
        for (char32_t ch : m_preedit.toUcs4()) {
            w += std::max(1, int(ghostty_unicode_codepoint_width(ch)));
        }
        const QRect r(cx * m_cellW, 0, std::min(w, cols - cx) * m_cellW, m_cellH);
        p.fillRect(r, defBg);
        p.setFont(m_fonts.plain[0]);
        p.setPen(defFg);
        int px = r.left();
        for (char32_t ch : m_preedit.toUcs4()) {
            const int cw = std::max(1, int(ghostty_unicode_codepoint_width(ch)));
            const QString s = QString::fromUcs4(&ch, 1);
            const qreal adv = m_fonts.plainMetrics[0]->horizontalAdvance(s);
            p.drawText(QPointF(px + (cw * m_cellW - adv) / 2, m_fonts.baseline), s);
            px += cw * m_cellW;
        }
        p.fillRect(QRect(r.left(), m_cellH - thick, r.width(), thick), cursorColor);
        return;
    }
    if (!cursor.visible || (m_blinkTimer.isActive() && !m_cursorBlinkOn))
        return;

    const int cw = (cx < cols && cells[cx].wide) ? 2 : 1;
    const QRect r(cx * m_cellW, 0, cw * m_cellW, m_cellH);
    auto style = cursor.visual_style;
    if (!m_focused)
        style = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW;
    switch (style) {
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK: {
        p.fillRect(r, cursorColor);
        if (cx < cols && !cells[cx].text.isEmpty()) {
            QColor textColor = m_theme.cursorText.value_or(defBg);
            // A theme's cursor-text that does not stand out against the
            // cursor (GitHub Light's does not) falls back to the background.
            if (qAbs(textColor.lightnessF() - cursorColor.lightnessF()) < 0.3f)
                textColor = qAbs(defBg.lightnessF() - cursorColor.lightnessF()) > qAbs(defFg.lightnessF() - cursorColor.lightnessF())
                                ? defBg
                                : defFg;
            Cell c = cells[cx];
            if (c.box)
                BoxDrawing::draw(p, c.text.toUcs4().value(0), r, textColor);
            else
                drawCell(c, cx, textColor);
        }
        break;
    }
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
        p.fillRect(QRect(r.left(), 0, std::max(2, thick * 2), m_cellH), cursorColor);
        break;
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
        p.fillRect(QRect(r.left(), m_cellH - std::max(2, thick * 2), r.width(), std::max(2, thick * 2)), cursorColor);
        break;
    default: {
        const int t = std::max(1, thick);
        p.fillRect(QRect(r.left(), 0, r.width(), t), cursorColor);
        p.fillRect(QRect(r.left(), m_cellH - t, r.width(), t), cursorColor);
        p.fillRect(QRect(r.left(), 0, t, m_cellH), cursorColor);
        p.fillRect(QRect(r.right() - t + 1, 0, t, m_cellH), cursorColor);
        break;
    }
    }
}

QSGNode *TerminalItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *root = static_cast<RootNode *>(old);
    if (!root) {
        root = new RootNode;
        root->background = window()->createRectangleNode();
        root->appendChildNode(root->background);
    }
    root->background->setRect(boundingRect());
    root->background->setColor(m_bgForNode.isValid() ? m_bgForNode : m_theme.background);

    // Pinch preview: the rows scale about the centre until the gesture ends
    // and the real re-layout happens.
    QTransform t;
    if (!qFuzzyCompare(m_pinchScale, 1.0)) {
        t.translate(width() / 2, height() / 2);
        t.scale(m_pinchScale, m_pinchScale);
        t.translate(-width() / 2, -height() / 2);
    }

    const int n = int(m_rowImages.size());
    while (int(root->rows.size()) > n) {
        QSGImageNode *node = root->rows.back();
        root->removeChildNode(node);
        delete node;
        root->rows.pop_back();
    }
    while (int(root->rows.size()) < n) {
        // A node enters the tree with a texture: renderers do not expect an
        // image node without one.
        const int y = int(root->rows.size());
        QImage img = m_rowImages[y];
        if (img.isNull()) {
            img = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
        }
        QSGImageNode *node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Nearest);
        node->setTexture(window()->createTextureFromImage(img));
        node->setSourceRect(QRectF(QPointF(0, 0), img.size()));
        node->setRect(QRectF());
        root->appendChildNode(node);
        root->rows.push_back(node);
        m_rowUpload[y] = !m_rowImages[y].isNull() ? false : true;
    }
    for (int y = 0; y < n; ++y) {
        QSGImageNode *node = root->rows[y];
        const QImage &img = m_rowImages[y];
        if (img.isNull())
            continue;
        if (m_rowUpload[y] || !node->texture()) {
            node->setTexture(window()->createTextureFromImage(img));
            node->setSourceRect(QRectF(QPointF(0, 0), img.size()));
            m_rowUpload[y] = false;
        }
        const QRectF rect = t.mapRect(
            QRectF(m_padX / m_dpr, (m_padY + y * m_cellH) / m_dpr, img.width() / m_dpr, img.height() / m_dpr));
        if (node->rect() != rect)
            node->setRect(rect);
    }
    m_geometryDirty = false;
    return root;
}

// ---- coordinates ------------------------------------------------------------

QPoint TerminalItem::cellAt(QPointF pos) const
{
    const int cx = int(std::floor((pos.x() * m_dpr - m_padX) / m_cellW));
    const int cy = int(std::floor((pos.y() * m_dpr - m_padY) / m_cellH));
    return QPoint(qBound(0, cx, std::max(0, m_cols - 1)), qBound(0, cy, std::max(0, m_rows - 1)));
}

QPointF TerminalItem::cellToItem(QPoint cell) const
{
    return QPointF((m_padX + cell.x() * m_cellW) / m_dpr, (m_padY + cell.y() * m_cellH) / m_dpr);
}

QRectF TerminalItem::selectionStartRect() const
{
    QPoint a, b;
    if (!m_session || !m_session->selectionRange(&a, &b) || a.y() < 0)
        return {};
    return QRectF(cellToItem(a), QSizeF(cellWidth(), cellHeight()));
}

QRectF TerminalItem::selectionEndRect() const
{
    QPoint a, b;
    if (!m_session || !m_session->selectionRange(&a, &b) || b.y() >= m_rows)
        return {};
    return QRectF(cellToItem(b), QSizeF(cellWidth(), cellHeight()));
}

// ---- keyboard and input method ----------------------------------------------

GhosttyMods TerminalItem::currentMods(Qt::KeyboardModifiers m) const
{
    GhosttyMods mods = 0;
    if (m & Qt::ShiftModifier)
        mods |= GHOSTTY_MODS_SHIFT;
    if ((m & Qt::ControlModifier) || m_ctrlLatched)
        mods |= GHOSTTY_MODS_CTRL;
    if ((m & Qt::AltModifier) || m_altLatched)
        mods |= GHOSTTY_MODS_ALT;
    if (m & Qt::MetaModifier)
        mods |= GHOSTTY_MODS_SUPER;
    return mods;
}

void TerminalItem::consumeLatches()
{
    // Latches are one-shot: the extra-keys row sets them, the next key uses them.
    setCtrlLatched(false);
    setAltLatched(false);
}

void TerminalItem::keyPressEvent(QKeyEvent *e)
{
    if (!m_session) {
        e->ignore();
        return;
    }
    const auto m = e->modifiers();
    // Terminal shortcuts that never reach the application.
    if ((m & Qt::ControlModifier) && (m & Qt::ShiftModifier)) {
        if (e->key() == Qt::Key_C) {
            m_session->copySelection();
            return;
        }
        if (e->key() == Qt::Key_V) {
            m_session->pasteClipboard(true);
            return;
        }
    }
    if ((m & Qt::ShiftModifier) && e->key() == Qt::Key_PageUp && !m_session->alternateScreen()) {
        m_session->scrollLines(-(m_rows - 1));
        return;
    }
    if ((m & Qt::ShiftModifier) && e->key() == Qt::Key_PageDown && !m_session->alternateScreen()) {
        m_session->scrollLines(m_rows - 1);
        return;
    }
    if ((m & Qt::ControlModifier) && (e->key() == Qt::Key_Equal || e->key() == Qt::Key_Plus)) {
        zoom(1.1);
        return;
    }
    if ((m & Qt::ControlModifier) && e->key() == Qt::Key_Minus) {
        zoom(1 / 1.1);
        return;
    }

    KeyInfo info = keyFromEvdev(e->nativeScanCode() > 8 ? e->nativeScanCode() - 8 : 0);
    if (info.key == GHOSTTY_KEY_UNIDENTIFIED)
        info = keyFromQt(e->key());
    QString text = e->text();
    // The encoder wants the character without Ctrl applied, never C0 controls.
    if (!text.isEmpty() && (text.at(0).unicode() < 0x20 || text.at(0).unicode() == 0x7f))
        text.clear();
    if (info.key == GHOSTTY_KEY_UNIDENTIFIED && text.isEmpty()) {
        e->ignore();
        return;
    }
    if (info.key == GHOSTTY_KEY_UNIDENTIFIED) {
        // A key we have no code for (another layout's letter): its text.
        if (m_ctrlLatched || m_altLatched) {
            const KeyInfo ci = keyFromChar(text.toUcs4().value(0));
            if (ci.key != GHOSTTY_KEY_UNIDENTIFIED)
                info = ci;
        }
        if (info.key == GHOSTTY_KEY_UNIDENTIFIED) {
            m_session->sendText(text);
            consumeLatches();
            return;
        }
    }
    const bool isModifier = e->key() == Qt::Key_Shift || e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt
                            || e->key() == Qt::Key_Meta || e->key() == Qt::Key_AltGr;
    m_session->clearSelection();
    m_session->sendKey(info.key, currentMods(m), text, info.unshifted,
                       e->isAutoRepeat() ? GHOSTTY_KEY_ACTION_REPEAT : GHOSTTY_KEY_ACTION_PRESS);
    if (!isModifier)
        consumeLatches();
    m_lastActivity.restart();
}

void TerminalItem::keyReleaseEvent(QKeyEvent *e)
{
    if (!m_session || e->isAutoRepeat())
        return;
    KeyInfo info = keyFromEvdev(e->nativeScanCode() > 8 ? e->nativeScanCode() - 8 : 0);
    if (info.key == GHOSTTY_KEY_UNIDENTIFIED)
        info = keyFromQt(e->key());
    if (info.key == GHOSTTY_KEY_UNIDENTIFIED)
        return;
    // Only the kitty protocol's "report events" mode makes this produce bytes.
    QString text = e->text();
    if (!text.isEmpty() && (text.at(0).unicode() < 0x20 || text.at(0).unicode() == 0x7f))
        text.clear();
    m_session->sendKey(info.key, currentMods(e->modifiers()), text, info.unshifted, GHOSTTY_KEY_ACTION_RELEASE);
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *e)
{
    if (!m_session) {
        e->ignore();
        return;
    }
    // "Delete n characters before the cursor" from a keyboard that thinks it
    // is editing text: the terminal equivalent is n backspaces.
    if (e->replacementLength() > 0 && e->replacementStart() < 0) {
        for (int i = 0; i < e->replacementLength(); ++i)
            m_session->sendKey(GHOSTTY_KEY_BACKSPACE, 0, {}, 0);
    }
    const QString commit = e->commitString();
    if (!commit.isEmpty()) {
        m_session->clearSelection();
        const auto chars = commit.toUcs4();
        if ((m_ctrlLatched || m_altLatched) && chars.size() == 1) {
            const KeyInfo ci = keyFromChar(chars[0]);
            if (ci.key != GHOSTTY_KEY_UNIDENTIFIED) {
                GhosttyMods mods = currentMods(ci.shifted ? Qt::ShiftModifier : Qt::NoModifier);
                m_session->sendKey(ci.key, mods, ci.key == GHOSTTY_KEY_ENTER || ci.key == GHOSTTY_KEY_TAB ? QString() : commit,
                                   ci.unshifted);
            } else {
                // Alt + a character with no key (Hangul, say): ESC prefix.
                if (m_altLatched)
                    m_session->writeRaw(QByteArrayLiteral("\x1b"));
                m_session->sendText(commit);
            }
        } else if (commit == u"\n" || commit == u"\r") {
            m_session->sendKey(GHOSTTY_KEY_ENTER, currentMods({}), {}, 0);
        } else if (commit == u"\t") {
            m_session->sendKey(GHOSTTY_KEY_TAB, currentMods({}), {}, 0);
        } else {
            m_session->sendText(commit);
        }
        consumeLatches();
        m_lastActivity.restart();
    }
    if (e->preeditString() != m_preedit) {
        m_preedit = e->preeditString();
        Q_EMIT preeditChanged();
        scheduleRender();
        if (QInputMethod *im = QGuiApplication::inputMethod())
            im->update(Qt::ImCursorRectangle);
    }
    e->accept();
}

QVariant TerminalItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImHints:
        // No autocorrect, no auto capitals, no prediction bar: a terminal
        // takes characters as typed. Hangul composition still works, it is
        // preedit, not prediction.
        return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhPreferLowercase | Qt::ImhNoTextHandles
                   | Qt::ImhMultiLine);
    case Qt::ImCursorRectangle: {
        if (!m_session)
            return QRectF();
        uint16_t cx = 0, cy = 0;
        ghostty_terminal_get(m_session->terminal(), GHOSTTY_TERMINAL_DATA_CURSOR_X, &cx);
        ghostty_terminal_get(m_session->terminal(), GHOSTTY_TERMINAL_DATA_CURSOR_Y, &cy);
        return QRectF(cellToItem(QPoint(cx, cy)), QSizeF(cellWidth(), cellHeight()));
    }
    case Qt::ImSurroundingText:
    case Qt::ImTextBeforeCursor:
    case Qt::ImTextAfterCursor:
    case Qt::ImCurrentSelection:
        return QString();
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;
    case Qt::ImFont:
        return m_fonts.regular;
    case Qt::ImEnterKeyType:
        return int(Qt::EnterKeyReturn);
    case Qt::ImReadOnly:
        return false;
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void TerminalItem::focusInEvent(QFocusEvent *e)
{
    QQuickItem::focusInEvent(e);
    m_focused = true;
    if (m_session)
        m_session->sendFocus(true);
    m_lastActivity.restart();
    scheduleRender();
}

void TerminalItem::focusOutEvent(QFocusEvent *e)
{
    QQuickItem::focusOutEvent(e);
    m_focused = false;
    if (!m_preedit.isEmpty()) {
        // An abandoned composition is committed, like a text field does.
        if (m_session)
            m_session->sendText(m_preedit);
        m_preedit.clear();
        Q_EMIT preeditChanged();
    }
    if (m_session)
        m_session->sendFocus(false);
    scheduleRender();
}

void TerminalItem::sendKeyName(const QString &name)
{
    if (!m_session)
        return;
    static const QHash<QString, GhosttyKey> keys = {
        {QStringLiteral("Esc"), GHOSTTY_KEY_ESCAPE},     {QStringLiteral("Tab"), GHOSTTY_KEY_TAB},
        {QStringLiteral("Enter"), GHOSTTY_KEY_ENTER},    {QStringLiteral("Backspace"), GHOSTTY_KEY_BACKSPACE},
        {QStringLiteral("Up"), GHOSTTY_KEY_ARROW_UP},    {QStringLiteral("Down"), GHOSTTY_KEY_ARROW_DOWN},
        {QStringLiteral("Left"), GHOSTTY_KEY_ARROW_LEFT}, {QStringLiteral("Right"), GHOSTTY_KEY_ARROW_RIGHT},
        {QStringLiteral("Home"), GHOSTTY_KEY_HOME},      {QStringLiteral("End"), GHOSTTY_KEY_END},
        {QStringLiteral("PgUp"), GHOSTTY_KEY_PAGE_UP},   {QStringLiteral("PgDn"), GHOSTTY_KEY_PAGE_DOWN},
        {QStringLiteral("Ins"), GHOSTTY_KEY_INSERT},     {QStringLiteral("Del"), GHOSTTY_KEY_DELETE},
        {QStringLiteral("F1"), GHOSTTY_KEY_F1},          {QStringLiteral("F2"), GHOSTTY_KEY_F2},
        {QStringLiteral("F3"), GHOSTTY_KEY_F3},          {QStringLiteral("F4"), GHOSTTY_KEY_F4},
        {QStringLiteral("F5"), GHOSTTY_KEY_F5},          {QStringLiteral("F6"), GHOSTTY_KEY_F6},
        {QStringLiteral("F7"), GHOSTTY_KEY_F7},          {QStringLiteral("F8"), GHOSTTY_KEY_F8},
        {QStringLiteral("F9"), GHOSTTY_KEY_F9},          {QStringLiteral("F10"), GHOSTTY_KEY_F10},
        {QStringLiteral("F11"), GHOSTTY_KEY_F11},        {QStringLiteral("F12"), GHOSTTY_KEY_F12},
    };
    m_session->clearSelection();
    const auto it = keys.constFind(name);
    if (it != keys.constEnd()) {
        m_session->sendKey(*it, currentMods({}), {}, 0);
    } else {
        sendString(name);
        return;
    }
    consumeLatches();
}

void TerminalItem::sendString(const QString &text)
{
    if (!m_session || text.isEmpty())
        return;
    const auto chars = text.toUcs4();
    if ((m_ctrlLatched || m_altLatched) && chars.size() == 1) {
        const KeyInfo ci = keyFromChar(chars[0]);
        if (ci.key != GHOSTTY_KEY_UNIDENTIFIED) {
            m_session->sendKey(ci.key, currentMods(ci.shifted ? Qt::ShiftModifier : Qt::NoModifier), text, ci.unshifted);
            consumeLatches();
            return;
        }
    }
    m_session->sendText(text);
    consumeLatches();
}

void TerminalItem::showKeyboard()
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (QInputMethod *im = QGuiApplication::inputMethod())
        im->show();
}

void TerminalItem::zoom(qreal factor)
{
    setFontSize(std::round(m_fontSize * factor * 2) / 2);
}

// ---- touch ------------------------------------------------------------------

void TerminalItem::handleTap(QPointF pos)
{
    const QPoint cell = cellAt(pos);
    if (m_selectMode || (m_session && m_session->hasSelection())) {
        endSelectionMode();
        return;
    }
    if (m_session && m_session->mouseTracking()) {
        sendMouseButton(pos, GHOSTTY_MOUSE_ACTION_PRESS, GHOSTTY_MOUSE_BUTTON_LEFT, {});
        sendMouseButton(pos, GHOSTTY_MOUSE_ACTION_RELEASE, GHOSTTY_MOUSE_BUTTON_LEFT, {});
    } else if (m_session) {
        const QString url = m_session->hyperlinkAt(cell.x(), cell.y());
        if (!url.isEmpty()) {
            Q_EMIT linkActivated(url);
            return;
        }
    }
    showKeyboard();
    Q_EMIT tapped();
}

void TerminalItem::beginTouchSelection(QPointF pos)
{
    if (!m_session)
        return;
    m_selAnchor = cellAt(pos);
    m_selHead = m_selAnchor;
    m_session->selectWordAt(m_selAnchor.x(), m_selAnchor.y());
    m_selectMode = true;
    Q_EMIT selectingChanged();
    Q_EMIT selectionGeometryChanged();
}

void TerminalItem::moveSelectionEdge(bool start, qreal x, qreal y)
{
    if (!m_session)
        return;
    QPoint a, b;
    if (!m_session->selectionRange(&a, &b))
        return;
    const QPoint p = cellAt(QPointF(x, y));
    if (start)
        m_session->selectRange(b, p, false);
    else
        m_session->selectRange(a, p, false);
    Q_EMIT selectionGeometryChanged();
}

void TerminalItem::endSelectionMode()
{
    if (m_session)
        m_session->clearSelection();
    if (m_selectMode) {
        m_selectMode = false;
        Q_EMIT selectingChanged();
    }
    Q_EMIT selectionGeometryChanged();
}

void TerminalItem::scrollByPixels(qreal dy)
{
    if (!m_session || m_cellH <= 0)
        return;
    // Finger down = content down = back in history.
    m_scrollAccum += dy * m_dpr;
    const int lines = int(m_scrollAccum / m_cellH);
    if (!lines)
        return;
    m_scrollAccum -= lines * m_cellH;
    if (m_session->mouseTracking()) {
        const QPointF pos(width() / 2, height() / 2);
        const GhosttyMouseButton b = lines > 0 ? GHOSTTY_MOUSE_BUTTON_FOUR : GHOSTTY_MOUSE_BUTTON_FIVE;
        for (int i = 0; i < std::abs(lines); ++i)
            sendMouseButton(pos, GHOSTTY_MOUSE_ACTION_PRESS, b, {});
    } else if (m_session->alternateScreen()) {
        // Full-screen programs without mouse support (less, man): arrows.
        const GhosttyKey k = lines > 0 ? GHOSTTY_KEY_ARROW_UP : GHOSTTY_KEY_ARROW_DOWN;
        for (int i = 0; i < std::abs(lines); ++i)
            m_session->sendKey(k, 0, {}, 0);
    } else {
        m_session->scrollLines(-lines);
    }
}

void TerminalItem::startFling(qreal velocity)
{
    // Pixels per second, decaying. The timer runs only while it moves.
    m_velocity = velocity;
    if (std::abs(m_velocity) < 300) {
        m_flingTimer.stop();
        return;
    }
    m_flingClock.start();
    m_flingTimer.start(16, Qt::PreciseTimer, this);
}

void TerminalItem::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == m_longPressTimer.timerId()) {
        m_longPressTimer.stop();
        if (m_gesture == Gesture::Pending) {
            m_gesture = Gesture::Select;
            beginTouchSelection(m_touchStart);
        }
    } else if (e->timerId() == m_flingTimer.timerId()) {
        const qreal dt = m_flingClock.restart() / 1000.0;
        scrollByPixels(m_velocity * dt);
        m_velocity *= std::pow(0.05, dt); // loses 95% per second
        if (std::abs(m_velocity) < 60)
            m_flingTimer.stop();
    } else if (e->timerId() == m_blinkTimer.timerId()) {
        m_cursorBlinkOn = !m_cursorBlinkOn;
        scheduleRender();
    } else {
        QQuickItem::timerEvent(e);
    }
}

void TerminalItem::touchEvent(QTouchEvent *e)
{
    const auto &points = e->points();
    if (points.size() >= 2) {
        // Pinch: preview by scaling, re-layout once at the end.
        const qreal dist = QLineF(points[0].position(), points[1].position()).length();
        if (m_gesture != Gesture::Pinch) {
            m_longPressTimer.stop();
            m_flingTimer.stop();
            m_gesture = Gesture::Pinch;
            m_pinchStartDist = std::max<qreal>(1, dist);
            m_pinchStartFont = m_fontSize;
        }
        m_pinchScale = qBound<qreal>(4 / m_pinchStartFont, dist / m_pinchStartDist, 48 / m_pinchStartFont);
        update();
        if (e->touchPointStates() & QEventPoint::Released) {
            const qreal s = m_pinchScale;
            m_pinchScale = 1;
            setFontSize(std::round(m_pinchStartFont * s * 2) / 2);
            m_gesture = Gesture::None;
            update();
        }
        e->accept();
        return;
    }
    if (points.isEmpty())
        return;
    const QEventPoint &pt = points.first();
    const QPointF pos = pt.position();
    switch (pt.state()) {
    case QEventPoint::Pressed:
        m_flingTimer.stop();
        m_touchStart = m_touchLast = pos;
        m_scrollAccum = 0;
        m_velocity = 0;
        m_moveClock.start();
        if (m_gesture == Gesture::Pinch)
            break;
        m_gesture = Gesture::Pending;
        m_longPressTimer.start(kLongPressMs, this);
        break;
    case QEventPoint::Updated: {
        const qreal threshold = QGuiApplication::styleHints()->startDragDistance();
        if (m_gesture == Gesture::Pending && QLineF(pos, m_touchStart).length() > threshold) {
            m_longPressTimer.stop();
            m_gesture = Gesture::Scroll;
            // m_touchLast stays at the press point: the whole drag counts,
            // so a short swipe in less still moves a line.
        }
        if (m_gesture == Gesture::Scroll) {
            const qreal dy = pos.y() - m_touchLast.y();
            const qreal dt = std::max<qint64>(1, m_moveClock.restart()) / 1000.0;
            m_velocity = 0.7 * (dy / dt) + 0.3 * m_velocity;
            scrollByPixels(dy);
            m_touchLast = pos;
        } else if (m_gesture == Gesture::Select && m_session) {
            const QPoint head = cellAt(pos);
            if (head != m_selHead) {
                m_selHead = head;
                m_session->selectRange(m_selAnchor, head, true);
                Q_EMIT selectionGeometryChanged();
            }
        }
        break;
    }
    case QEventPoint::Released:
        m_longPressTimer.stop();
        if (m_gesture == Gesture::Pending)
            handleTap(pos);
        else if (m_gesture == Gesture::Scroll)
            startFling(m_moveClock.elapsed() > 80 ? 0 : m_velocity);
        else if (m_gesture == Gesture::Select)
            Q_EMIT contextMenuRequested(pos.x(), pos.y());
        m_gesture = Gesture::None;
        break;
    default:
        break;
    }
    e->accept();
}

// ---- mouse (a real mouse, or a desktop while testing) -----------------------

void TerminalItem::sendMouseButton(QPointF pos, GhosttyMouseAction action, GhosttyMouseButton button,
                                   Qt::KeyboardModifiers mods)
{
    if (!m_session)
        return;
    const QPointF p((pos.x() * m_dpr - m_padX), (pos.y() * m_dpr - m_padY));
    m_session->sendMouse(action, button, p, currentMods(mods), QSizeF(m_cols * m_cellW, m_rows * m_cellH),
                         QSizeF(m_cellW, m_cellH));
}

static GhosttyMouseButton toButton(Qt::MouseButton b)
{
    switch (b) {
    case Qt::LeftButton: return GHOSTTY_MOUSE_BUTTON_LEFT;
    case Qt::RightButton: return GHOSTTY_MOUSE_BUTTON_RIGHT;
    case Qt::MiddleButton: return GHOSTTY_MOUSE_BUTTON_MIDDLE;
    default: return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    }
}

void TerminalItem::mousePressEvent(QMouseEvent *e)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (!m_session)
        return;
    if (m_session->mouseTracking() && !(e->modifiers() & Qt::ShiftModifier)) {
        sendMouseButton(e->position(), GHOSTTY_MOUSE_ACTION_PRESS, toButton(e->button()), e->modifiers());
        m_gesture = Gesture::MouseDrag;
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_session->clearSelection();
        m_selAnchor = cellAt(e->position());
        m_mouseSelecting = true;
    } else if (e->button() == Qt::MiddleButton) {
        const QString text = QGuiApplication::clipboard()->text(QClipboard::Selection);
        m_session->paste(text, true);
    } else if (e->button() == Qt::RightButton) {
        Q_EMIT contextMenuRequested(e->position().x(), e->position().y());
    }
}

void TerminalItem::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_session)
        return;
    if (m_gesture == Gesture::MouseDrag) {
        const QPoint cell = cellAt(e->position());
        if (cell != m_lastMouseCell) {
            m_lastMouseCell = cell;
            GhosttyMouseButton b = GHOSTTY_MOUSE_BUTTON_UNKNOWN;
            if (e->buttons() & Qt::LeftButton) b = GHOSTTY_MOUSE_BUTTON_LEFT;
            else if (e->buttons() & Qt::RightButton) b = GHOSTTY_MOUSE_BUTTON_RIGHT;
            else if (e->buttons() & Qt::MiddleButton) b = GHOSTTY_MOUSE_BUTTON_MIDDLE;
            sendMouseButton(e->position(), GHOSTTY_MOUSE_ACTION_MOTION, b, e->modifiers());
        }
        return;
    }
    if (m_mouseSelecting) {
        const QPoint head = cellAt(e->position());
        if (head != m_selHead) {
            m_selHead = head;
            m_session->selectRange(m_selAnchor, head, false);
        }
    }
}

void TerminalItem::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_session)
        return;
    if (m_gesture == Gesture::MouseDrag) {
        sendMouseButton(e->position(), GHOSTTY_MOUSE_ACTION_RELEASE, toButton(e->button()), e->modifiers());
        m_gesture = Gesture::None;
        return;
    }
    if (m_mouseSelecting) {
        m_mouseSelecting = false;
        if (m_session->hasSelection()) {
            const QString text = m_session->selectedText();
            if (!text.isEmpty() && QGuiApplication::clipboard()->supportsSelection())
                QGuiApplication::clipboard()->setText(text, QClipboard::Selection);
        } else {
            const QPoint cell = cellAt(e->position());
            const QString url = m_session->hyperlinkAt(cell.x(), cell.y());
            if (!url.isEmpty() && (e->modifiers() & Qt::ControlModifier))
                Q_EMIT linkActivated(url);
        }
    }
}

void TerminalItem::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!m_session || m_session->mouseTracking())
        return;
    const QPoint c = cellAt(e->position());
    m_session->selectWordAt(c.x(), c.y());
    m_mouseSelecting = false;
}

void TerminalItem::wheelEvent(QWheelEvent *e)
{
    if (!m_session)
        return;
    const QPoint px = e->pixelDelta();
    if (!px.isNull())
        scrollByPixels(px.y());
    else
        scrollByPixels(e->angleDelta().y() / 120.0 * 3 * cellHeight());
    e->accept();
}
